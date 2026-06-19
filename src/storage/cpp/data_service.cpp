#include <storage/cpp/data_service.h>
#include <storage/cpp/data_server.h>
#include <storage/cpp/data_reader.h>
#include <storage/cpp/temp_store.h>
#include <storage/cpp/compressor.h>
#include <storage/cpp/compression_utils.h>
#include <storage/cpp/decompressing_streambuf.h>
#include <storage/cpp/object_cache.h>
#include <serialization/cpp/object_header.h>
#include <core/cpp/config.h>
#include <log/cpp/logger.h>
#include <chrono>
#include <algorithm>
#include <utility>
#include <cstring>

namespace fly {

namespace {
// Single source of truth for db_id length; exposed externally via the
// inline db_id_len() function in the header. Kept here as an implementation
// detail — split_full() uses it directly for fixed-offset parsing.
constexpr size_t kDbIdLen = db_id_len();
}  // namespace

CMSharedPtr<DataService> DataService::instance() {
    static CMSharedPtr<DataService> inst = CMMakeShared<DataService>();
    return inst;
}

DataService::DataService() = default;

namespace {

ReadResult decompress_raw(const CMString& raw) {
    ReadResult result;
    DecompressingStreamBuf dsbuf(raw.data(), raw.size());
    result.py_name_ = dsbuf.py_name();
    std::istream is(&dsbuf);
    CMVector<char> tmp(4096);
    while (is) {
        is.read(tmp.data(), static_cast<std::streamsize>(tmp.size()));
        if (is.gcount() > 0) {
            result.data_buffer_.insert(result.data_buffer_.end(),
                tmp.data(), tmp.data() + static_cast<size_t>(is.gcount()));
        }
    }
    return result;
}

}  // namespace

DataService::~DataService() {
    if (data_server_) {
        data_server_->stop();
    }
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
    stop_data_server();
    drain_write_back();
    stop_write_back();
    local_idx_.clear();
    remote_idx_.clear();
    worker_registry_.clear();
    db_paths_.clear();
    remote_compressed_read_handler_ = nullptr;
    direct_compressed_read_handler_ = nullptr;
    temp_lru_order_.clear();
    temp_total_bytes_ = 0;
    if (temp_eviction_store_) {
        temp_eviction_store_->cleanup_all();
    }
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
        if (paths.base_path_ == base_path) {
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
    info->db_id_ = db_id;
    info->entries_.push_back(entry);
    info->flushed_ = false;
    info->completion_state_ = CompletionState::COMPLETE;
}

void DataService::on_flush(const CMString& db_id) {
    CMVector<CMSharedPtr<LocalObjectInfo>> to_notify;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto db_it = local_idx_.find(db_id);
        if (db_it == local_idx_.end()) return;
        for (auto& [name, info] : db_it->second) {
            if (info) {
                info->flushed_ = true;
                to_notify.push_back(info);
            }
        }
    }
    for (auto& info : to_notify) {
        info->cv_.notify_all();
    }
}

void DataService::on_write_started(const CMString& db_id,
                                     const CMString& object_name) {
    auto [_, short_name] = split_full(object_name);
    CMSharedPtr<LocalObjectInfo> info = CMMakeShared<LocalObjectInfo>();
    info->db_id_ = db_id;
    info->completion_state_ = CompletionState::INCOMPLETE;

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
        info->entries_ = entries;
        info->completion_state_ = CompletionState::COMPLETE;
    }
    {
        std::lock_guard<std::mutex> cv_lock(info->cv_mutex_);
    }
    info->cv_.notify_all();
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
        info->completion_state_ = CompletionState::FAILED;
        info->error_message_ = error_message;
        db_it->second.erase(it);
    }
    info->cv_.notify_all();
}

void DataService::remove_local_index(const CMString& object_name) {
    auto [db_id, short_name] = split_full(object_name);
    int64_t freed_bytes = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto db_it = local_idx_.find(db_id);
        if (db_it != local_idx_.end()) {
            auto it = db_it->second.find(short_name);
            if (it != db_it->second.end() && it->second && it->second->is_temp_) {
                freed_bytes = it->second->temp_compressed_data_ ? static_cast<int64_t>(it->second->temp_compressed_data_->size()) : 0;
            }
            db_it->second.erase(short_name);
        }
    }
    // Invalidate cached bytes (low/high tier) for this object so subsequent
    // reads don't return stale data after removal.
    fly::ObjectCache::instance().remove(object_name);

    if (freed_bytes > 0) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto lru_it = std::find(temp_lru_order_.begin(), temp_lru_order_.end(), object_name);
        if (lru_it != temp_lru_order_.end()) {
            temp_lru_order_.erase(lru_it);
        }
        if (temp_eviction_store_) {
            temp_eviction_store_->remove(object_name);
        }
        temp_total_bytes_ -= freed_bytes;
        if (temp_total_bytes_ < 0) temp_total_bytes_ = 0;
    }
}

bool DataService::has_local_object(const CMString& object_name) const {
    auto [db_id, short_name] = split_full(object_name);
    std::lock_guard<std::mutex> lock(mutex_);
    auto db_it = local_idx_.find(db_id);
    if (db_it == local_idx_.end()) return false;
    auto it = db_it->second.find(short_name);
    return it != db_it->second.end() && it->second &&
           it->second->completion_state_ == CompletionState::COMPLETE;
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
                it->second->flushed_ = true;
                info = it->second;
            }
        }
    }
    if (info) {
        {
            std::lock_guard<std::mutex> cv_lock(info->cv_mutex_);
        }
        info->cv_.notify_all();
    }
}

void DataService::restore_entries(const CMString& db_id,
                                    const CMVector<IndexEntry>& entries) {
    CMUnorderedMap<CMString, CMVector<IndexEntry>> grouped;
    for (const auto& e : entries) {
        auto [entry_db_id, short_name] = split_full(e.object_name_);
        const CMString& key = entry_db_id.empty() ? e.object_name_ : short_name;
        grouped[key].push_back(e);
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto& db_map = local_idx_[db_id];
    for (auto& [short_name, obj_entries] : grouped) {
        auto& info = db_map[short_name];
        if (!info) {
            info = CMMakeShared<LocalObjectInfo>();
        }
        info->db_id_ = db_id;
        for (auto& e : obj_entries) {
            info->entries_.push_back(std::move(e));
        }
        info->completion_state_ = CompletionState::COMPLETE;
        info->flushed_ = true;
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
    return it->second->entries_;
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
    auto& meta = remote_idx_[db_id][short_name];
    if (std::find(meta.workers_.begin(), meta.workers_.end(), worker_id) == meta.workers_.end()) {
        meta.workers_.push_back(worker_id);
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
            auto& workers = it->second.workers_;
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
            return it->second.workers_;
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
        return it != db_it->second.end() && !it->second.workers_.empty();
    }
    return false;
}

RemoteObjectInfo DataService::lookup_remote_idx(const CMString& object_name) const {
    auto [db_id, short_name] = split_full(object_name);
    std::lock_guard<std::mutex> lock(mutex_);
    auto db_it = remote_idx_.find(db_id);
    if (db_it != remote_idx_.end()) {
        auto it = db_it->second.find(short_name);
        if (it != db_it->second.end() && !it->second.workers_.empty()) {
            uint64_t wid = it->second.workers_.front();
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
    // Invalidate cached bytes (a prior remote read may have populated the low
    // tier via read_raw_compressed → put_low; remove it to avoid stale data).
    fly::ObjectCache::instance().remove(object_name);
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
    // Fixed-length split: db_id is exactly kDbIdLen base62 chars followed by ':'.
    // Avoids a separator scan; relies on the invariant that every full object
    // name produced by Database::full_name() is "<db_id>:<short_name>".
    if (full.size() > kDbIdLen && full[kDbIdLen] == ':') {
        return {full.substr(0, kDbIdLen), full.substr(kDbIdLen + 1)};
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
    FlyBufferPtr raw = do_read_raw_entries(entries, paths);
    if (!raw || raw->empty()) return ReadResult{};
    return decompress_raw(CMString(raw->data(), raw->size()));
}

FlyBufferPtr DataService::do_read_raw_entries(const CMVector<IndexEntry>& entries,
                                            const DbPaths& paths) {
    DataReader reader(paths.base_path_, paths.data_path_, paths.writer_id_);
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
    bool is_temp = false;
    FlyBufferPtr temp_data;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto db_it = local_idx_.find(db_id);
        if (db_it == local_idx_.end()) return {false, ReadResult{}};
        auto it = db_it->second.find(short_name);
        if (it == db_it->second.end() || !it->second) {
            return {false, ReadResult{}};
        }
        auto& info = *it->second;
        if (info.completion_state_ != CompletionState::COMPLETE || !info.flushed_) {
            return {false, ReadResult{}};
        }

        if (info.is_temp_) {
            is_temp = true;
            temp_data = info.temp_compressed_data_;
        } else {
            entries = info.entries_;

            auto path_it = db_paths_.find(db_id);
            if (path_it == db_paths_.end()) {
                return {false, ReadResult{}};
            }
            paths = path_it->second;
        }
    }

    if (is_temp) {
        if (!temp_data) {
            if (temp_eviction_store_) {
                auto [found, data] = temp_eviction_store_->get(object_name);
                if (!found) return {false, ReadResult{}};
                temp_data = CMMakeShared<FlyBuffer>();
                temp_data->take(std::move(data));
            } else {
                return {false, ReadResult{}};
            }
        }
        return {true, decompress_raw(CMString(temp_data->data(), temp_data->size()))};
    }

    ReadResult result = do_read_local_entries(entries, paths);
    if (result.data_buffer_.empty()) return {false, ReadResult{}};
    return {true, std::move(result)};
}

std::pair<bool, FlyBufferPtr> DataService::try_read_local_raw(const CMString& object_name) {
    // Short-circuit: serve compressed bytes from the ObjectCache low tier when
    // available. This benefits both the remote DataServer serve path (which
    // calls this directly) and the local read_raw_compressed Tier-1 path —
    // avoiding index lookup + disk IO entirely on a cache hit.
    if (auto [hit, cached] = fly::ObjectCache::instance().get_low(object_name); hit) {
        return {true, cached};
    }

    auto [db_id, short_name] = split_full(object_name);
    CMVector<IndexEntry> entries;
    DbPaths paths;
    bool is_temp = false;
    FlyBufferPtr temp_data;
    int diag = 0;  // 0=not_found_db, 1=not_found_obj, 2=not_ready, 3=found_temp

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto db_it = local_idx_.find(db_id);
        if (db_it == local_idx_.end()) {
            diag = 0;
        } else {
            auto it = db_it->second.find(short_name);
            if (it == db_it->second.end() || !it->second) {
                diag = 1;
            } else {
                auto& info = *it->second;
                if (info.completion_state_ != CompletionState::COMPLETE) {
                    diag = 2;
                } else if (info.is_temp_) {
                    is_temp = true;
                    temp_data = info.temp_compressed_data_;
                    diag = 3;
                } else {
                    entries = info.entries_;
                    auto path_it = db_paths_.find(db_id);
                    if (path_it == db_paths_.end()) {
                        diag = 0;
                    } else {
                        paths = path_it->second;
                        diag = 3;
                    }
                }
            }
        }
    }

    switch (diag) {
        case 0: DBG("[TIER1] NOT FOUND: obj={}", object_name); break;
        case 1: DBG("[TIER1] NOT FOUND: obj={}, short_name={}", object_name, short_name); break;
        case 2: DBG("[TEMP-READ-LOCAL] NOT READY: obj={}", object_name); break;
        case 3: DBG("[TEMP-READ-LOCAL] FOUND: obj={}, data_size={}", object_name, temp_data ? temp_data->size() : 0); break;
    }

    if (diag != 3) return {false, nullptr};

    if (is_temp) {
        if (temp_data) {
            return {true, temp_data};  // zero-copy shared_ptr return
        }
        if (temp_eviction_store_) {
            auto [found, data] = temp_eviction_store_->get(object_name);
            if (!found) return {false, nullptr};
            auto buf = CMMakeShared<FlyBuffer>();
            buf->take(std::move(data));
            return {true, buf};
        }
        return {false, nullptr};
    }

    FlyBufferPtr raw = do_read_raw_entries(entries, paths);
    if (!raw || raw->empty()) return {false, nullptr};

    // Populate the low-tier cache so subsequent reads (local or remote serve)
    // skip disk IO. Account by uncompressed size from the object header;
    // fall back to compressed size if the header cannot be parsed.
    size_t accounted = raw->size();
    try {
        int64_t off = 0;
        auto hdr = ObjectHeader::deserialize({raw->data(), raw->size()}, off);
        if (hdr.total_size_ > 0) accounted = static_cast<size_t>(hdr.total_size_);
    } catch (...) {}
    fly::ObjectCache::instance().put_low(object_name, raw, accounted);

    return {true, raw};
}

bool DataService::is_write_in_progress(const CMString& object_name) const {
    auto [db_id, short_name] = split_full(object_name);
    std::lock_guard<std::mutex> lock(mutex_);
    auto db_it = local_idx_.find(db_id);
    if (db_it == local_idx_.end()) return false;
    auto it = db_it->second.find(short_name);
    if (it == db_it->second.end() || !it->second) return false;
    return it->second->completion_state_ == CompletionState::INCOMPLETE;
}

std::tuple<bool, FlyBufferPtr, CMString> DataService::try_read_local_raw_or_wait(
        const CMString& object_name, int timeout_ms) {
    auto [db_id, short_name] = split_full(object_name);
    CMSharedPtr<LocalObjectInfo> info;
    DbPaths paths;
    bool is_temp = false;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto db_it = local_idx_.find(db_id);
        if (db_it == local_idx_.end()) return {false, nullptr, {}};
        auto it = db_it->second.find(short_name);
        if (it == db_it->second.end() || !it->second) {
            return {false, nullptr, {}};
        }
        info = it->second;

        bool readable = info->completion_state_ == CompletionState::COMPLETE;
        if (readable) {
            if (info->is_temp_) {
                is_temp = true;
            } else {
                auto path_it = db_paths_.find(db_id);
                if (path_it == db_paths_.end()) {
                    return {false, nullptr, {}};
                }
                paths = path_it->second;
            }
        }
    }

    bool readable = info->completion_state_ == CompletionState::COMPLETE &&
                    (info->is_temp_ || info->flushed_);
    if (readable) {
        if (is_temp) {
            FlyBufferPtr temp_data = info->temp_compressed_data_;
            if (!temp_data) {
                if (temp_eviction_store_) {
                    auto [found, data] = temp_eviction_store_->get(object_name);
                    if (!found) return {false, nullptr, {}};
                    temp_data = CMMakeShared<FlyBuffer>();
                    temp_data->take(std::move(data));
                } else {
                    return {false, nullptr, {}};
                }
            }
            CMString py_name;
            DecompressingStreamBuf dsbuf(temp_data->data(), temp_data->size());
            py_name = dsbuf.py_name();
            return {true, temp_data, std::move(py_name)};
        }

        FlyBufferPtr raw = do_read_raw_entries(info->entries_, paths);
        if (!raw || raw->empty()) return {false, nullptr, {}};
        CMString py_name;
        DecompressingStreamBuf dsbuf(raw->data(), raw->size());
        py_name = dsbuf.py_name();
        return {true, raw, std::move(py_name)};
    }

    if (info->completion_state_ == CompletionState::FAILED) {
        return {false, nullptr, {}};
    }

    {
        std::unique_lock<std::mutex> cv_lock(info->cv_mutex_);
        auto pred = [&info]() {
            return info->completion_state_ == CompletionState::FAILED ||
                   info->completion_state_ == CompletionState::COMPLETE;
        };

        bool completed = true;
        if (timeout_ms < 0) {
            info->cv_.wait(cv_lock, pred);
        } else {
            completed = info->cv_.wait_for(cv_lock,
                std::chrono::milliseconds(timeout_ms), pred);
        }

        if (!completed || info->completion_state_ == CompletionState::FAILED) {
            return {false, nullptr, {}};
        }
    }

    if (info->is_temp_) {
        FlyBufferPtr temp_data = info->temp_compressed_data_;
        if (!temp_data) {
            if (temp_eviction_store_) {
                auto [found, data] = temp_eviction_store_->get(object_name);
                if (!found) return {false, nullptr, {}};
                temp_data = CMMakeShared<FlyBuffer>();
                temp_data->take(std::move(data));
            } else {
                return {false, nullptr, {}};
            }
        }
        CMString py_name;
        DecompressingStreamBuf dsbuf(temp_data->data(), temp_data->size());
        py_name = dsbuf.py_name();
        return {true, temp_data, std::move(py_name)};
    }

    DbPaths final_paths;
    CMVector<IndexEntry> final_entries;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto path_it = db_paths_.find(db_id);
        if (path_it == db_paths_.end()) return {false, nullptr, {}};
        final_paths = path_it->second;
        final_entries = info->entries_;
    }

    FlyBufferPtr raw = do_read_raw_entries(final_entries, final_paths);
    if (!raw || raw->empty()) return {false, nullptr, {}};
    CMString py_name;
    DecompressingStreamBuf dsbuf(raw->data(), raw->size());
    py_name = dsbuf.py_name();
    return {true, raw, std::move(py_name)};
}

std::pair<bool, ReadResult> DataService::try_read_remote(const CMString& object_name) {
    auto [found, result] = try_read_local(object_name);
    if (found) return {true, std::move(result)};

    auto [comp_found, comp_data, comp_py_name, comp_hash, can_still_produce] = read_raw_compressed(object_name);
    if (!comp_found || !comp_data || comp_data->empty()) {
        ReadResult empty;
        empty.can_still_produce_ = can_still_produce;
        return {false, std::move(empty)};
    }

    ReadResult ret;
    ret.py_name_ = comp_py_name;
    ret.can_still_produce_ = false;

    DecompressingStreamBuf dsbuf(comp_data->data(), comp_data->size());
    std::istream is(&dsbuf);
    CMVector<char> tmp(4096);
    while (is) {
        is.read(tmp.data(), static_cast<std::streamsize>(tmp.size()));
        if (is.gcount() > 0) {
            ret.data_buffer_.insert(ret.data_buffer_.end(), tmp.data(),
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
    bool is_temp = false;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto db_it = local_idx_.find(db_id);
        if (db_it == local_idx_.end()) return {false, ReadResult{}};
        auto it = db_it->second.find(short_name);
        if (it == db_it->second.end() || !it->second) {
            return {false, ReadResult{}};
        }
        info = it->second;

        bool readable = info->completion_state_ == CompletionState::COMPLETE;
        if (readable) {
            if (info->is_temp_) {
                is_temp = true;
            } else {
                auto path_it = db_paths_.find(db_id);
                if (path_it == db_paths_.end()) {
                    return {false, ReadResult{}};
                }
                paths = path_it->second;
            }
        }
    }

    bool readable2 = info->completion_state_ == CompletionState::COMPLETE;
    if (readable2) {
        if (is_temp) {
            FlyBufferPtr temp_data = info->temp_compressed_data_;
            if (!temp_data) {
                if (temp_eviction_store_) {
                    auto [found, data] = temp_eviction_store_->get(object_name);
                    if (!found) return {false, ReadResult{}};
                    temp_data = CMMakeShared<FlyBuffer>();
                    temp_data->take(std::move(data));
                } else {
                    return {false, ReadResult{}};
                }
            }
            return {true, decompress_raw(CMString(temp_data->data(), temp_data->size()))};
        }
        ReadResult read_result = do_read_local_entries(info->entries_, paths);
        if (read_result.data_buffer_.empty()) return {false, ReadResult{}};
        return {true, std::move(read_result)};
    }

    if (info->completion_state_ == CompletionState::FAILED) {
        return {false, ReadResult{}};
    }

    {
        std::unique_lock<std::mutex> cv_lock(info->cv_mutex_);
        auto pred = [&info]() {
            return info->completion_state_ == CompletionState::FAILED ||
                   info->completion_state_ == CompletionState::COMPLETE;
        };

        bool completed = true;
        if (timeout_ms < 0) {
            info->cv_.wait(cv_lock, pred);
        } else {
            completed = info->cv_.wait_for(cv_lock,
                std::chrono::milliseconds(timeout_ms), pred);
        }

        if (!completed || info->completion_state_ == CompletionState::FAILED) {
            return {false, ReadResult{}};
        }
    }

    if (info->is_temp_) {
        FlyBufferPtr temp_data = info->temp_compressed_data_;
        if (!temp_data) {
            if (temp_eviction_store_) {
                auto [found, data] = temp_eviction_store_->get(object_name);
                if (!found) return {false, ReadResult{}};
                temp_data = CMMakeShared<FlyBuffer>();
                temp_data->take(std::move(data));
            } else {
                return {false, ReadResult{}};
            }
        }
        return {true, decompress_raw(CMString(temp_data->data(), temp_data->size()))};
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto path_it = db_paths_.find(db_id);
        if (path_it == db_paths_.end()) {
            return {false, ReadResult{}};
        }
        paths = path_it->second;
    }

    ReadResult read_result = do_read_local_entries(info->entries_, paths);
    if (read_result.data_buffer_.empty()) return {false, ReadResult{}};
    return {true, std::move(read_result)};
}

std::tuple<bool, FlyBufferPtr, CMString, CMString, bool> DataService::read_raw_compressed(const CMString& object_name) {
    auto [found, raw] = try_read_local_raw(object_name);
    if (found) {
        auto [db_id, short_name] = split_full(object_name);
        bool is_temp_entry = false;
        DbPaths paths;
        CMVector<IndexEntry> entries;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto db_it = local_idx_.find(db_id);
            if (db_it != local_idx_.end()) {
                auto it = db_it->second.find(short_name);
                if (it != db_it->second.end() && it->second) {
                    is_temp_entry = it->second->is_temp_;
                    if (!is_temp_entry) {
                        entries = it->second->entries_;
                    }
                }
            }
            auto path_it = db_paths_.find(db_id);
            if (path_it != db_paths_.end()) {
                paths = path_it->second;
            }
        }

        if (is_temp_entry) {
            CMString py_name;
            DecompressingStreamBuf dsbuf(raw->data(), raw->size());
            py_name = dsbuf.py_name();
            return {true, raw, std::move(py_name), {}, false};
        }

        CMString py_name;
        CMString write_hash;
        if (!entries.empty() && !paths.base_path_.empty()) {
            FlyBufferPtr entry_raw = do_read_raw_entries(entries, paths);
            if (entry_raw && !entry_raw->empty()) {
                DecompressingStreamBuf dsbuf(entry_raw->data(), entry_raw->size());
                py_name = dsbuf.py_name();
            }
            write_hash = entries.back().write_context_hash_;
        }
        return {true, raw, std::move(py_name), std::move(write_hash), false};
    }

    auto info = lookup_remote_idx(object_name);
    DBG("[TIER2] remote_idx lookup: obj={}, worker_id={}, host={}", object_name, info.worker_id_, info.host_);
    if (info.worker_id_ != 0 && !info.host_.empty()) {
        DirectCompressedReadCallback cb;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            cb = direct_compressed_read_handler_;
        }
        if (cb) {
            auto [cb_found, cb_data, cb_py_name, cb_hash] = cb(info.host_, info.port_, object_name);
            DBG("[TIER2] obj={}, cb_found={}", object_name, cb_found);
            if (cb_found) return {true, cb_data, std::move(cb_py_name), std::move(cb_hash), false};
            remove_remote_location(object_name, info.worker_id_);
        }
    }

    RemoteCompressedReadCallback remote_cb;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        remote_cb = remote_compressed_read_handler_;
    }
    if (remote_cb) {
        auto [cb_found, cb_data, cb_py_name, cb_can_still_produce] = remote_cb(object_name);
        DBG("[TIER3] obj={}, found={}, can_produce={}", object_name, cb_found, cb_can_still_produce);
        if (cb_found && cb_data && !cb_data->empty()) {
            return {true, cb_data, std::move(cb_py_name), {}, false};
        }
        return {false, nullptr, {}, {}, cb_can_still_produce};
    }

    DBG("[TIER3] obj={}, no remote_cb", object_name);
    return {false, nullptr, {}, {}, false};
}

// ============================================================
// Data Server (independent data transfer network layer)
// ============================================================

void DataService::start_data_server(const CMString& host, int port, int io_thread_count) {
    data_server_ = CMMakeUnique<DataServer>(*this, io_thread_count);
    data_server_->start(host, port);
}

void DataService::stop_data_server() {
    if (data_server_) {
        data_server_->stop();
        data_server_.reset();
    }
}

int DataService::get_data_port() const {
    if (data_server_) {
        return data_server_->get_port();
    }
    return 0;
}

CMString DataService::get_write_context_hash(const CMString& object_name) const {
    auto [db_id, short_name] = split_full(object_name);
    std::lock_guard<std::mutex> lock(mutex_);
    auto db_it = local_idx_.find(db_id);
    if (db_it != local_idx_.end()) {
        auto it = db_it->second.find(short_name);
        if (it != db_it->second.end() && it->second && !it->second->entries_.empty()) {
            return it->second->entries_.back().write_context_hash_;
        }
    }
    return {};
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

void DataService::on_temp_write_started(const CMString& db_id, const CMString& object_name) {
    if (!temp_eviction_store_) {
        temp_max_bytes_ = Config::instance()->get_int("temp_store_size");
        if (temp_max_bytes_ <= 0) temp_max_bytes_ = 2147483648LL;
        temp_eviction_store_ = CMMakeUnique<fly::TempStore>(temp_max_bytes_);
    }

    auto [_, short_name] = split_full(object_name);
    CMSharedPtr<LocalObjectInfo> info = CMMakeShared<LocalObjectInfo>();
    info->db_id_ = db_id;
    info->is_temp_ = true;
    info->completion_state_ = CompletionState::INCOMPLETE;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        local_idx_[db_id][short_name] = info;
    }

    DBG("[TEMP-WRITE-STARTED] obj={}, db_id={}", object_name, db_id);
}

void DataService::on_temp_write(const CMString& db_id, const CMString& object_name, FlyBufferPtr compressed_data) {
    if (!temp_eviction_store_) {
        temp_max_bytes_ = Config::instance()->get_int("temp_store_size");
        if (temp_max_bytes_ <= 0) temp_max_bytes_ = 2147483648LL;
        temp_eviction_store_ = CMMakeUnique<fly::TempStore>(temp_max_bytes_);
    }

    int64_t data_size = compressed_data ? static_cast<int64_t>(compressed_data->size()) : 0;
    auto [_, short_name] = split_full(object_name);

    CMSharedPtr<LocalObjectInfo> info;
    {
        std::lock_guard<std::mutex> lock(mutex_);

        auto& db_map = local_idx_[db_id];
        auto it = db_map.find(short_name);
        if (it == db_map.end() || !it->second) {
            ERR("[TEMP-WRITE] on_temp_write: no entry found for obj={}, db_id={}", object_name, db_id);
            return;
        }
        info = it->second;

        if (info->is_temp_ && info->temp_compressed_data_) {
            int64_t old_size = static_cast<int64_t>(info->temp_compressed_data_->size());
            temp_total_bytes_ -= old_size;
            auto lru_it = std::find(temp_lru_order_.begin(), temp_lru_order_.end(), object_name);
            if (lru_it != temp_lru_order_.end()) temp_lru_order_.erase(lru_it);
        }

        info->temp_compressed_data_ = std::move(compressed_data);
        info->completion_state_ = CompletionState::COMPLETE;

        temp_lru_order_.push_back(object_name);
        temp_total_bytes_ += data_size;

        DBG("[TEMP-WRITE] on_temp_write complete: obj={}, db_id={}, data_size={}, lru_count={}",
            object_name, db_id, data_size, temp_lru_order_.size());

        while (temp_total_bytes_ > temp_max_bytes_ && temp_lru_order_.size() > 1) {
            CMString oldest = temp_lru_order_.front();
            temp_lru_order_.erase(temp_lru_order_.begin());

            auto [old_db_id, old_short_name] = split_full(oldest);
            auto old_db_it = local_idx_.find(old_db_id);
            if (old_db_it != local_idx_.end()) {
                auto old_ent = old_db_it->second.find(old_short_name);
                if (old_ent != old_db_it->second.end() && old_ent->second && old_ent->second->is_temp_
                    && old_ent->second->temp_compressed_data_) {
                    int64_t freed = static_cast<int64_t>(old_ent->second->temp_compressed_data_->size());
                    temp_eviction_store_->put(oldest,
                        CMString(old_ent->second->temp_compressed_data_->data(),
                                 old_ent->second->temp_compressed_data_->size()));
                    old_ent->second->temp_compressed_data_.reset();
                    temp_total_bytes_ -= freed;
                }
            }
        }
    }

    {
        std::lock_guard<std::mutex> cv_lock(info->cv_mutex_);
    }
    info->cv_.notify_all();
}

void DataService::cleanup_temp_entries(const CMString& db_id) {
    CMVector<CMString> names_to_clean;
    int64_t freed_bytes = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto db_it = local_idx_.find(db_id);
        if (db_it == local_idx_.end()) return;
        for (auto it = db_it->second.begin(); it != db_it->second.end();) {
            if (it->second && it->second->is_temp_) {
                freed_bytes += it->second->temp_compressed_data_ ? static_cast<int64_t>(it->second->temp_compressed_data_->size()) : 0;
                names_to_clean.push_back(db_id + ":" + it->first);
                it = db_it->second.erase(it);
            } else {
                ++it;
            }
        }
    }

    if (!names_to_clean.empty()) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& name : names_to_clean) {
            auto lru_it = std::find(temp_lru_order_.begin(), temp_lru_order_.end(), name);
            if (lru_it != temp_lru_order_.end()) {
                temp_lru_order_.erase(lru_it);
            }
            if (temp_eviction_store_) {
                temp_eviction_store_->remove(name);
            }
        }
        temp_total_bytes_ -= freed_bytes;
        if (temp_total_bytes_ < 0) temp_total_bytes_ = 0;
    }
}

// ============================================================
// Auto-Backup Access Tracking (inline in remote_idx_)
// ============================================================

void DataService::record_remote_access(const CMString& object_name) {
    auto [db_id, short_name] = split_full(object_name);
    std::lock_guard<std::mutex> lock(mutex_);
    auto db_it = remote_idx_.find(db_id);
    if (db_it == remote_idx_.end()) return;
    auto obj_it = db_it->second.find(short_name);
    if (obj_it == db_it->second.end()) return;
    auto& meta = obj_it->second;
    meta.read_count_++;
    meta.last_access_time_ = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

BackupDecision DataService::evaluate_auto_backup(const CMString& object_name,
                                                   uint64_t threshold,
                                                   uint32_t target_replicas) const {
    auto [db_id, short_name] = split_full(object_name);
    std::lock_guard<std::mutex> lock(mutex_);
    BackupDecision decision;
    decision.target_replicas_ = target_replicas;
    decision.read_count_ = 0;
    decision.current_replicas_ = 0;

    auto db_it = remote_idx_.find(db_id);
    if (db_it == remote_idx_.end()) {
        DBG("[AUTO-BACKUP] evaluate: obj={}, db_id={} not found in remote_idx", object_name, db_id);
        return decision;
    }
    auto obj_it = db_it->second.find(short_name);
    if (obj_it == db_it->second.end()) {
        DBG("[AUTO-BACKUP] evaluate: obj={}, short_name={} not found in remote_idx", object_name, short_name);
        return decision;
    }

    const auto& meta = obj_it->second;
    decision.current_replicas_ = static_cast<uint32_t>(meta.workers_.size());
    decision.read_count_ = meta.read_count_;
    decision.should_backup_ = (meta.read_count_ >= threshold) && (meta.workers_.size() < target_replicas);

    DBG("[AUTO-BACKUP] evaluate: obj={}, read_count={}, workers_size={}, threshold={}, target={}, should_backup={}",
        object_name, meta.read_count_, meta.workers_.size(), threshold, target_replicas, decision.should_backup_);

    return decision;
}

void DataService::decay_remote_access(int64_t protection_seconds, int decay_factor_percent) {
    std::lock_guard<std::mutex> lock(mutex_);
    int64_t current_time = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    for (auto& [db_id, objects] : remote_idx_) {
        for (auto it = objects.begin(); it != objects.end();) {
            auto& meta = it->second;
            int64_t age = current_time - meta.last_access_time_;
            if (meta.read_count_ > 0 && age >= protection_seconds) {
                meta.read_count_ = meta.read_count_ * static_cast<uint64_t>(decay_factor_percent) / 100u;
            }
            ++it;
        }
    }
}

uint64_t DataService::get_access_read_count(const CMString& object_name) const {
    auto [db_id, short_name] = split_full(object_name);
    std::lock_guard<std::mutex> lock(mutex_);
    auto db_it = remote_idx_.find(db_id);
    if (db_it == remote_idx_.end()) return 0;
    auto obj_it = db_it->second.find(short_name);
    if (obj_it == db_it->second.end()) return 0;
    return obj_it->second.read_count_;
}

}  // namespace fly
