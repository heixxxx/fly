#include <storage/cpp/database.h>
#include <storage/cpp/compressor.h>
#include <network/cpp/data_client.h>
#include <serialization/cpp/object_header.h>
#include <storage/cpp/object_cache.h>
#include <storage/cpp/compression_utils.h>
#include <storage/cpp/decompressing_streambuf.h>
#include <core/cpp/config.h>
#include <log/cpp/logger.h>
#include <common/cpp/writer_id.h>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <functional>
#include <random>
#include <istream>
#include <cassert>

namespace fs = std::filesystem;

Database::Database(const CMString& base_path, const CMString& data_path, uint64_t worker_id, const CMString& host, const CMString& existing_db_id)
    : base_path_(base_path)
    , data_path_(data_path)
    , writer_id_(generate_writer_id())
    , db_id_(existing_db_id.empty() ? generate_db_id(base_path) : existing_db_id)
    , host_(host) {
    (void)worker_id;  // worker_id kept for API compat; writer_id_ is used for file naming

    fly::DataService::instance()->register_database(db_id_, base_path_, data_path_, writer_id_);

    if (existing_db_id.empty()) {
        ensure_directory_exists(base_path_);
        if (!data_path_.empty()) {
            ensure_directory_exists(data_path_);
        }

        write_db_meta_header();
    }

    CMString frozen_marker = base_path_ + "/_FROZEN";
    if (fs::exists(frozen_marker)) {
        is_frozen_ = true;
    }

    auto config = Config::instance();
    CMString comp_type_str = config->get_str("compression_type");
    compression_type_ = CompressorFactory::type_from_name(comp_type_str);
    compression_level_ = static_cast<int>(config->get_int("compression_level"));
    serialize_chunk_size_ = config->get_int("serialize_chunk_size");

    writer_ = CMMakeUnique<DataWriter>(
        base_path_, data_path_, writer_id_,
        config->get_int("aggregation_threshold"),
        host_
    );
    reader_ = CMMakeUnique<DataReader>(base_path_, data_path_, writer_id_);
    temp_store_ = CMMakeUnique<fly::TempStore>();
}

Database::~Database() {
    fly::DataService::instance()->drain_write_back();
    fly::DataService::instance()->cleanup_temp_entries(db_id_);
    fly::DataService::instance()->unregister_database(db_id_);
}

Database::CompressResult Database::compress_buffered_data(
    const char* data, int64_t data_size,
    const CMString& py_name, FlyBuffer& target) {

    ObjectHeader header;
    header.total_size_ = 0;
    header.chunk_count_ = 0;
    header.compression_type_ = static_cast<uint8_t>(compression_type_);
    header.py_name_ = py_name;
    header.py_name_len_ = static_cast<uint16_t>(py_name.size());
    CMString header_bytes = header.serialize();

    FlyBufferStreamBuf fly_buf(target);
    CountingStreamBuf counting_buf(fly_buf);
    std::ostream counting_stream(&counting_buf);

    counting_stream.write(header_bytes.data(), static_cast<std::streamsize>(header_bytes.size()));

    int64_t total_uncompressed = 0;
    int32_t chunk_count = 0;
    {
        auto compressor = compression_type_ != CompressionType::NONE
            ? CompressorFactory::create(compression_type_) : nullptr;
        CompressingStreamBuf csbuf(counting_stream, std::move(compressor),
                                    serialize_chunk_size_);
        std::ostream os(&csbuf);
        os.write(data, static_cast<std::streamsize>(data_size));
        os.flush();
        total_uncompressed = csbuf.total_uncompressed();
        chunk_count = csbuf.chunk_count();
    }
    counting_stream.flush();

    header.total_size_ = static_cast<uint64_t>(total_uncompressed);
    header.chunk_count_ = static_cast<uint32_t>(chunk_count);
    CMString real_header = header.serialize();
    std::memcpy(target.data(), real_header.data(), real_header.size());

    return {total_uncompressed, chunk_count};
}

// Shared commit logic: cache → register → enqueue disk write.
fly::WriteErrorType Database::commit_write(const CMString& object_name,
                                           const CMString& full,
                                           FlyBufferPtr record,
                                           int64_t original_size,
                                           int32_t chunk_count,
                                           bool backup) {
    // 1. Populate low-tier cache immediately — remote reads can serve from
    //    cache without waiting for the background disk write.
    {
        size_t sz = original_size > 0 ? static_cast<size_t>(original_size) : record->size();
        fly::ObjectCache::instance().put_low(full, record, sz);
    }

    // 2. Register write with master. Only NOW does the master mark data ready
    //    and schedule dependent tasks — by which point the cache is populated
    //    and remote reads can be served immediately.
    fly::DataService::instance()->on_write_started(db_id_, full);
    auto [reg_error, reg_error_type] = fly::WorkerAgentContext::register_write(db_id_, object_name);

    if (reg_error_type == fly::TaskErrorType::WRITE_DUPLICATE_SKIPPED) {
        fly::ObjectCache::instance().remove(full);
        fly::DataService::instance()->on_write_failed(db_id_, full, "duplicate skipped");
        fly::WorkerAgentContext::set_last_error_type(fly::TaskErrorType::WRITE_DUPLICATE_SKIPPED);
        return fly::WriteErrorType::DUPLICATE_SKIPPED;
    }
    if (reg_error_type == fly::TaskErrorType::WRITE_PROVENANCE_MISMATCH) {
        fly::ObjectCache::instance().remove(full);
        fly::DataService::instance()->on_write_failed(db_id_, full, reg_error);
        ERR("Write registration failed: {} (type={})", reg_error, static_cast<int>(reg_error_type));
        return fly::WriteErrorType::REGISTRATION_FAILED;
    }
    if (reg_error_type == fly::TaskErrorType::WRITE_REGISTRATION_TIMEOUT) {
        fly::ObjectCache::instance().remove(full);
        fly::DataService::instance()->on_write_failed(db_id_, full, reg_error);
        ERR("Write registration timeout: {}", reg_error);
        return fly::WriteErrorType::REGISTRATION_TIMEOUT;
    }

    // 3. Registration succeeded — enqueue background disk write.
    DataWriter* w = writer_.get();
    auto caller_record_func = fly::WorkerAgentContext::current_record_func();
    auto caller_backup_func = backup ? fly::WorkerAgentContext::current_backup_func() : std::function<void(const fly::CMString&, const fly::CMString&)>{};
    CMString write_hash = fly::WorkerAgentContext::get_current_write_hash();

    auto execute = [w, name = full, original_size, chunk_count, record, write_hash]() {
        w->write_record(name, original_size, chunk_count, *record, write_hash);
        w->flush();
    };

    auto complete = [full, db_id = this->db_id_, object_name,
                     caller_record_func, caller_backup_func, w, backup]() {
        auto ds = fly::DataService::instance();
        auto entries = w->get_all_entries(full);
        if (entries.has_value()) {
            ds->on_write_completed(db_id, full, entries.value());
        }
        ds->on_object_flushed(full);
        if (caller_record_func) {
            caller_record_func(db_id, object_name);
        }
        if (backup && caller_backup_func) {
            caller_backup_func(db_id, object_name);
        }
    };

    fly::WriteRequest req;
    req.execute_ = std::move(execute);
    req.on_complete_ = std::move(complete);
    fly::DataService::instance()->enqueue_write_back(std::move(req));

    return fly::WriteErrorType::OK;
}

void Database::write_temp_pickle(const CMString& object_name,
                                 const char* data, int64_t data_size,
                                 const CMString& py_name) {
    // Compress directly into FlyBufferPtr — zero-copy path, no Python roundtrip.
    auto buf = CMMakeShared<FlyBuffer>();
    compress_buffered_data(data, data_size, py_name, *buf);

    // Register + store as temp (same logic as put_temp_data).
    put_temp_data(object_name, buf);
}

fly::WriteErrorType Database::write_pickle_bytes(const CMString& object_name,
                                         const char* data, int64_t data_size,
                                         const CMString& py_name, bool backup) {
    CMString full = full_name(object_name);
    if (check_frozen()) { fly::WorkerAgentContext::set_last_error_type(fly::TaskErrorType::WRITE_TO_FROZEN_DB); return fly::WriteErrorType::FROZEN_DB; }

    auto record = CMMakeShared<FlyBuffer>();
    auto cr = compress_buffered_data(data, data_size, py_name, *record);

    return commit_write(object_name, full, record, cr.original_size_, cr.chunk_count_, backup);
}

CMString Database::compress_pickle_bytes(const char* data, int64_t data_size,
                                          const CMString& py_name) {
    FlyBuffer buf;
    compress_buffered_data(data, data_size, py_name, buf);
    return CMString(buf.data(), buf.size());
}

std::pair<FlyBufferPtr, CMString> Database::read_object_compressed(const CMString& object_name, bool backup, bool bypass_cache) {
    CMString full = full_name(object_name);
    auto& cache = fly::ObjectCache::instance();

    // Low-tier hit: skip disk/remote IO. Re-parse py_name from the cached header.
    // Skipped when bypass_cache=true (cache="none" mode).
    if (!bypass_cache) {
        if (auto [hit, cached] = cache.get_low(full); hit) {
            CMString py_name;
            try {
                int64_t off = 0;
                auto hdr = ObjectHeader::deserialize({cached->data(), cached->size()}, off);
                py_name = hdr.py_name_;
            } catch (...) {
                // Malformed cached entry — fall through to re-read from source.
            }
            if (!py_name.empty()) {
                // backup=True means the caller wants a persisted local replica.
                // The low-tier cache is memory-only and does NOT imply a local
                // on-disk copy, so a cache hit must still honour the backup
                // request when the object is not yet persisted locally.
                // (Without this, a prior _get_py_name() pre-read would populate
                // the cache and silently skip the backup the caller asked for.)
                if (backup) {
                    auto ds = fly::DataService::instance();
                    if (!ds->has_local_object(full)) {
                        do_backup_write(full, object_name,
                                        CMString(cached->data(), cached->size()), {});
                    }
                }
                return {cached, std::move(py_name)};
            }
            // If header parse failed, evict the stale entry and re-read.
            cache.remove(full);
        }
    }

    auto ds = fly::DataService::instance();
    auto [comp_found, comp_data, comp_py_name, comp_hash, comp_can_still_produce] = ds->read_raw_compressed(full);
    if (!comp_found || !comp_data || comp_data->empty()) {
        ERR("read_object_compressed: no data for '{}'", full);
        return {nullptr, {}};
    }

    if (backup && !ds->has_local_object(full)) {
        do_backup_write(full, object_name, CMString(comp_data->data(), comp_data->size()), comp_hash);
    }

    // Populate low tier: account by uncompressed size from the object header
    // (fall back to compressed size if the header cannot be parsed).
    size_t accounted = comp_data->size();
    try {
        int64_t off = 0;
        auto hdr = ObjectHeader::deserialize({comp_data->data(), comp_data->size()}, off);
        if (hdr.total_size_ > 0) accounted = static_cast<size_t>(hdr.total_size_);
    } catch (...) {
        // Keep compressed-size accounting.
    }
    cache.put_low(full, comp_data, accounted);

    return {comp_data, std::move(comp_py_name)};
}

CMString Database::read_object_py_name(const CMString& object_name) {
    // read_object_compressed already goes through the low-tier cache, so this
    // is a cheap header parse on cache hit (no payload read).
    auto [comp_data, py_name] = read_object_compressed(object_name, false);
    return py_name;
}

void Database::do_backup_write(const CMString& full, const CMString& object_name, CMString compressed_data, const CMString& source_hash) {
    auto ds = fly::DataService::instance();
    ds->on_write_started(db_id_, full);

    auto saved_hash = fly::WorkerAgentContext::get_current_write_hash();
    fly::WorkerAgentContext::clear_current_write_hash();

    auto [reg_error, reg_error_type] = fly::WorkerAgentContext::register_write(db_id_, object_name);
    if (reg_error_type != fly::TaskErrorType::UNKNOWN) {
        ds->on_write_failed(db_id_, full, reg_error);
        fly::WorkerAgentContext::set_current_write_hash(saved_hash);
        ERR("do_backup_write: register_write failed for '{}': {}", object_name, reg_error);
        return;
    }

    int64_t h_off = 0;
    ObjectHeader header = ObjectHeader::deserialize(compressed_data, h_off);

    DataWriter* w = writer_.get();
    auto caller_record_func = fly::WorkerAgentContext::current_record_func();
    CMString backup_hash = source_hash;
    auto record = CMMakeShared<FlyBuffer>();
    record->take(std::move(compressed_data));

    auto execute = [w, name = full, header, record, backup_hash]() {
        w->write_record(name, header.total_size_, header.chunk_count_, *record, backup_hash);
        w->flush();
    };

    auto complete = [full, db_id = db_id_, object_name,
                     caller_record_func, w, saved_hash]() {
        fly::WorkerAgentContext::set_current_write_hash(saved_hash);
        auto dsvc = fly::DataService::instance();
        auto entries = w->get_all_entries(full);
        if (entries.has_value()) {
            dsvc->on_write_completed(db_id, full, entries.value());
        }
        dsvc->on_object_flushed(full);
        if (caller_record_func) {
            caller_record_func(db_id, object_name);
        }
    };

    fly::WriteRequest req;
    req.execute_ = std::move(execute);
    req.on_complete_ = std::move(complete);
    ds->enqueue_write_back(std::move(req));
    ds->drain_write_back();
}

void Database::backup_object(const CMString& object_name) {
    CMString full = full_name(object_name);
    auto ds = fly::DataService::instance();

    auto [found, compressed_data, py_name, source_hash, can_still_produce] = ds->read_raw_compressed(full);
    if (!found || !compressed_data || compressed_data->empty()) {
        ERR("backup_object: no data for '{}'", full);
        return;
    }

    CMString hash_to_use = source_hash.empty() ? fly::WorkerAgentContext::get_current_write_hash() : source_hash;
    do_backup_write(full, object_name, CMString(compressed_data->data(), compressed_data->size()), hash_to_use);
}

void Database::freeze() {
    if (is_frozen_) {
        return;
    }
    fly::DataService::instance()->drain_write_back();
    is_frozen_ = true;
    create_frozen_marker();
    fly::DataService::instance()->on_flush(db_id_);
    fly::DataService::instance()->cleanup_temp_entries(db_id_);
    fly::WorkerAgentContext::notify_freeze(db_id_);

    // TODO: freeze 后处理 — 从聚合文件中真正删除 removed_objects_ 的数据
    // 当前聚合文件可能包含多个对象，删除单个对象需要重写整个文件
    // 完整实现需要在数据压缩(compaction)功能中完成
    if (!removed_objects_.empty()) {
        uint64_t count = removed_objects_.size();
        INFO("freeze: {} objects marked for removal (disk cleanup pending compaction implementation)",
             count);
    }
}

bool Database::is_frozen() const {
    return is_frozen_;
}

void Database::remove_object(const CMString& object_name) {
    if (check_frozen()) return;

    CMString full = full_name(object_name);
    removed_objects_.insert(full);

    fly::WorkerAgentContext::request_remove(db_id_, object_name);

    writer_->remove_entry(full);

    fly::DataService::instance()->remove_local_index(full);

    fly::ObjectCache::instance().remove(full);

    INFO("Object removed: {}", full);
}

void Database::remove_index_entry(const CMString& object_name) {
    CMString full = full_name(object_name);
    removed_objects_.insert(full);
    writer_->remove_entry(full);
    fly::ObjectCache::instance().remove(full);
    INFO("Index entry removed: {}", full);
}

DbMeta Database::load_meta() const {
    CMString meta_path = base_path_ + "/_DB_META";
    std::ifstream ifs(meta_path, std::ios::binary);
    if (!ifs.is_open()) {
        ERR("Cannot open meta file: {}", meta_path);
        return {};
    }

    int64_t header_size = 0;
    ifs.read(reinterpret_cast<char*>(&header_size), sizeof(header_size));
    if (!ifs || header_size <= 0) {
        ERR("Invalid _DB_META header size");
        return {};
    }

    CMString header_data(header_size, '\0');
    ifs.read(header_data.data(), header_size);
    if (!ifs) {
        ERR("Failed to read _DB_META header");
        return {};
    }

    DbMetaHeader header;
    FLY_DECODE(header_data, DbMetaHeader, header);

    CMVector<WorkerInfo> workers;
    while (true) {
        int64_t record_size = 0;
        ifs.read(reinterpret_cast<char*>(&record_size), sizeof(record_size));
        if (!ifs || record_size <= 0) break;

        CMString record_data(record_size, '\0');
        ifs.read(record_data.data(), record_size);
        if (!ifs) break;

        WorkerInfo info;
        try {
            FLY_DECODE(record_data, WorkerInfo, info);
            workers.push_back(std::move(info));
        } catch (...) {
            break;
        }
    }

    DbMeta meta;
    meta.db_id_ = header.db_id_;
    meta.created_at_ = header.created_at_;
    meta.workers_ = std::move(workers);
    return meta;
}

CMString Database::get_db_id() const {
    return db_id_;
}

void Database::set_db_id(const CMString& db_id) {
    auto ds = fly::DataService::instance();
    ds->unregister_database(db_id_);
    ds->register_database(db_id, base_path_, data_path_, writer_id_);
    db_id_ = db_id;
}

CMString Database::get_base_path() const {
    return base_path_;
}

CMString Database::get_data_path() const {
    return data_path_;
}

CMString Database::get_writer_id() const {
    return writer_id_;
}

CMString Database::get_obj_name(const CMString& name) const {
    return full_name(name);
}

CMString Database::full_name(const CMString& short_name) const {
    return db_id_ + ":" + short_name;
}

void Database::reset() {
    is_frozen_ = false;
    CMString frozen_marker = base_path_ + "/_FROZEN";
    if (fs::exists(frozen_marker)) {
        fs::remove(frozen_marker);
    }
}

bool Database::check_frozen() {
    if (is_frozen_) {
        ERR("Database is frozen: {}", base_path_);
        return true;
    }
    return false;
}

void Database::create_frozen_marker() {
    CMString frozen_path = base_path_ + "/_FROZEN";
    std::ofstream ofs(frozen_path);
    ofs.close();
}

void Database::write_db_meta_header() {
    CMString meta_path = base_path_ + "/_DB_META";
    if (fs::exists(meta_path)) return;  // don't overwrite

    auto now = std::chrono::system_clock::now();
    int64_t created_at = std::chrono::duration_cast<std::chrono::seconds>(
        now.time_since_epoch()).count();

    DbMetaHeader header{db_id_, created_at};
    CMString encoded;
    FLY_ENCODE(header, encoded);

    std::ofstream ofs(meta_path, std::ios::binary);
    if (!ofs.is_open()) {
        ERR("Failed to open _DB_META for writing: {}", meta_path);
        return;
    }
    int64_t size = static_cast<int64_t>(encoded.size());
    ofs.write(reinterpret_cast<const char*>(&size), sizeof(size));
    ofs.write(encoded.data(), static_cast<std::streamsize>(encoded.size()));
    ofs.close();

    DBG("Wrote _DB_META header: db_id={}, base_path={}", db_id_, base_path_);
}

void Database::append_worker_info_to_meta(const WorkerInfo& info) {
    CMString meta_path = base_path_ + "/_DB_META";
    if (!fs::exists(meta_path)) {
        ERR("_DB_META file not found, cannot append worker info: {}", meta_path);
        return;
    }

    CMString encoded;
    FLY_ENCODE(info, encoded);

    std::ofstream ofs(meta_path, std::ios::binary | std::ios::app);
    if (!ofs.is_open()) {
        ERR("Failed to open _DB_META for appending: {}", meta_path);
        return;
    }
    int64_t size = static_cast<int64_t>(encoded.size());
    ofs.write(reinterpret_cast<const char*>(&size), sizeof(size));
    ofs.write(encoded.data(), static_cast<std::streamsize>(encoded.size()));
    ofs.close();

    DBG("Appended WorkerInfo to _DB_META: worker_id={}, hostname={}",
        info.worker_id_, info.hostname_);
}

CMString Database::generate_db_id(const CMString& base_path) {
    // Format: <4 char path-hash><6 char random> = 10 char base62
    //   - path-hash (deterministic): FNV-1a 32-bit of base_path -> prefix base62 chars.
    //     Same path -> same prefix. Enables collision detection when a migrated db
    //     is loaded (its id occupies db_paths_) and a new db is then created on the
    //     original path: identical prefix makes the collision observable.
    //   - random suffix: remaining base62 chars (~35.7 bit). Re-rolled on collision retry.
    static constexpr char kBase62[] =
        "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    static constexpr uint64_t kBase = 62;
    static constexpr size_t kPrefixLen = 4;  // path-hash chars (deterministic)
    static constexpr size_t kSuffixLen = 6;  // random chars
    static_assert(kPrefixLen + kSuffixLen == 10, "prefix+suffix length mismatch");
    // Runtime guard: keep in sync with the canonical db_id_len(). (A static_assert
    // on the inline constexpr function is rejected by GCC via virtual includes.)
    assert(kPrefixLen + kSuffixLen == fly::db_id_len());

    // --- Deterministic prefix: FNV-1a 32-bit over base_path ---
    uint32_t hash = 2166136261u;
    for (char c : base_path) {
        hash ^= static_cast<uint8_t>(c);
        hash *= 16777619u;
    }
    uint32_t prefix_val = hash % (kBase * kBase * kBase * kBase);  // 62^kPrefixLen
    char prefix[kPrefixLen];
    for (int i = kPrefixLen - 1; i >= 0; --i) {
        prefix[i] = kBase62[prefix_val % kBase];
        prefix_val /= kBase;
    }

    // --- Random suffix (re-rolled on collision) ---
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dist(0, 61);

    auto ds = fly::DataService::instance();
    constexpr int kMaxRetries = 10;
    for (int attempt = 0; attempt < kMaxRetries; ++attempt) {
        char suffix[kSuffixLen];
        for (size_t i = 0; i < kSuffixLen; ++i) {
            suffix[i] = kBase62[dist(gen)];
        }
        CMString id(prefix, kPrefixLen);
        id.append(suffix, kSuffixLen);
        if (!ds->has_database(id)) {
            return id;
        }
        WARN("db_id collision on prefix '{}', retrying (attempt {}/{})", id.substr(0, kPrefixLen), attempt + 1, kMaxRetries);
    }
    // Extremely unlikely (62^kSuffixLen ~ 56 billion suffix space). Return the last candidate
    // rather than blocking construction; DataService registration will surface any
    // real conflict downstream.
    ERR("db_id collision exhausted retries for path '{}'", base_path);
    CMString fallback(prefix, kPrefixLen);
    for (size_t i = 0; i < kSuffixLen; ++i) fallback.push_back(kBase62[dist(gen)]);
    return fallback;
}

void Database::ensure_directory_exists(const CMString& path) {
    fs::create_directories(path);
}

void Database::put_temp(const CMString& object_name, const CMString& compressed_data) {
    CMString full = full_name(object_name);
    temp_store_->put(full, compressed_data);
    fly::DataService::instance()->mark_temp_entry(full, compressed_data);
}

std::pair<bool, CMString> Database::get_temp(const CMString& object_name) {
    return temp_store_->get(full_name(object_name));
}

bool Database::has_temp(const CMString& object_name) {
    return temp_store_->has(full_name(object_name));
}

void Database::remove_temp(const CMString& object_name) {
    CMString full = full_name(object_name);
    temp_store_->remove(full);
    fly::DataService::instance()->unmark_temp_entry(full);
}

void Database::mark_temp(const CMString& object_name) {
    temp_objects_.insert(full_name(object_name));
}

void Database::put_temp_data(const CMString& object_name, FlyBufferPtr compressed_data) {
    CMString full = full_name(object_name);
    DBG("[TEMP-PUT] put_temp_data: obj={}, full={}, data_size={}", object_name, full, compressed_data ? compressed_data->size() : 0);

    // Step 1: Add local idx entry (INCOMPLETE, is_temp=true)
    fly::DataService::instance()->on_temp_write_started(db_id_, full);

    // Step 2: Store temp data and mark COMPLETE — must happen BEFORE register_write.
    // register_write is synchronous (blocks for ACK). Master dispatches dependent
    // tasks immediately on receiving WriteRegister. If data isn't stored yet,
    // other workers' reads will fail.
    fly::DataService::instance()->on_temp_write(db_id_, full, compressed_data);

    // Step 3: Register with master so other workers can discover this data.
    // By now the data is readable on this worker's DataServer.
    auto [reg_error, reg_error_type] = fly::WorkerAgentContext::register_write(db_id_, object_name);
    if (reg_error_type != fly::TaskErrorType::UNKNOWN) {
        ERR("[TEMP-PUT] register_write failed for '{}': {}", object_name, reg_error);
        fly::DataService::instance()->on_write_failed(db_id_, full, reg_error);
        return;
    }

    DBG("[TEMP-PUT] put_temp_data complete: obj={}", object_name);
}


