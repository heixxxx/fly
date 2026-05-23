#include <storage/cpp/database.h>
#include <storage/cpp/compressor.h>
#include <core/cpp/config.h>
#include <log/cpp/logger.h>
#include <common/cpp/writer_id.h>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <functional>
#include <random>
#include <sstream>
#include <iomanip>

namespace fs = std::filesystem;

Database::Database(const CMString& base_path, const CMString& data_path, uint64_t worker_id, const CMString& host, const CMString& existing_db_id)
    : base_path_(base_path)
    , data_path_(data_path)
    , writer_id_(generate_writer_id())
    , db_id_(existing_db_id.empty() ? generate_db_id() : existing_db_id)
    , host_(host) {
    (void)worker_id;  // worker_id kept for API compat; writer_id_ is used for file naming

    fly::DataService::instance().register_database(db_id_, base_path_, data_path_, writer_id_);

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

    Config& config = Config::instance();
    CMString comp_type_str = config.get_str("compression_type");
    CompressionType comp_type = CompressorFactory::type_from_name(comp_type_str);
    int64_t comp_threshold = config.get_int("compression_threshold");
    int64_t stream_chunk_size = config.get_int("compression_stream_chunk_size");

    writer_ = CMMakeUnique<DataWriter>(
        base_path_, data_path_, writer_id_,
        config.get_int("aggregation_threshold"),
        config.get_int("large_file_threshold_kb") * 1024,
        config.get_int("block_size"),
        comp_type,
        comp_threshold,
        static_cast<int>(config.get_int("compression_level")),
        stream_chunk_size,
        host_
    );
    reader_ = CMMakeUnique<DataReader>(base_path_, data_path_, writer_id_);
}

Database::~Database() {
    try {
        fly::DataService::instance().drain_write_back();
        fly::DataService::instance().unregister_database(db_id_);
    } catch (...) {
    }
}

CMString Database::write_object(const CMString& object_name, const CMString& data, bool backup) {
    CMString full = full_name(object_name);
    check_frozen();

    fly::DataService::instance().on_write_started(db_id_, full);

    try {
        fly::WorkerAgentContext::register_write(db_id_, object_name);
    } catch (const std::exception& e) {
        fly::DataService::instance().on_write_failed(db_id_, full, e.what());
        throw;
    }

    auto data_ptr = CMMakeShared<CMString>(std::move(data));

    DataWriter* w = writer_.get();
    auto caller_record_func = fly::WorkerAgentContext::current_record_func();
    auto caller_record_ctx = fly::WorkerAgentContext::current_record_ctx();

    auto execute = [w, name = full, data_ptr, backup]() {
        (void)backup;
        w->write_object(name, *data_ptr, false);
        w->flush();
    };

    auto complete = [full, db_id = this->db_id_, object_name,
                     caller_record_func, caller_record_ctx, w]() {
        auto& ds = fly::DataService::instance();
        auto* entries = w->get_all_entries(full);
        if (entries) {
            ds.on_write_completed(db_id, full, *entries);
        }
        ds.on_object_flushed(full);
        if (caller_record_func) {
            caller_record_func(caller_record_ctx, db_id, object_name);
        }
    };

    fly::WriteRequest req;
    req.execute = std::move(execute);
    req.on_complete = std::move(complete);
    fly::DataService::instance().enqueue_write_back(std::move(req));

    return "";
}

CMString Database::read_object(const CMString& object_name) {
    ReadResult result = read_object_typed(object_name);
    return CMString(result.data_buffer.begin(), result.data_buffer.end());
}

CMString Database::write_object_typed(const CMString& object_name, const CMString& data,
                                        const CMString& py_name) {
    CMString full = full_name(object_name);
    check_frozen();

    fly::DataService::instance().on_write_started(db_id_, full);

    try {
        fly::WorkerAgentContext::register_write(db_id_, object_name);
    } catch (const std::exception& e) {
        fly::DataService::instance().on_write_failed(db_id_, full, e.what());
        throw;
    }

    auto data_ptr = CMMakeShared<CMString>(std::move(data));
    auto original_size = static_cast<uint64_t>(data_ptr->size());

    DataWriter* w = writer_.get();
    auto caller_record_func = fly::WorkerAgentContext::current_record_func();
    auto caller_record_ctx = fly::WorkerAgentContext::current_record_ctx();

    auto execute = [w, name = full, original_size, py = py_name, data_ptr]() {
        w->write_typed_object(name, original_size, py,
            data_ptr->data(), static_cast<int64_t>(data_ptr->size()));
        w->flush();
    };

    auto complete = [full, db_id = this->db_id_, object_name,
                     caller_record_func, caller_record_ctx, w]() {
        auto& ds = fly::DataService::instance();
        auto* entries = w->get_all_entries(full);
        if (entries) {
            ds.on_write_completed(db_id, full, *entries);
        }
        ds.on_object_flushed(full);
        if (caller_record_func) {
            caller_record_func(caller_record_ctx, db_id, object_name);
        }
    };

    fly::WriteRequest req;
    req.execute = std::move(execute);
    req.on_complete = std::move(complete);
    fly::DataService::instance().enqueue_write_back(std::move(req));

    return "";
}

ReadResult Database::read_object_typed(const CMString& object_name) {
    CMString full = full_name(object_name);
    auto& ds = fly::DataService::instance();
    auto [found, result] = ds.try_read_local(full);
    if (found) {
        return result;
    }
    throw std::runtime_error("Object not found locally: " + full);
}

void Database::freeze() {
    if (is_frozen_) {
        return;
    }
    fly::DataService::instance().drain_write_back();
    is_frozen_ = true;
    writer_->close();
    create_frozen_marker();
    fly::DataService::instance().on_flush(db_id_);
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
    check_frozen();

    CMString full = full_name(object_name);
    removed_objects_.insert(full);

    writer_->remove_entry(full);

    fly::DataService::instance().remove_local_index(full);

    fly::WorkerAgentContext::notify_object_removed(db_id_, object_name);

    INFO("Object removed: {}", full);
}

DbMeta Database::load_meta() const {
    CMString meta_path = base_path_ + "/_DB_META";
    std::ifstream ifs(meta_path, std::ios::binary);
    if (!ifs.is_open()) {
        throw std::runtime_error("Cannot open meta file: " + meta_path);
    }

    int64_t header_size = 0;
    ifs.read(reinterpret_cast<char*>(&header_size), sizeof(header_size));
    if (!ifs || header_size <= 0) {
        throw std::runtime_error("Invalid _DB_META header size");
    }

    CMString header_data(header_size, '\0');
    ifs.read(header_data.data(), header_size);
    if (!ifs) {
        throw std::runtime_error("Failed to read _DB_META header");
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
    meta.db_id = header.db_id;
    meta.created_at = header.created_at;
    meta.workers = std::move(workers);
    return meta;
}

CMString Database::get_db_id() const {
    return db_id_;
}

void Database::set_db_id(const CMString& db_id) {
    auto& ds = fly::DataService::instance();
    ds.unregister_database(db_id_);
    ds.register_database(db_id, base_path_, data_path_, writer_id_);
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

void Database::check_frozen() {
    if (is_frozen_) {
        throw std::runtime_error("Database is frozen: " + base_path_);
    }
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
        info.worker_id, info.hostname);
}

CMString Database::generate_db_id() {
    // UUID v4 (random), 32 hex chars without hyphens
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> dist;

    uint32_t parts[4] = {dist(gen), dist(gen), dist(gen), dist(gen)};
    // Set version (4) and variant (10xx)
    parts[2] = (parts[2] & 0x0FFFFFFFu) | 0x40000000u;
    parts[3] = (parts[3] & 0x3FFFFFFFu) | 0x80000000u;

    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    ss << std::setw(8) << parts[0]
       << std::setw(8) << parts[1]
       << std::setw(8) << parts[2]
       << std::setw(8) << parts[3];
    return ss.str();
}

void Database::ensure_directory_exists(const CMString& path) {
    fs::create_directories(path);
}


