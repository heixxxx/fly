#include <storage/cpp/data_service.h>
#include <storage/cpp/data_reader.h>
#include <storage/cpp/temp_store.h>
#include <storage/cpp/compressor.h>
#include <storage/cpp/compression_utils.h>
#include <storage/cpp/decompressing_streambuf.h>
#include <serialization/cpp/object_header.h>
#include <log/cpp/logger.h>
#include <chrono>
#include <algorithm>
#include <utility>
#include <cstring>

namespace fly {

namespace {

constexpr size_t DB_ID_LEN = 32;

}  // namespace

struct DataService::Creator_ : public DataService {
    Creator_() = default;
};

CMSharedPtr<DataService> DataService::instance_ptr() {
    static CMSharedPtr<DataService> inst = CMMakeShared<Creator_>();
    return inst;
}

DataService& DataService::instance() {
    return *instance_ptr();
}

namespace {

ReadResult decompress_raw(const CMString& raw) {
    ReadResult result;
    DecompressingStreamBuf dsbuf(raw.data(), raw.size());
    result.py_name = dsbuf.py_name();
    std::istream is(&dsbuf);
    CMVector<char> tmp(4096);
    while (is) {
        is.read(tmp.data(), static_cast<std::streamsize>(tmp.size()));
        if (is.gcount() > 0) {
            result.data_buffer.insert(result.data_buffer.end(),
                tmp.data(), tmp.data() + static_cast<size_t>(is.gcount()));
        }
    }
    return result;
}

}  // namespace

DataService::~DataService() {
    if (write_back_queue_) {
        write_back_queue_->drain();
        write_back_queue_->stop();
    }
}

// ============================================================
// Lifecycle
// ============================================================

void DataService::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    stop_transfer_server();
    drain_write_back();
    stop_write_back();
    local_idx_.clear();
    remote_idx_.clear();
    worker_registry_.clear();
    db_paths_.clear();
    remote_compressed_read_handler_ = nullptr;
    direct_compressed_read_handler_ = nullptr;
}

// ============================================================
// Database Registry
// ============================================================

void DataService::register_database(const CMString& db_id,
                                     const CMString& base_path,
                                     const CMString& data_path,
                                     const CMString& writer_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = db_paths_.find(db_id);
    if (it != db_paths_.end()) {
        it->second = {base_path, data_path, writer_id};
        return;
    }
    for (const auto& [existing_id, paths] : db_paths_) {
        if (paths.base_path == base_path) {
            ERR("base_path '{}' already registered by database '{}'", base_path, existing_id);
            return;
        }
    }
    db_paths_[db_id] = {base_path, data_path, writer_id};
}

void DataService::unregister_database(const CMString& db_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    db_paths_.erase(db_id);
}

bool DataService::has_database(const CMString& db_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return db_paths_.find(db_id) != db_paths_.end();
}

// ============================================================
// Local Index Management
// ============================================================

void DataService::on_object_written(const CMString& db_id,
                                     const CMString& object_name,
                                     const IndexEntry& entry) {
    auto [_, short_name] = split_full(object_name);
    std::lock_guard<std::mutex> lock(mutex_);
    auto& info = local_idx_[db_id][short_name];
    if (!info) {
        info = CMMakeShared<LocalObjectInfo>();
    }
    info->db_id = db_id;
    info->entries.push_back(entry);
    info->flushed = false;
    info->completion_state = CompletionState::COMPLETE;
}

void DataService::on_flush(const CMString& db_id) {
    CMVector<CMSharedPtr<LocalObjectInfo>> to_notify;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto db_it = local_idx_.find(db_id);
        if (db_it == local_idx_.end()) return;
        for (auto& [name, info] : db_it->second) {
            if (info) {
                info->flushed = true;
                to_notify.push_back(info);
            }
        }
    }
    for (auto& info : to_notify) {
        info->cv.notify_all();
    }
}

void DataService::on_write_started(const CMString& db_id,
                                     const CMString& object_name) {
    auto [_, short_name] = split_full(object_name);
    CMSharedPtr<LocalObjectInfo> info = CMMakeShared<LocalObjectInfo>();
    info->db_id = db_id;
    info->completion_state = CompletionState::INCOMPLETE;

    std::lock_guard<std::mutex> lock(mutex_);
    local_idx_[db_id][short_name] = info;
}

void DataService::on_write_completed(const CMString& db_id,
                                      const CMString& object_name,
                                      const CMVector<IndexEntry>& entries) {
    auto [_, short_name] = split_full(object_name);
    CMSharedPtr<LocalObjectInfo> info;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto db_it = local_idx_.find(db_id);
        if (db_it == local_idx_.end()) return;
        auto it = db_it->second.find(short_name);
        if (it == db_it->second.end() || !it->second) return;
        info = it->second;
        info->entries = entries;
        info->completion_state = CompletionState::COMPLETE;
    }
    {
        std::lock_guard<std::mutex> cv_lock(info->cv_mutex);
    }
    info->cv.notify_all();
}

void DataService::on_write_failed(const CMString& db_id,
                                    const CMString& object_name,
                                    const CMString& error_message) {
    auto [_, short_name] = split_full(object_name);
    CMSharedPtr<LocalObjectInfo> info;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto db_it = local_idx_.find(db_id);
        if (db_it == local_idx_.end()) return;
        auto it = db_it->second.find(short_name);
        if (it == db_it->second.end() || !it->second) return;
        info = it->second;
        info->completion_state = CompletionState::FAILED;
        info->error_message = error_message;
        db_it->second.erase(it);
    }
    info->cv.notify_all();
}

void DataService::remove_local_index(const CMString& object_name) {
    auto [db_id, short_name] = split_full(object_name);
    std::lock_guard<std::mutex> lock(mutex_);
    auto db_it = local_idx_.find(db_id);
    if (db_it != local_idx_.end()) {
        db_it->second.erase(short_name);
    }
}

bool DataService::has_local_object(const CMString& object_name) const {
    auto [db_id, short_name] = split_full(object_name);
    std::lock_guard<std::mutex> lock(mutex_);
    auto db_it = local_idx_.find(db_id);
    if (db_it == local_idx_.end()) return false;
    auto it = db_it->second.find(short_name);
    return it != db_it->second.end() && it->second &&
           it->second->completion_state == CompletionState::COMPLETE && it->second->flushed;
}

void DataService::on_object_flushed(const CMString& object_name) {
    auto [db_id, short_name] = split_full(object_name);
    CMSharedPtr<LocalObjectInfo> info;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto db_it = local_idx_.find(db_id);
        if (db_it != local_idx_.end()) {
            auto it = db_it->second.find(short_name);
            if (it != db_it->second.end() && it->second) {
                it->second->flushed = true;
                info = it->second;
            }
        }
    }
    if (info) {
        {
            std::lock_guard<std::mutex> cv_lock(info->cv_mutex);
        }
        info->cv.notify_all();
    }
}

void DataService::restore_entries(const CMString& db_id,
                                    const CMVector<IndexEntry>& entries) {
    CMUnorderedMap<CMString, CMVector<IndexEntry>> grouped;
    for (const auto& e : entries) {
        auto [entry_db_id, short_name] = split_full(e.object_name);
        const CMString& key = entry_db_id.empty() ? e.object_name : short_name;
        grouped[key].push_back(e);
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto& db_map = local_idx_[db_id];
    for (auto& [short_name, obj_entries] : grouped) {
        auto& info = db_map[short_name];
        if (!info) {
            info = CMMakeShared<LocalObjectInfo>();
        }
        info->db_id = db_id;
        for (auto& e : obj_entries) {
            info->entries.push_back(std::move(e));
        }
        info->completion_state = CompletionState::COMPLETE;
        info->flushed = true;
    }

    if (!grouped.empty()) {
        DBG("restore_entries: restored {} objects for db_id={}", grouped.size(), db_id);
    }
}

std::optional<CMVector<IndexEntry>> DataService::find_local_entries(const CMString& object_name) const {
    auto [db_id, short_name] = split_full(object_name);
    std::lock_guard<std::mutex> lock(mutex_);
    auto db_it = local_idx_.find(db_id);
    if (db_it == local_idx_.end()) return std::nullopt;
    auto it = db_it->second.find(short_name);
    if (it == db_it->second.end() || !it->second) return std::nullopt;
    return it->second->entries;
}

// ============================================================
// Remote Index Management
// ============================================================

void DataService::update_remote_idx(const CMString& object_name,
                                      uint64_t worker_id,
                                      const CMString& host,
                                      int32_t port) {
    register_worker(worker_id, host, port);
    add_remote_location(object_name, worker_id);
}

void DataService::add_remote_location(const CMString& object_name, uint64_t worker_id) {
    auto [db_id, short_name] = split_full(object_name);
    std::lock_guard<std::mutex> lock(mutex_);
    auto& workers = remote_idx_[db_id][short_name];
    if (std::find(workers.begin(), workers.end(), worker_id) == workers.end()) {
        workers.push_back(worker_id);
    }
}

void DataService::remove_remote_location(const CMString& object_name) {
    auto [db_id, short_name] = split_full(object_name);
    std::lock_guard<std::mutex> lock(mutex_);
    auto db_it = remote_idx_.find(db_id);
    if (db_it != remote_idx_.end()) {
        db_it->second.erase(short_name);
    }
}

void DataService::remove_remote_location(const CMString& object_name, uint64_t worker_id) {
    auto [db_id, short_name] = split_full(object_name);
    std::lock_guard<std::mutex> lock(mutex_);
    auto db_it = remote_idx_.find(db_id);
    if (db_it != remote_idx_.end()) {
        auto it = db_it->second.find(short_name);
        if (it != db_it->second.end()) {
            auto& workers = it->second;
            workers.erase(std::remove(workers.begin(), workers.end(), worker_id), workers.end());
            if (workers.empty()) {
                db_it->second.erase(it);
            }
        }
    }
}

CMVector<uint64_t> DataService::get_remote_workers(const CMString& object_name) const {
    auto [db_id, short_name] = split_full(object_name);
    std::lock_guard<std::mutex> lock(mutex_);
    auto db_it = remote_idx_.find(db_id);
    if (db_it != remote_idx_.end()) {
        auto it = db_it->second.find(short_name);
        if (it != db_it->second.end()) {
            return it->second;
        }
    }
    return {};
}

bool DataService::has_remote_location(const CMString& object_name) const {
    auto [db_id, short_name] = split_full(object_name);
    std::lock_guard<std::mutex> lock(mutex_);
    auto db_it = remote_idx_.find(db_id);
    if (db_it != remote_idx_.end()) {
        auto it = db_it->second.find(short_name);
        return it != db_it->second.end() && !it->second.empty();
    }
    return false;
}

RemoteObjectInfo DataService::lookup_remote_idx(const CMString& object_name) const {
    auto [db_id, short_name] = split_full(object_name);
    std::lock_guard<std::mutex> lock(mutex_);
    auto db_it = remote_idx_.find(db_id);
    if (db_it != remote_idx_.end()) {
        auto it = db_it->second.find(short_name);
        if (it != db_it->second.end() && !it->second.empty()) {
            uint64_t wid = it->second.front();
            auto wit = worker_registry_.find(wid);
            if (wit != worker_registry_.end()) {
                return wit->second;
            }
        }
    }
    return RemoteObjectInfo{};
}

void DataService::remove_remote_index(const CMString& object_name) {
    auto [db_id, short_name] = split_full(object_name);
    std::lock_guard<std::mutex> lock(mutex_);
    auto db_it = remote_idx_.find(db_id);
    if (db_it != remote_idx_.end()) {
        db_it->second.erase(short_name);
    }
}

// ============================================================
// Worker Registry
// ============================================================

void DataService::register_worker(uint64_t worker_id,
                                   const CMString& host,
                                   int32_t port) {
    std::lock_guard<std::mutex> lock(mutex_);
    worker_registry_[worker_id] = {worker_id, host, port};
}

RemoteObjectInfo DataService::get_worker_address(uint64_t worker_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = worker_registry_.find(worker_id);
    if (it != worker_registry_.end()) {
        return it->second;
    }
    return RemoteObjectInfo{};
}

// ============================================================
// Private Helpers — Name Parsing
// ============================================================

std::pair<CMString, CMString> DataService::split_full(const CMString& full) {
    if (full.size() > DB_ID_LEN && full[DB_ID_LEN] == ':') {
        return {full.substr(0, DB_ID_LEN), full.substr(DB_ID_LEN + 1)};
    }
    return {CMString{}, full};
}

CMString DataService::get_db_id_for_object(const CMString& object_name) const {
    return split_full(object_name).first;
}

// ============================================================
// Private Helpers — Read Operations
// ============================================================

ReadResult DataService::do_read_local_entries(const CMVector<IndexEntry>& entries,
                                               const DbPaths& paths) {
    CMString raw = do_read_raw_entries(entries, paths);
    if (raw.empty()) return ReadResult{};
    return decompress_raw(raw);
}

CMString DataService::do_read_raw_entries(const CMVector<IndexEntry>& entries,
                                            const DbPaths& paths) {
    DataReader reader(paths.base_path, paths.data_path, paths.writer_id);
    return reader.read_raw_bytes(entries.back());
}

// ============================================================
// Read Operations (3-tier fallback)
// ============================================================

void DataService::set_remote_compressed_read_handler(RemoteCompressedReadCallback cb) {
    std::lock_guard<std::mutex> lock(mutex_);
    remote_compressed_read_handler_ = std::move(cb);
}

void DataService::set_direct_compressed_read_handler(DirectCompressedReadCallback cb) {
    std::lock_guard<std::mutex> lock(mutex_);
    direct_compressed_read_handler_ = std::move(cb);
}

std::pair<bool, ReadResult> DataService::try_read_local(const CMString& object_name) {
    auto [db_id, short_name] = split_full(object_name);
    CMVector<IndexEntry> entries;
    DbPaths paths;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto db_it = local_idx_.find(db_id);
        if (db_it == local_idx_.end()) return {false, ReadResult{}};
        auto it = db_it->second.find(short_name);
        if (it == db_it->second.end() || !it->second) {
            return {false, ReadResult{}};
        }
        auto& info = *it->second;
        if (info.completion_state != CompletionState::COMPLETE || !info.flushed) {
            return {false, ReadResult{}};
        }
        entries = info.entries;

        auto path_it = db_paths_.find(db_id);
        if (path_it == db_paths_.end()) {
            return {false, ReadResult{}};
        }
        paths = path_it->second;
    }

    ReadResult result = do_read_local_entries(entries, paths);
    if (result.data_buffer.empty()) return {false, ReadResult{}};
    return {true, std::move(result)};
}

std::pair<bool, CMString> DataService::try_read_local_raw(const CMString& object_name) {
    auto [db_id, short_name] = split_full(object_name);
    CMVector<IndexEntry> entries;
    DbPaths paths;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto db_it = local_idx_.find(db_id);
        if (db_it == local_idx_.end()) return {false, {}};
        auto it = db_it->second.find(short_name);
        if (it == db_it->second.end() || !it->second) {
            return {false, {}};
        }
        auto& info = *it->second;
        if (info.completion_state != CompletionState::COMPLETE || !info.flushed) {
            return {false, {}};
        }
        entries = info.entries;

        auto path_it = db_paths_.find(db_id);
        if (path_it == db_paths_.end()) {
            return {false, {}};
        }
        paths = path_it->second;
    }

    CMString raw = do_read_raw_entries(entries, paths);
    if (raw.empty()) return {false, {}};
    return {true, std::move(raw)};
}

std::tuple<bool, CMString, CMString> DataService::try_read_local_raw_or_wait(
        const CMString& object_name, int timeout_ms) {
    auto [db_id, short_name] = split_full(object_name);
    CMSharedPtr<LocalObjectInfo> info;
    DbPaths paths;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto db_it = local_idx_.find(db_id);
        if (db_it == local_idx_.end()) return {false, {}, {}};
        auto it = db_it->second.find(short_name);
        if (it == db_it->second.end() || !it->second) {
            return {false, {}, {}};
        }
        info = it->second;

        if (info->completion_state == CompletionState::COMPLETE && info->flushed) {
            auto path_it = db_paths_.find(db_id);
            if (path_it == db_paths_.end()) {
                return {false, {}, {}};
            }
            paths = path_it->second;
        }
    }

    if (info->completion_state == CompletionState::COMPLETE && info->flushed) {
        CMString raw = do_read_raw_entries(info->entries, paths);
        if (raw.empty()) return {false, {}, {}};
        CMString py_name;
        DecompressingStreamBuf dsbuf(raw.data(), raw.size());
        py_name = dsbuf.py_name();
        return {true, std::move(raw), std::move(py_name)};
    }

    if (info->completion_state == CompletionState::FAILED) {
        return {false, {}, {}};
    }

    {
        std::unique_lock<std::mutex> cv_lock(info->cv_mutex);
        auto pred = [&info]() {
            return info->completion_state == CompletionState::FAILED ||
                   (info->completion_state == CompletionState::COMPLETE && info->flushed);
        };

        bool completed = true;
        if (timeout_ms < 0) {
            info->cv.wait(cv_lock, pred);
        } else {
            completed = info->cv.wait_for(cv_lock,
                std::chrono::milliseconds(timeout_ms), pred);
        }

        if (!completed || info->completion_state == CompletionState::FAILED) {
            return {false, {}, {}};
        }
    }

    DbPaths final_paths;
    CMVector<IndexEntry> final_entries;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto path_it = db_paths_.find(db_id);
        if (path_it == db_paths_.end()) return {false, {}, {}};
        final_paths = path_it->second;
        final_entries = info->entries;
    }

    CMString raw = do_read_raw_entries(final_entries, final_paths);
    if (raw.empty()) return {false, {}, {}};
    CMString py_name;
    DecompressingStreamBuf dsbuf(raw.data(), raw.size());
    py_name = dsbuf.py_name();
    return {true, std::move(raw), std::move(py_name)};
}

std::pair<bool, ReadResult> DataService::try_read_remote(const CMString& object_name) {
    auto [found, result] = try_read_local(object_name);
    if (found) return {true, std::move(result)};

    auto [comp_found, comp_data, comp_py_name, comp_hash] = read_raw_compressed(object_name);
    if (!comp_found || comp_data.empty()) return {false, ReadResult{}};

    ReadResult ret;
    ret.py_name = comp_py_name;

    DecompressingStreamBuf dsbuf(comp_data.data(), comp_data.size());
    std::istream is(&dsbuf);
    CMVector<char> tmp(4096);
    while (is) {
        is.read(tmp.data(), static_cast<std::streamsize>(tmp.size()));
        if (is.gcount() > 0) {
            ret.data_buffer.insert(ret.data_buffer.end(), tmp.data(),
                                   tmp.data() + static_cast<size_t>(is.gcount()));
        }
    }
    return {true, std::move(ret)};
}

std::pair<bool, ReadResult> DataService::try_read_local_or_wait(
        const CMString& object_name, int timeout_ms) {
    auto [db_id, short_name] = split_full(object_name);
    CMSharedPtr<LocalObjectInfo> info;
    DbPaths paths;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto db_it = local_idx_.find(db_id);
        if (db_it == local_idx_.end()) return {false, ReadResult{}};
        auto it = db_it->second.find(short_name);
        if (it == db_it->second.end() || !it->second) {
            return {false, ReadResult{}};
        }
        info = it->second;

        if (info->completion_state == CompletionState::COMPLETE && info->flushed) {
            auto path_it = db_paths_.find(db_id);
            if (path_it == db_paths_.end()) {
                return {false, ReadResult{}};
            }
            paths = path_it->second;
        }
    }

    if (info->completion_state == CompletionState::COMPLETE && info->flushed) {
        ReadResult read_result = do_read_local_entries(info->entries, paths);
        if (read_result.data_buffer.empty()) return {false, ReadResult{}};
        return {true, std::move(read_result)};
    }

    if (info->completion_state == CompletionState::FAILED) {
        return {false, ReadResult{}};
    }

    {
        std::unique_lock<std::mutex> cv_lock(info->cv_mutex);
        auto pred = [&info]() {
            return info->completion_state == CompletionState::FAILED ||
                   (info->completion_state == CompletionState::COMPLETE && info->flushed);
        };

        bool completed = true;
        if (timeout_ms < 0) {
            info->cv.wait(cv_lock, pred);
        } else {
            completed = info->cv.wait_for(cv_lock,
                std::chrono::milliseconds(timeout_ms), pred);
        }

        if (!completed || info->completion_state == CompletionState::FAILED) {
            return {false, ReadResult{}};
        }
    }

    if (!info->flushed) {
        return {false, ReadResult{}};
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto path_it = db_paths_.find(db_id);
        if (path_it == db_paths_.end()) {
            return {false, ReadResult{}};
        }
        paths = path_it->second;
    }

    ReadResult read_result = do_read_local_entries(info->entries, paths);
    if (read_result.data_buffer.empty()) return {false, ReadResult{}};
    return {true, std::move(read_result)};
}

std::tuple<bool, CMString, CMString, CMString> DataService::read_raw_compressed(const CMString& object_name) {
    auto [found, raw] = try_read_local_raw(object_name);
    if (found) {
        ReadResult header;
        auto [db_id, short_name] = split_full(object_name);
        DbPaths paths;
        CMVector<IndexEntry> entries;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto db_it = local_idx_.find(db_id);
            if (db_it != local_idx_.end()) {
                auto it = db_it->second.find(short_name);
                if (it != db_it->second.end() && it->second) {
                    entries = it->second->entries;
                }
            }
            auto path_it = db_paths_.find(db_id);
            if (path_it != db_paths_.end()) {
                paths = path_it->second;
            }
        }
        CMString py_name;
        CMString write_hash;
        if (!entries.empty() && !paths.base_path.empty()) {
            CMString entry_raw = do_read_raw_entries(entries, paths);
            if (!entry_raw.empty()) {
                DecompressingStreamBuf dsbuf(entry_raw.data(), entry_raw.size());
                py_name = dsbuf.py_name();
            }
            write_hash = entries.back().write_context_hash;
        }
        return {true, std::move(raw), std::move(py_name), std::move(write_hash)};
    }

    auto info = lookup_remote_idx(object_name);
    if (info.worker_id != 0 && !info.host.empty()) {
        DirectCompressedReadCallback cb;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            cb = direct_compressed_read_handler_;
        }
        if (cb) {
            auto [cb_found, cb_data, cb_py_name, cb_hash] = cb(info.host, info.port, object_name);
            if (cb_found) return {true, std::move(cb_data), std::move(cb_py_name), std::move(cb_hash)};
            remove_remote_location(object_name, info.worker_id);
        }
    }

    RemoteCompressedReadCallback remote_cb;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        remote_cb = remote_compressed_read_handler_;
    }
    if (remote_cb) {
        for (int attempt = 0; attempt < 3; ++attempt) {
            auto [cb_found, cb_data, cb_py_name] = remote_cb(object_name);
            if (cb_found && !cb_data.empty()) {
                return {true, std::move(cb_data), std::move(cb_py_name), {}};
            }
            if (attempt < 2) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            }
        }
    }

    return {false, {}, {}, {}};
}

// ============================================================
// Transfer Server
// ============================================================

void DataService::start_transfer_server(int thread_count, TransferCallback callback) {
    transfer_callback_ = std::move(callback);
    transfer_pool_ = CMMakeShared<IOThreadPool>(thread_count);
    transfer_pool_->start();
    transfer_running_ = true;
}

void DataService::stop_transfer_server() {
    transfer_running_ = false;
    if (transfer_pool_) {
        transfer_pool_->stop();
        transfer_pool_.reset();
    }
    transfer_callback_ = nullptr;
}

bool DataService::is_transfer_server_running() const {
    return transfer_running_;
}

void DataService::submit_transfer(uint64_t conn_id, const CMString& object_name) {
    if (!transfer_running_ || !transfer_pool_) return;

    auto result = CMMakeShared<TransferResult>();
    result->conn_id = conn_id;
    result->object_name = object_name;

    {
        auto [db_id, short_name] = split_full(object_name);
        std::lock_guard<std::mutex> lock(mutex_);
        auto db_it = local_idx_.find(db_id);
        if (db_it != local_idx_.end()) {
            auto it = db_it->second.find(short_name);
            if (it != db_it->second.end() && it->second && !it->second->entries.empty()) {
                result->write_context_hash = it->second->entries.back().write_context_hash;
            }
        }
    }

    auto callback = transfer_callback_;

    transfer_pool_->submit(
        [this, result]() {
            auto [found, raw_data, py_name] = try_read_local_raw_or_wait(result->object_name, -1);
            result->success = found;
            if (found) {
                result->compressed_data = std::move(raw_data);
                result->py_name = std::move(py_name);
            } else {
                result->error_message = "Object not found (raw): " + result->object_name;
            }
        },
        [callback, result]() {
            if (callback) {
                callback(*result);
            }
        }
    );
}

CMSharedPtr<IOThreadPool> DataService::get_transfer_pool() const {
    return transfer_pool_;
}

// ============================================================
// Write-Back Queue
// ============================================================

void DataService::start_write_back() {
    if (!write_back_queue_) {
        write_back_queue_ = CMMakeUnique<fly::WriteBackQueue>(10);
    }
    write_back_queue_->start();
}

void DataService::stop_write_back() {
    if (write_back_queue_) {
        write_back_queue_->stop();
    }
}

void DataService::enqueue_write_back(fly::WriteRequest&& task) {
    if (!write_back_queue_ || !write_back_queue_->is_running()) {
        start_write_back();
    }
    write_back_queue_->enqueue(std::move(task));
}

void DataService::drain_write_back() {
    if (write_back_queue_) {
        write_back_queue_->drain();
    }
}

bool DataService::is_write_back_running() const {
    return write_back_queue_ && write_back_queue_->is_running();
}

void DataService::mark_temp_entry(const CMString& object_name, const CMString& compressed_data) {
    temp_entries_.insert(object_name, compressed_data);
}

void DataService::unmark_temp_entry(const CMString& object_name) {
    temp_entries_.erase(object_name);
}

bool DataService::is_temp_entry(const CMString& object_name) const {
    return temp_entries_.contains(object_name);
}

std::pair<bool, CMString> DataService::get_temp_data(const CMString& object_name) const {
    auto val = temp_entries_.find(object_name);
    if (val.has_value()) return {true, *val};
    return {false, {}};
}

}  // namespace fly
