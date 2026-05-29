#include <storage/cpp/data_service.h>
#include <log/cpp/logger.h>
#include <chrono>
#include <algorithm>
#include <utility>

namespace fly {

namespace {

constexpr size_t DB_ID_LEN = 32;

std::pair<CMString, CMString> split_full(const CMString& full) {
    if (full.size() > DB_ID_LEN && full[DB_ID_LEN] == ':') {
        return {full.substr(0, DB_ID_LEN), full.substr(DB_ID_LEN + 1)};
    }
    return {CMString{}, full};
}

}

DataService& DataService::instance() {
    static DataService instance;
    return instance;
}

DataService::~DataService() {
    if (write_back_queue_) {
        write_back_queue_->drain();
        write_back_queue_->stop();
    }
}

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

void DataService::remove_local_index(const CMString& object_name) {
    auto [db_id, short_name] = split_full(object_name);
    std::lock_guard<std::mutex> lock(mutex_);
    auto db_it = local_idx_.find(db_id);
    if (db_it != local_idx_.end()) {
        db_it->second.erase(short_name);
    }
}

void DataService::remove_remote_index(const CMString& object_name) {
    auto [db_id, short_name] = split_full(object_name);
    std::lock_guard<std::mutex> lock(mutex_);
    auto db_it = remote_idx_.find(db_id);
    if (db_it != remote_idx_.end()) {
        db_it->second.erase(short_name);
    }
}

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

    DataReader reader(paths.base_path, paths.data_path, paths.writer_id);
    ReadResult result = reader.read_from_entries(entries);
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

    DataReader reader(paths.base_path, paths.data_path, paths.writer_id);
    if (entries.size() != 1) {
        ERR("try_read_local_raw: multi-entry objects not supported for raw transfer");
        return {false, {}};
    }
    CMString raw = reader.read_raw_bytes(entries.front());
    if (raw.empty()) return {false, {}};
    return {true, std::move(raw)};
}

std::pair<bool, ReadResult> DataService::try_read_remote(const CMString& object_name) {
    auto [found, result] = try_read_local(object_name);
    if (found) return {true, std::move(result)};

    auto info = lookup_remote_idx(object_name);
    if (info.worker_id != 0 && !info.host.empty()) {
        DirectReadCallback direct_cb;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            direct_cb = direct_read_handler_;
        }
        if (direct_cb) {
            auto [cb_found, cb_result] = direct_cb(info.host, info.port, object_name);
            if (cb_found) return {true, std::move(cb_result)};
            remove_remote_location(object_name, info.worker_id);
        }
    }

    RemoteReadCallback remote_cb;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        remote_cb = remote_read_handler_;
    }
    if (!remote_cb) return {false, ReadResult{}};

    return remote_cb(object_name);
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
        DataReader reader(paths.base_path, paths.data_path, paths.writer_id);
        auto read_result = reader.read_from_entries(info->entries);
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

    DataReader reader(paths.base_path, paths.data_path, paths.writer_id);
    auto read_result = reader.read_from_entries(info->entries);
    if (read_result.data_buffer.empty()) return {false, ReadResult{}};
    return {true, std::move(read_result)};
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

void DataService::set_remote_read_handler(RemoteReadCallback cb) {
    remote_read_handler_ = std::move(cb);
}

void DataService::set_direct_read_handler(DirectReadCallback cb) {
    std::lock_guard<std::mutex> lock(mutex_);
    direct_read_handler_ = std::move(cb);
}

void DataService::set_direct_compressed_read_handler(DirectCompressedReadCallback cb) {
    std::lock_guard<std::mutex> lock(mutex_);
    direct_compressed_read_handler_ = std::move(cb);
}

std::tuple<bool, CMString, CMString> DataService::read_raw_compressed(const CMString& object_name) {
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
        if (!entries.empty() && !paths.base_path.empty()) {
            DataReader reader(paths.base_path, paths.data_path, paths.writer_id);
            auto hr = reader.read_object_data(entries.front());
            py_name = hr.py_name;
        }
        return {true, std::move(raw), std::move(py_name)};
    }

    auto info = lookup_remote_idx(object_name);
    if (info.worker_id != 0 && !info.host.empty()) {
        DirectCompressedReadCallback cb;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            cb = direct_compressed_read_handler_;
        }
        if (cb) {
            auto [cb_found, cb_data, cb_py_name] = cb(info.host, info.port, object_name);
            if (cb_found) return {true, std::move(cb_data), std::move(cb_py_name)};
            remove_remote_location(object_name, info.worker_id);
        }
    }

    return {false, {}, {}};
}

ReadResult DataService::read_raw(const CMString& object_name, int max_retries) {
    auto [found, result] = try_read_local(object_name);
    if (found) return result;

    auto info = lookup_remote_idx(object_name);
    if (info.worker_id != 0 && !info.host.empty()) {
        DirectReadCallback direct_cb;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            direct_cb = direct_read_handler_;
        }
        if (direct_cb) {
            auto [cb_found, cb_result] = direct_cb(info.host, info.port, object_name);
            if (cb_found) return cb_result;
            remove_remote_location(object_name, info.worker_id);
        }
    }

    RemoteReadCallback remote_cb;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        remote_cb = remote_read_handler_;
    }

    if (!remote_cb) {
        ERR("No remote read handler registered for: {}", object_name);
        return ReadResult{};
    }

    for (int attempt = 0; attempt < max_retries; ++attempt) {
        auto [cb_found, cb_result] = remote_cb(object_name);
        if (cb_found) return cb_result;
        if (attempt < max_retries - 1) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
    }

    ERR("Failed to read '{}' after {} attempts", object_name, max_retries);
    return ReadResult{};
}


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

void DataService::reset() {
    stop_transfer_server();
    drain_write_back();
    stop_write_back();
    local_idx_.clear();
    remote_idx_.clear();
    worker_registry_.clear();
    db_paths_.clear();
    remote_read_handler_ = nullptr;
    direct_read_handler_ = nullptr;
    direct_compressed_read_handler_ = nullptr;
}

bool DataService::is_transfer_server_running() const {
    return transfer_running_;
}

void DataService::submit_transfer(uint64_t conn_id, const CMString& object_name, bool raw_transfer) {
    if (!transfer_running_ || !transfer_pool_) return;

    auto result = CMMakeShared<TransferResult>();
    result->conn_id = conn_id;
    result->object_name = object_name;

    auto callback = transfer_callback_;

    transfer_pool_->submit(
        [this, result, raw_transfer]() {
            if (raw_transfer) {
                auto [found, raw_data] = try_read_local_raw(result->object_name);
                result->success = found;
                if (found) {
                    result->compressed_data = std::move(raw_data);
                    auto [db_id, short_name] = split_full(result->object_name);
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
                    }
                    if (!entries.empty()) {
                        CMString file_path;
                        auto path_it = db_paths_.find(db_id);
                        if (path_it != db_paths_.end()) {
                            DataReader reader(path_it->second.base_path,
                                             path_it->second.data_path,
                                             path_it->second.writer_id);
                            ReadResult header_result = reader.read_object_data(entries.front());
                            result->py_name = header_result.py_name;
                        }
                    }
                } else {
                    result->error_message = "Object not found (raw): " + result->object_name;
                }
            } else {
                auto [found, read_result] = try_read_local_or_wait(result->object_name, -1);
                result->success = found;
                if (found) {
                    result->data.assign(read_result.data_buffer.begin(), read_result.data_buffer.end());
                } else {
                    result->error_message = "Object not found: " + result->object_name;
                }
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

}  // namespace fly
