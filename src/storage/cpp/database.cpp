#include <storage/cpp/database.h>
#include <storage/cpp/compressor.h>
#include <serialization/cpp/object_header.h>
#include <serialization/cpp/serialization_macros.h>
#include <storage/cpp/object_cache.h>
#include <storage/cpp/decompressing_streambuf.h>
#include <core/cpp/config.h>
#include <log/cpp/logger.h>
#include <common/cpp/writer_id.h>
#include <common/cpp/worker_context.h>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <functional>
#include <random>
#include <istream>
#include <cassert>

namespace fs = std::filesystem;

Database::Database(const CMString& db_path, const CMString& data_path, uint64_t worker_id, const CMString& host, const CMString& existing_db_path)
    : db_path_(db_path)
    , data_path_(data_path)
    , writer_id_(generate_writer_id())
    , host_(host) {
    (void)worker_id;  // worker_id kept for API compat; writer_id_ is used for file naming
    (void)existing_db_path;  // 废弃参数（值被忽略）

    // 校验 db_path 不含 ':' —— full_name = "db_path:short" 用 ':' 分隔，
    // db_path 含 ':' 会导致 split 歧义。源头拒绝，双保险。
    if (db_path_.find(':') != CMString::npos) {
        ERR("Database db_path must not contain ':' (would break full_name split): '{}'", db_path_);
        db_path_.clear();
        return;
    }

    // 迁移跟随：_MIGRATED_TO 机制已废弃（db chain 取代）。
    // resolve_migrated_path 现在对新 db 总是返回原始 path（_MIGRATED_TO 不再写入）。
    // 旧 db 如有 _MIGRATED_TO 残留仍可跟随（兼容），但新 merge 不再产生此文件。
    CMString resolved = fly::DataService::instance()->resolve_migrated_path(db_path_);
    if (resolved != db_path_) {
        CMString target_data = fly::DataService::instance()->read_migrated_data_path(db_path_);
        INFO("Database migrated: db_path '{}' -> '{}', data_path '{}' -> '{}'",
             db_path_, resolved, data_path_, target_data);
        db_path_ = resolved;
        if (!target_data.empty()) {
            data_path_ = target_data;
        }
    }

    fly::DataService::instance()->register_database(db_path_, data_path_, writer_id_);

    ensure_directory_exists(db_path_);
    if (!data_path_.empty()) {
        ensure_directory_exists(data_path_);
    }

    // 仅当 _DB_META 不存在时才写（新建 db）；load/merge 场景 _DB_META 已存在则跳过。
    if (!fs::exists(db_path_ + "/_DB_META")) {
        write_db_meta_header();
    }

    CMString frozen_marker = db_path_ + "/_FROZEN";
    if (fs::exists(frozen_marker)) {
        is_frozen_ = true;
    }

    // Restore vars from a prior freeze (_VARS file) if present. Applies to both
    // fresh opens and load_db scenarios — absence of the file is a no-op.
    load_vars_from_disk();

    auto config = Config::instance();
    CMString comp_type_str = config->get_str("compression_type");
    compression_type_ = CompressorFactory::type_from_name(comp_type_str);
    compression_level_ = static_cast<int>(config->get_int("compression_level"));
    serialize_chunk_size_ = config->get_int("serialize_chunk_size");
    compression_threshold_ = config->get_int("compression_threshold");

    writer_ = CMMakeUnique<DataWriter>(
        db_path_, data_path_, writer_id_,
        config->get_int("aggregation_threshold"),
        host_
    );
}

Database::~Database() {
    fly::DataService::instance()->drain_write_back();
    fly::DataService::instance()->cleanup_temp_entries(db_path_);
    fly::DataService::instance()->unregister_database(db_path_);
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
            ? CompressorFactory::create(compression_type_, compression_level_) : nullptr;
        CompressingStreamBuf csbuf(counting_stream, std::move(compressor),
                                    serialize_chunk_size_, compression_threshold_);
        std::ostream os(&csbuf);
        os.write(data, static_cast<std::streamsize>(data_size));
        os.flush();
        total_uncompressed = csbuf.total_uncompressed();
        chunk_count = csbuf.chunk_count();
        // Small payloads skip compression internally; record the actual format
        // so the read-side picks the matching (de)compressor path.
        header.compression_type_ = static_cast<uint8_t>(csbuf.effective_compression_type());
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
    fly::DataService::instance()->on_write_started(db_path_, full);
    int64_t compressed_size = static_cast<int64_t>(record->size());
    auto [reg_error, reg_error_type] = fly::WorkerAgentContext::register_write(db_path_, object_name, compressed_size);

    if (reg_error_type == fly::TaskErrorType::WRITE_DUPLICATE_SKIPPED) {
        fly::ObjectCache::instance().remove(full);
        fly::DataService::instance()->on_write_failed(db_path_, full, "duplicate skipped");
        fly::WorkerAgentContext::set_last_error_type(fly::TaskErrorType::WRITE_DUPLICATE_SKIPPED);
        return fly::WriteErrorType::DUPLICATE_SKIPPED;
    }
    if (reg_error_type == fly::TaskErrorType::WRITE_PROVENANCE_MISMATCH) {
        fly::ObjectCache::instance().remove(full);
        fly::DataService::instance()->on_write_failed(db_path_, full, reg_error);
        ERR("Write registration failed: {} (type={})", reg_error, static_cast<int>(reg_error_type));
        return fly::WriteErrorType::REGISTRATION_FAILED;
    }
    if (reg_error_type == fly::TaskErrorType::WRITE_REGISTRATION_TIMEOUT) {
        fly::ObjectCache::instance().remove(full);
        fly::DataService::instance()->on_write_failed(db_path_, full, reg_error);
        ERR("Write registration timeout: {}", reg_error);
        return fly::WriteErrorType::REGISTRATION_TIMEOUT;
    }

    // 3. Registration succeeded — enqueue background disk write.
    DataWriter* w = writer_.get();
    auto caller_record_func = fly::WorkerAgentContext::current_record_func();
    auto caller_backup_func = backup ? fly::WorkerAgentContext::current_backup_func() : std::function<void(const fly::CMString&, const fly::CMString&)>{};
    CMString write_hash = fly::WorkerAgentContext::get_current_write_hash();

    // 仅 worker task 上下文打 BEGIN（transaction_mode 激活时）。master 直接
    // write_object 时 transaction_mode 未激活，其 ADD 为段外隐式事务，load 时
    // 直接生效。mark_begin 记录 data 偏移回滚点，后续 mark_end 提交 /
    // abort_segment 回滚。WBQ 单线程串行，segment_active 判断与设置无竞态。
    bool in_task_context = fly::WorkerAgentContext::is_transaction_mode();
    // LocalIndex 是 per-(db,writer) 的，idx 文件天然属于本 db，entry 只需存 short_name
    // （前缀在同文件内 100% 冗余）。write_record/get_all_entries 传 short_name；
    // DataService 层（on_write_completed/on_object_flushed）仍用 full_name 作 key。
    auto execute = [w, short_name = object_name, original_size, chunk_count, record, write_hash, in_task_context]() {
        if (in_task_context && !w->segment_active()) {
            w->mark_begin();
        }
        w->write_record(short_name, original_size, chunk_count, *record, write_hash);
        w->flush();
    };

    auto complete = [full, db_path = this->db_path_, object_name,
                     caller_record_func, caller_backup_func, w, backup, compressed_size]() {
        auto ds = fly::DataService::instance();
        auto entries = w->get_all_entries(object_name);
        if (entries.has_value()) {
            ds->on_write_completed(db_path, full, entries.value());
        }
        ds->on_object_flushed(full);
        if (caller_record_func) {
            caller_record_func(db_path, object_name, compressed_size);
        }
        if (backup && caller_backup_func) {
            caller_backup_func(db_path, object_name);
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

fly::WriteErrorType Database::commit_stream(const CMString& object_name,
                                     FlyBufferPtr record,
                                     const CMString& py_name, bool backup) {
    CMString full = full_name(object_name);
    if (check_frozen()) { fly::WorkerAgentContext::set_last_error_type(fly::TaskErrorType::WRITE_TO_FROZEN_DB); return fly::WriteErrorType::FROZEN_DB; }

    // The incoming FlyBufferPtr may carry a nanobind py_deleter (if it crossed
    // the Python↔C++ boundary). commit_write stores it in ObjectCache, which is
    // destructed from non-Python threads (e.g. reactor thread's remove_local_index).
    // A py_deleter would try to acquire the GIL on those threads → deadlock if
    // the task executor thread holds the GIL. Create a pure-C++ copy to strip
    // the nanobind deleter.
    auto pure_record = CMMakeShared<FlyBuffer>();
    pure_record->write(record->data(), record->size());

    int64_t original_size = 0;
    int32_t chunk_count = 0;
    try {
        int64_t off = 0;
        ObjectHeader hdr = ObjectHeader::deserialize({pure_record->data(), pure_record->size()}, off);
        original_size = static_cast<int64_t>(hdr.total_size_);
        chunk_count = static_cast<int32_t>(hdr.chunk_count_);
    } catch (...) {}

    return commit_write(object_name, full, pure_record, original_size, chunk_count, backup);
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
    ds->on_write_started(db_path_, full);

    auto saved_hash = fly::WorkerAgentContext::get_current_write_hash();
    fly::WorkerAgentContext::clear_current_write_hash();

    int64_t backup_compressed_size = static_cast<int64_t>(compressed_data.size());
    auto [reg_error, reg_error_type] = fly::WorkerAgentContext::register_write(db_path_, object_name, backup_compressed_size);
    if (reg_error_type != fly::TaskErrorType::UNKNOWN) {
        ds->on_write_failed(db_path_, full, reg_error);
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

    auto execute = [w, short_name = object_name, header, record, backup_hash]() {
        w->write_record(short_name, header.total_size_, header.chunk_count_, *record, backup_hash);
        w->flush();
    };

    auto complete = [full, db_path = db_path_, object_name,
                     caller_record_func, w, saved_hash, backup_compressed_size]() {
        fly::WorkerAgentContext::set_current_write_hash(saved_hash);
        auto dsvc = fly::DataService::instance();
        auto entries = w->get_all_entries(object_name);
        if (entries.has_value()) {
            dsvc->on_write_completed(db_path, full, entries.value());
        }
        dsvc->on_object_flushed(full);
        if (caller_record_func) {
            caller_record_func(db_path, object_name, backup_compressed_size);
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
    fly::DataService::instance()->on_flush(db_path_);
    fly::DataService::instance()->cleanup_temp_entries(db_path_);
    fly::WorkerAgentContext::notify_freeze(db_path_);

    // Persist non-deleted vars alongside the frozen db.
    flush_vars_to_disk();

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

    fly::WorkerAgentContext::request_remove(db_path_, object_name);

    // LocalIndex 只存 short_name（idx 文件天然属于本 db）。
    writer_->remove_entry(object_name);

    fly::DataService::instance()->remove_local_index(full);

    fly::ObjectCache::instance().remove(full);

    INFO("Object removed: {}", full);
}

void Database::remove_index_entry(const CMString& object_name) {
    CMString full = full_name(object_name);
    removed_objects_.insert(full);
    writer_->remove_entry(object_name);
    fly::ObjectCache::instance().remove(full);
    INFO("Index entry removed: {}", full);
}

void Database::mark_write_begin() {
    writer_->mark_begin();
}

void Database::mark_write_end() {
    writer_->mark_end();
}

void Database::abort_task_writes(const CMVector<CMString>& dirty_full_names) {
    // 1. 先清空 WBQ 中尚未开始的脏写请求（比 drain 更高效，避免先落盘再删）。
    fly::DataService::instance()->clear_write_back();
    // 2. drain 正在执行的那个写（已 pop 出 queue，clear_pending 丢不掉它）：
    //    等它自然完成，保证 truncate 时文件状态确定。它落盘的脏字节会被
    //    下一步 truncate 回收，on_complete_ 更新的 local_idx_ 会在第 4 步清理。
    fly::DataService::instance()->drain_write_back();

    // 3. idx 打 ABORT + data 文件 truncate 回滚点。
    writer_->abort_segment();

    // 4. 清运行时内存中被本 task 污染的对象。
    //    （clear_pending 跳过了未开始请求的 on_complete_，drain 完成的那个
    //    请求的 on_complete_ 更新了 local_idx_；ABORT 标记只在 load 时丢弃
    //    pending，运行时内存需主动清。）
    for (const auto& full : dirty_full_names) {
        fly::DataService::instance()->remove_local_index(full);
        fly::ObjectCache::instance().remove(full);
    }

    INFO("Task writes aborted: {} dirty objects, db_path={}", dirty_full_names.size(), db_path_);
}

DbMeta Database::load_meta() const {
    return load_meta_from_path(db_path_);
}

DbMeta Database::load_meta_from_path(const CMString& db_path) {
    CMString meta_path = db_path + "/_DB_META";
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
    meta.created_at_ = header.created_at_;
    meta.workers_ = std::move(workers);
    return meta;
}

CMString Database::get_db_path() const {
    return db_path_;
}

CMString Database::get_data_path() const {
    return data_path_;
}

void Database::set_paths(const CMString& db_path, const CMString& data_path) {
    // Merge 产物落到新路径：更新 db_path_/data_path_ + re-register 进 DataService。
    // db_path_ 是 Database 唯一路径标识，merge 后所有句柄（底层共享同一 C++ 对象）
    // 的 db_path_ 同时变为 target，full_name 自然产生 target path 前缀。
    // merge 的 wait_for_all_tasks 前提保证 merge 时无 pending task，不需要旧 path 锚点。
    auto ds = fly::DataService::instance();
    // unregister 旧 db_path（merge 前的源 path），再注册新的。
    ds->unregister_database(db_path_);
    db_path_ = db_path;
    data_path_ = data_path;
    ds->register_database(db_path_, data_path_, writer_id_);
}

CMString Database::get_writer_id() const {
    return writer_id_;
}

CMString Database::get_full_name(const CMString& name) const {
    return full_name(name);
}

CMString Database::full_name(const CMString& short_name) const {
    return db_path_ + ":" + short_name;
}

void Database::reset() {
    is_frozen_ = false;
    CMString frozen_marker = db_path_ + "/_FROZEN";
    if (fs::exists(frozen_marker)) {
        fs::remove(frozen_marker);
    }
}

bool Database::check_frozen() {
    if (is_frozen_) {
        ERR("Database is frozen: {}", db_path_);
        return true;
    }
    return false;
}

void Database::create_frozen_marker() {
    CMString frozen_path = db_path_ + "/_FROZEN";
    std::ofstream ofs(frozen_path);
    ofs.close();
}

void Database::write_db_meta_header() {
    CMString meta_path = db_path_ + "/_DB_META";
    if (fs::exists(meta_path)) return;  // don't overwrite

    auto now = std::chrono::system_clock::now();
    int64_t created_at = std::chrono::duration_cast<std::chrono::seconds>(
        now.time_since_epoch()).count();

    DbMetaHeader header{created_at};
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

    DBG("Wrote _DB_META header: db_path={}", db_path_);
}

void Database::append_worker_info_to_meta(const WorkerInfo& info) {
    CMString meta_path = db_path_ + "/_DB_META";
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

size_t Database::worker_info_count() const {
    // 读 _DB_META 文件，统计已登记的 WorkerInfo 记录数（跳过 header）。
    CMString meta_path = db_path_ + "/_DB_META";
    if (!fs::exists(meta_path)) return 0;

    std::ifstream ifs(meta_path, std::ios::binary);
    if (!ifs.is_open()) return 0;

    int64_t header_size = 0;
    ifs.read(reinterpret_cast<char*>(&header_size), sizeof(header_size));
    if (!ifs || header_size <= 0) return 0;
    ifs.ignore(header_size);   // 跳过 header
    if (!ifs) return 0;

    size_t count = 0;
    while (true) {
        int64_t record_size = 0;
        ifs.read(reinterpret_cast<char*>(&record_size), sizeof(record_size));
        if (!ifs || record_size <= 0) break;
        CMString record_data(record_size, '\0');
        ifs.read(record_data.data(), record_size);
        if (!ifs) break;
        count++;
    }
    return count;
}

void Database::ensure_directory_exists(const CMString& path) {
    fs::create_directories(path);
}

void Database::mark_temp(const CMString& object_name) {
    temp_objects_.insert(full_name(object_name));
}

void Database::put_temp_data(const CMString& object_name, FlyBufferPtr compressed_data) {
    CMString full = full_name(object_name);
    DBG("[TEMP-PUT] put_temp_data: obj={}, full={}, data_size={}", object_name, full, compressed_data ? compressed_data->size() : 0);

    // Step 1: Add local idx entry (INCOMPLETE, is_temp=true)
    fly::DataService::instance()->on_temp_write_started(db_path_, full);

    // Step 2: Store temp data and mark COMPLETE — must happen BEFORE register_write.
    // register_write is synchronous (blocks for ACK). Master dispatches dependent
    // tasks immediately on receiving WriteRegister. If data isn't stored yet,
    // other workers' reads will fail.
    fly::DataService::instance()->on_temp_write(db_path_, full, compressed_data);

    // Step 3: Register with master so other workers can discover this data.
    // By now the data is readable on this worker's DataServer.
    int64_t temp_compressed_size = static_cast<int64_t>(compressed_data->size());
    auto [reg_error, reg_error_type] = fly::WorkerAgentContext::register_write(db_path_, object_name, temp_compressed_size);
    if (reg_error_type != fly::TaskErrorType::UNKNOWN) {
        ERR("[TEMP-PUT] register_write failed for '{}': {}", object_name, reg_error);
        fly::DataService::instance()->on_write_failed(db_path_, full, reg_error);
        return;
    }

    DBG("[TEMP-PUT] put_temp_data complete: obj={}", object_name);
}

// =============================================================================
// Var service implementation
// =============================================================================

// _VARS file magic header ("VARS" in little-endian int64).
static constexpr int64_t VARS_FILE_MAGIC = 0x53524156;  // 'V','A','R','S' LE

bool Database::set_var(const CMString& var_name, FlyBufferPtr value, const CMString& type_name) {
    // var is db data: freeze makes it immutable too (same gate as write_object).
    if (check_frozen()) {
        ERR("set_var rejected: db {} is frozen", db_path_);
        return false;
    }
    // Size warning: var is for small objects; >1K serialized is too large.
    if (value && value->size() > 1024) {
        WARN("var '{}' serialized size {} > 1K, too large for var (consider write_object)",
             var_name, value->size());
    }

    // var_store_ keys on the short name; the context receives the full name.
    CMString full = full_name(var_name);
    bool ok = fly::WorkerAgentContext::set_var(full, value, type_name);
    if (ok) {
        // Master accepted — cache locally (zero-copy shared FlyBufferPtr).
        std::lock_guard<std::mutex> lk(var_mutex_);
        var_store_[var_name] = VarEntry{value, type_name};
    }
    // On rejection (frozen / immutable) leave the local cache untouched: the
    // existing entry (if any) is the authoritative master value.
    return ok;
}

std::tuple<bool, FlyBufferPtr, CMString> Database::get_var(const CMString& var_name) {
    // Local cache first (zero-copy).
    {
        std::lock_guard<std::mutex> lk(var_mutex_);
        auto it = var_store_.find(var_name);
        if (it != var_store_.end()) {
            return {true, it->second.value_, it->second.type_name_};
        }
    }
    // Miss → ask master (synchronous) via the full name.
    CMString full = full_name(var_name);
    auto [success, value, type_name] = fly::WorkerAgentContext::get_var(full);
    if (success && value) {
        // Cache the fetched value locally (zero-copy shared).
        std::lock_guard<std::mutex> lk(var_mutex_);
        var_store_[var_name] = VarEntry{value, type_name};
    }
    return {success, value, type_name};
}

void Database::remove_var(const CMString& var_name) {
    // Clear local cache immediately; master notified asynchronously via full name.
    {
        std::lock_guard<std::mutex> lk(var_mutex_);
        var_store_.erase(var_name);
    }
    CMString full = full_name(var_name);
    fly::WorkerAgentContext::remove_var(full);
}

void Database::inject_var(const CMString& var_name, FlyBufferPtr value, const CMString& type_name) {
    // Worker-side local cache population from TaskAssignMessage var_payloads.
    // Overwrites any existing local entry (master authoritative).
    std::lock_guard<std::mutex> lk(var_mutex_);
    var_store_[var_name] = VarEntry{value, type_name};
}

void Database::drop_local_var(const CMString& var_name) {
    std::lock_guard<std::mutex> lk(var_mutex_);
    var_store_.erase(var_name);
}

// ---- Master authoritative operations (no network) ----

bool Database::master_set_var(const CMString& var_name, FlyBufferPtr value, const CMString& type_name) {
    if (check_frozen()) {
        ERR("master_set_var rejected: db {} is frozen", db_path_);
        return false;
    }
    std::lock_guard<std::mutex> lk(var_mutex_);
    if (var_store_.count(var_name)) {
        // Immutable: a var may only be written once.
        ERR("master_set_var rejected: var '{}' already exists (immutable)", var_name);
        return false;
    }
    var_store_[var_name] = VarEntry{value, type_name};
    return true;
}

std::tuple<bool, FlyBufferPtr, CMString> Database::master_get_var(const CMString& var_name) {
    std::lock_guard<std::mutex> lk(var_mutex_);
    auto it = var_store_.find(var_name);
    if (it == var_store_.end()) {
        return {false, nullptr, ""};
    }
    return {true, it->second.value_, it->second.type_name_};
}

void Database::master_remove_var(const CMString& var_name) {
    std::lock_guard<std::mutex> lk(var_mutex_);
    if (var_name.empty()) {
        var_store_.clear();
    } else {
        var_store_.erase(var_name);
    }
}

bool Database::master_has_var(const CMString& var_name) {
    std::lock_guard<std::mutex> lk(var_mutex_);
    return var_store_.count(var_name) > 0;
}

// ---- Persistence (_VARS file) ----

void Database::flush_vars_to_disk() {
    // Snapshot shared FlyBufferPtrs under lock (zero-copy — shared_ptr refcount,
    // no byte copy), write outside lock to avoid blocking readers.
    CMVector<std::tuple<CMString, FlyBufferPtr, CMString>> snapshot;  // (name, buf, type_name)
    {
        std::lock_guard<std::mutex> lk(var_mutex_);
        snapshot.reserve(var_store_.size());
        for (const auto& [name, entry] : var_store_) {
            snapshot.emplace_back(name, entry.value_, entry.type_name_);
        }
    }

    CMString vars_path = db_path_ + "/_VARS";
    std::ofstream ofs(vars_path, std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) {
        ERR("flush_vars_to_disk: cannot open {}", vars_path);
        return;
    }

    int64_t magic = VARS_FILE_MAGIC;
    int64_t count = static_cast<int64_t>(snapshot.size());
    ofs.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
    ofs.write(reinterpret_cast<const char*>(&count), sizeof(count));

    for (const auto& [name, buf, type_name] : snapshot) {
        int64_t name_len = static_cast<int64_t>(name.size());
        ofs.write(reinterpret_cast<const char*>(&name_len), sizeof(name_len));
        ofs.write(name.data(), name_len);

        // Read directly from the shared FlyBuffer — no copy.
        int64_t val_len = buf ? static_cast<int64_t>(buf->size()) : 0;
        ofs.write(reinterpret_cast<const char*>(&val_len), sizeof(val_len));
        if (val_len > 0) {
            ofs.write(buf->data(), val_len);
        }

        int64_t tn_len = static_cast<int64_t>(type_name.size());
        ofs.write(reinterpret_cast<const char*>(&tn_len), sizeof(tn_len));
        ofs.write(type_name.data(), tn_len);
    }
    ofs.flush();
    DBG("flush_vars_to_disk: wrote {} vars to {}", count, vars_path);
}

void Database::load_vars_from_disk() {
    CMString vars_path = db_path_ + "/_VARS";
    if (!fs::exists(vars_path)) {
        return;  // No _VARS file — nothing to load.
    }

    std::ifstream ifs(vars_path, std::ios::binary);
    if (!ifs.is_open()) {
        ERR("load_vars_from_disk: cannot open {}", vars_path);
        return;
    }

    int64_t magic = 0;
    ifs.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    if (!ifs || magic != VARS_FILE_MAGIC) {
        ERR("load_vars_from_disk: bad magic in {}", vars_path);
        return;
    }

    int64_t count = 0;
    ifs.read(reinterpret_cast<char*>(&count), sizeof(count));
    if (!ifs || count < 0) {
        ERR("load_vars_from_disk: bad count in {}", vars_path);
        return;
    }

    std::lock_guard<std::mutex> lk(var_mutex_);
    for (int64_t i = 0; i < count; ++i) {
        int64_t name_len = 0;
        ifs.read(reinterpret_cast<char*>(&name_len), sizeof(name_len));
        if (!ifs || name_len < 0) break;
        CMString name(static_cast<size_t>(name_len), '\0');
        ifs.read(name.data(), name_len);

        int64_t val_len = 0;
        ifs.read(reinterpret_cast<char*>(&val_len), sizeof(val_len));
        if (!ifs || val_len < 0) break;
        CMString value_bytes(static_cast<size_t>(val_len), '\0');
        ifs.read(value_bytes.data(), val_len);

        int64_t tn_len = 0;
        ifs.read(reinterpret_cast<char*>(&tn_len), sizeof(tn_len));
        if (!ifs || tn_len < 0) break;
        CMString type_name(static_cast<size_t>(tn_len), '\0');
        ifs.read(type_name.data(), tn_len);

        // Zero-copy into a fresh FlyBufferPtr (take moves the CMString).
        auto buf = CMMakeShared<FlyBuffer>();
        buf->take(std::move(value_bytes));
        var_store_[std::move(name)] = VarEntry{buf, std::move(type_name)};
    }
    DBG("load_vars_from_disk: loaded {} vars from {}", count, vars_path);
}


