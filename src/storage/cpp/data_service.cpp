#include <storage/cpp/data_service.h>
#include <log/cpp/logger.h>
#include <chrono>

namespace fly {

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
                                     uint64_t writer_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = db_paths_.find(db_id);
    if (it != db_paths_.end()) {
        it->second = {base_path, data_path, writer_id};
        return;
    }
    for (const auto& [existing_id, paths] : db_paths_) {
        if (paths.base_path == base_path) {
            throw std::runtime_error(
                "base_path '" + base_path + "' already registered by database '" +
                existing_id + "'");
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
    std::lock_guard<std::mutex> lock(mutex_);
    auto& info = local_idx_[object_name];
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
        for (auto& [name, info] : local_idx_) {
            if (info && info->db_id == db_id) {
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
    CMSharedPtr<LocalObjectInfo> info = CMMakeShared<LocalObjectInfo>();
    info->db_id = db_id;
    info->completion_state = CompletionState::INCOMPLETE;

    std::lock_guard<std::mutex> lock(mutex_);
    local_idx_[object_name] = info;
}

void DataService::on_write_completed(const CMString& db_id,
                                      const CMString& object_name,
                                      const CMVector<IndexEntry>& entries) {
    CMSharedPtr<LocalObjectInfo> info;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = local_idx_.find(object_name);
        if (it == local_idx_.end() || !it->second) return;
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
    CMSharedPtr<LocalObjectInfo> info;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = local_idx_.find(object_name);
        if (it == local_idx_.end() || !it->second) return;
        info = it->second;
        info->completion_state = CompletionState::FAILED;
        info->error_message = error_message;
        local_idx_.erase(it);
    }
    info->cv.notify_all();
}

void DataService::update_remote_idx(const CMString& object_name,
                                      uint64_t worker_id,
                                      const CMString& host,
                                      int32_t port) {
    std::lock_guard<std::mutex> lock(mutex_);
    remote_idx_[object_name] = {worker_id, host, port};
}

bool DataService::has_remote_location(const CMString& object_name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return remote_idx_.count(object_name) > 0;
}

RemoteObjectInfo DataService::lookup_remote_idx(const CMString& object_name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = remote_idx_.find(object_name);
    if (it != remote_idx_.end()) {
        return it->second;
    }
    return RemoteObjectInfo{};
}

void DataService::remove_local_index(const CMString& object_name) {
    std::lock_guard<std::mutex> lock(mutex_);
    local_idx_.erase(object_name);
}

void DataService::remove_remote_index(const CMString& object_name) {
    std::lock_guard<std::mutex> lock(mutex_);
    remote_idx_.erase(object_name);
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
    CMString db_id;
    CMVector<IndexEntry> entries;
    DbPaths paths;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = local_idx_.find(object_name);
        if (it == local_idx_.end() || !it->second) {
            return {false, ReadResult{}};
        }
        auto& info = *it->second;
        if (info.completion_state != CompletionState::COMPLETE || !info.flushed) {
            return {false, ReadResult{}};
        }
        db_id = info.db_id;
        entries = info.entries;

        auto path_it = db_paths_.find(db_id);
        if (path_it == db_paths_.end()) {
            return {false, ReadResult{}};
        }
        paths = path_it->second;
    }

    try {
        DataReader reader(paths.base_path, paths.data_path, paths.writer_id);
        ReadResult result = reader.read_from_entries(entries);
        return {true, result};
    } catch (const std::exception& e) {
        return {false, ReadResult{}};
    }
}

std::pair<bool, ReadResult> DataService::try_read_local_or_wait(
        const CMString& object_name, int timeout_ms) {
    CMSharedPtr<LocalObjectInfo> info;
    CMString db_id;
    DbPaths paths;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = local_idx_.find(object_name);
        if (it == local_idx_.end() || !it->second) {
            return {false, ReadResult{}};
        }
        info = it->second;
        db_id = info->db_id;

        if (info->completion_state == CompletionState::COMPLETE && info->flushed) {
            auto path_it = db_paths_.find(db_id);
            if (path_it == db_paths_.end()) {
                return {false, ReadResult{}};
            }
            paths = path_it->second;
        }
    }

    if (info->completion_state == CompletionState::COMPLETE && info->flushed) {
        try {
            DataReader reader(paths.base_path, paths.data_path, paths.writer_id);
            return {true, reader.read_from_entries(info->entries)};
    } catch (const std::exception& e) {
        ERR("try_read_local FAILED for '{}': {}", object_name, e.what());
        return {false, ReadResult{}};
    }
}

    if (info->completion_state == CompletionState::FAILED) {
        return {false, ReadResult{}};
    }

    {
        std::unique_lock<std::mutex> cv_lock(info->cv_mutex);
        bool completed = info->cv.wait_for(cv_lock,
            std::chrono::milliseconds(timeout_ms),
            [&info]() {
                return info->completion_state == CompletionState::FAILED ||
                       (info->completion_state == CompletionState::COMPLETE && info->flushed);
            });

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

    try {
        DataReader reader(paths.base_path, paths.data_path, paths.writer_id);
        return {true, reader.read_from_entries(info->entries)};
    } catch (const std::exception& e) {
        return {false, ReadResult{}};
    }
}

bool DataService::has_local_object(const CMString& object_name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = local_idx_.find(object_name);
    return it != local_idx_.end() && it->second &&
           it->second->completion_state == CompletionState::COMPLETE && it->second->flushed;
}

void DataService::set_remote_read_handler(RemoteReadCallback cb) {
    remote_read_handler_ = std::move(cb);
}

void DataService::set_direct_read_handler(DirectReadCallback cb) {
    std::lock_guard<std::mutex> lock(mutex_);
    direct_read_handler_ = std::move(cb);
}

ReadResult DataService::read_raw(const CMString& object_name, int max_retries) {
    // Tier 1: local
    auto [found, result] = try_read_local(object_name);
    if (found) {
        return result;
    }

    // Tier 2: remote_idx cache → direct worker-to-worker
    auto info = lookup_remote_idx(object_name);
    if (info.worker_id != 0 && !info.host.empty()) {
        DirectReadCallback direct_cb;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            direct_cb = direct_read_handler_;
        }
        if (direct_cb) {
            try {
                return direct_cb(info.host, info.port, object_name);
            } catch (const std::exception&) {
                // stale cache, fall through to full remote
            }
        }
    }

    // Tier 3: full remote via Master
    RemoteReadCallback remote_cb;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        remote_cb = remote_read_handler_;
    }

    if (!remote_cb) {
        throw std::runtime_error("No remote read handler registered for: " + object_name);
    }

    std::string last_error;
    for (int attempt = 0; attempt < max_retries; ++attempt) {
        try {
            return remote_cb(object_name);
        } catch (const std::exception& e) {
            last_error = e.what();
            if (attempt < max_retries - 1) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            }
        }
    }

    throw std::runtime_error(
        "Failed to read '" + object_name + "' after " +
        std::to_string(max_retries) + " attempts: " + last_error);
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

    auto callback = transfer_callback_;

    transfer_pool_->submit(
        [this, result]() {
            auto [found, read_result] = try_read_local_or_wait(result->object_name);
            result->success = found;
            if (found) {
                result->data.assign(read_result.data_buffer.begin(), read_result.data_buffer.end());
            } else {
                result->error_message = "Object not found: " + result->object_name;
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
    // Group entries by object_name
    CMUnorderedMap<CMString, CMVector<IndexEntry>> grouped;
    for (const auto& e : entries) {
        grouped[e.object_name].push_back(e);
    }

    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [object_name, obj_entries] : grouped) {
        auto& info = local_idx_[object_name];
        if (!info) {
            info = CMMakeShared<LocalObjectInfo>();
        }
        info->db_id = db_id;
        // Append entries (don't replace — there might be existing entries)
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
    CMSharedPtr<LocalObjectInfo> info;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = local_idx_.find(object_name);
        if (it != local_idx_.end() && it->second) {
            it->second->flushed = true;
            info = it->second;
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
