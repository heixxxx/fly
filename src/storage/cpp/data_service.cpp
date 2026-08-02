#include <storage/cpp/data_service.h>
#include <storage/cpp/data_server.h>
#include <storage/cpp/data_reader.h>
#include <storage/cpp/temp_store.h>
#include <storage/cpp/compressor.h>
#include <storage/cpp/decompressing_streambuf.h>
#include <storage/cpp/db_meta.h>
#include <storage/cpp/object_cache.h>
#include <network/cpp/net_quality_monitor.h>
#include <serialization/cpp/object_header.h>
#include <serialization/cpp/serialization_macros.h>
#include <core/cpp/config.h>
#include <log/cpp/logger.h>
#include <chrono>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <utility>
#include <cstring>
#include <random>

namespace fly {

namespace {
// Thread-local RNG for backoff jitter. Seeded once per thread; safe under the
// multi-threaded TIER2 retry loop.
thread_local std::mt19937 g_backoff_rng{std::random_device{}()};
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
    migrated_db_paths_.clear();
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

void DataService::register_database(const CMString& db_path,
                                     const CMString& data_path,
                                     const CMString& writer_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    db_paths_[db_path] = {db_path, data_path, writer_id};
}

void DataService::unregister_database(const CMString& db_path) {
    std::lock_guard<std::mutex> lock(mutex_);
    db_paths_.erase(db_path);
}

bool DataService::has_database(const CMString& db_path) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return db_paths_.find(db_path) != db_paths_.end();
}

// ============================================================
// DB Migration Redirect
// ============================================================

namespace {
// 读取 {db_path}/_MIGRATED_TO 的 MigrationHeader。不存在或解析失败返回空 target。
MigrationHeader read_migration_marker(const CMString& db_path) {
    MigrationHeader header;
    CMString meta_path = db_path + "/_MIGRATED_TO";
    if (!std::filesystem::exists(meta_path)) return header;

    std::ifstream ifs(meta_path, std::ios::binary);
    if (!ifs.is_open()) return header;

    int64_t size = 0;
    ifs.read(reinterpret_cast<char*>(&size), sizeof(size));
    if (!ifs || size <= 0) return header;

    CMString bytes(static_cast<size_t>(size), '\0');
    ifs.read(bytes.data(), size);
    if (!ifs) return header;

    try {
        FLY_DECODE(bytes, MigrationHeader, header);
    } catch (...) {
        header = MigrationHeader{};
    }
    return header;
}
}  // namespace

CMString DataService::resolve_migrated_path(const CMString& db_path) {
    // 1. 查缓存。
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = migrated_db_paths_.find(db_path);
        if (it != migrated_db_paths_.end()) {
            return it->second;
        }
    }

    // 2. miss → stat _MIGRATED_TO，链式展平 A→B→C。
    //    用本地 visited 防环（理论上不应出现，防御性）。
    CMString resolved = db_path;
    CMVector<CMString> visited;
    while (true) {
        if (std::find(visited.begin(), visited.end(), resolved) != visited.end()) {
            ERR("resolve_migrated_path: cycle detected at '{}', stopping", resolved);
            break;
        }
        visited.push_back(resolved);

        auto header = read_migration_marker(resolved);
        if (header.target_db_path_.empty()) {
            break;  // 无迁移文件，resolved 即最终值
        }
        resolved = header.target_db_path_;
    }

    // 3. 缓存最终结果（含"无迁移"——resolved==db_path 也缓存，避免重复 stat）。
    {
        std::lock_guard<std::mutex> lock(mutex_);
        migrated_db_paths_[db_path] = resolved;
    }
    return resolved;
}

CMString DataService::read_migrated_data_path(const CMString& db_path) {
    // 读源 db_path 的 _MIGRATED_TO，返回 target_data_path。
    // 链式迁移：跟随到最终 target 的 data_path。
    CMString current = db_path;
    CMVector<CMString> visited;
    while (true) {
        if (std::find(visited.begin(), visited.end(), current) != visited.end()) break;
        visited.push_back(current);
        auto header = read_migration_marker(current);
        if (header.target_db_path_.empty()) {
            // current 无迁移文件。若 current == db_path（无迁移），返回空；
            // 否则返回最后一次迁移的 target_data_path（已在上一轮 header 里）。
            break;
        }
        // 记下本轮 target_data_path，继续跟随
        if (header.target_db_path_ == current) break;  // 自环防御
        current = header.target_db_path_;
        // 检查 target 是否还有进一步迁移；若无，header.target_data_path_ 就是最终值
        auto next_header = read_migration_marker(current);
        if (next_header.target_db_path_.empty()) {
            return header.target_data_path_;
        }
    }
    // 回退：直接读 db_path 的 marker 取 data_path
    auto h = read_migration_marker(db_path);
    return h.target_data_path_;
}

void DataService::set_migrated_path(const CMString& source_db_path,
                                     const CMString& target_db_path) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (target_db_path.empty()) {
        migrated_db_paths_.erase(source_db_path);
    } else {
        migrated_db_paths_[source_db_path] = target_db_path;
    }
}

void DataService::write_migration_marker(const CMString& source_db_path,
                                          const CMString& target_db_path,
                                          const CMString& target_data_path) {
    MigrationHeader header;
    header.target_db_path_ = target_db_path;
    header.target_data_path_ = target_data_path;
    header.migrated_at_ = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    CMString encoded;
    FLY_ENCODE(header, encoded);

    CMString meta_path = source_db_path + "/_MIGRATED_TO";
    std::ofstream ofs(meta_path, std::ios::binary);
    if (!ofs.is_open()) {
        ERR("Failed to open _MIGRATED_TO for writing: {}", meta_path);
        return;
    }
    int64_t size = static_cast<int64_t>(encoded.size());
    ofs.write(reinterpret_cast<const char*>(&size), sizeof(size));
    ofs.write(encoded.data(), static_cast<std::streamsize>(encoded.size()));
    ofs.close();

    INFO("Wrote _MIGRATED_TO: source={}, target_base={}, target_data={}",
         source_db_path, target_db_path, target_data_path);
}

// ============================================================
// Local Index Management
// ============================================================

void DataService::on_object_written(const CMString& db_path,
                                     const CMString& object_name,
                                     const IndexEntry& entry) {
    auto [_, short_name] = split_full(object_name);
    std::lock_guard<std::mutex> lock(mutex_);
    auto& info = local_idx_[db_path].objects_[short_name];
    if (!info) {
        info = CMMakeShared<LocalObjectInfo>();
    }
    info->db_path_ = db_path;
    info->entries_.push_back(entry);
    info->flushed_ = false;
    info->completion_state_.store(CompletionState::COMPLETE, std::memory_order_release);
}

void DataService::on_flush(const CMString& db_path) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto db_it = local_idx_.find(db_path);
    if (db_it == local_idx_.end()) return;
    for (auto& [name, info] : db_it->second.objects_) {
        if (info) {
            info->flushed_ = true;
        }
    }
    // 持锁 notify：cv 在 map value (DbLocalIndex) 内，锁外访问其引用有
    // use-after-free 风险（db 条目可能被 merge 删除）。notify 本身不获取锁，
    // 不会死锁；waiter 唤醒后重新抢 mutex_ 本就是串行的，convoy 影响可忽略。
    db_it->second.write_cv_.notify_all();
}

void DataService::on_write_started(const CMString& db_path,
                                     const CMString& object_name) {
    auto [_, short_name] = split_full(object_name);
    CMSharedPtr<LocalObjectInfo> info = CMMakeShared<LocalObjectInfo>();
    info->db_path_ = db_path;
    info->completion_state_.store(CompletionState::INCOMPLETE, std::memory_order_relaxed);

    std::lock_guard<std::mutex> lock(mutex_);
    local_idx_[db_path].objects_[short_name] = info;
}

void DataService::on_write_completed(const CMString& db_path,
                                      const CMString& object_name,
                                      const CMVector<IndexEntry>& entries) {
    auto [_, short_name] = split_full(object_name);
    std::lock_guard<std::mutex> lock(mutex_);
    auto db_it = local_idx_.find(db_path);
    if (db_it == local_idx_.end()) return;
    auto it = db_it->second.objects_.find(short_name);
    if (it == db_it->second.objects_.end() || !it->second) return;
    it->second->entries_ = entries;
    it->second->completion_state_.store(CompletionState::COMPLETE, std::memory_order_release);
    db_it->second.write_cv_.notify_all();
}

void DataService::on_write_failed(const CMString& db_path,
                                    const CMString& object_name,
                                    const CMString& error_message) {
    auto [_, short_name] = split_full(object_name);
    std::lock_guard<std::mutex> lock(mutex_);
    auto db_it = local_idx_.find(db_path);
    if (db_it == local_idx_.end()) return;
    auto it = db_it->second.objects_.find(short_name);
    if (it == db_it->second.objects_.end() || !it->second) return;
    it->second->completion_state_.store(CompletionState::FAILED, std::memory_order_release);
    it->second->error_message_ = error_message;
    db_it->second.objects_.erase(it);
    // FAILED 也 notify：等待 INCOMPLETE→终态的 reader 被唤醒（predicate 返回 true，
    // 重查为 FAILED → 返回 false 走 TIER2 兜底）。
    db_it->second.write_cv_.notify_all();
}

void DataService::remove_local_index(const CMString& object_name) {
    auto [db_path, short_name] = split_full(object_name);
    int64_t freed_bytes = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto db_it = local_idx_.find(db_path);
        if (db_it != local_idx_.end()) {
            auto it = db_it->second.objects_.find(short_name);
            if (it != db_it->second.objects_.end() && it->second && it->second->is_temp_) {
                freed_bytes = it->second->temp_compressed_data_ ? static_cast<int64_t>(it->second->temp_compressed_data_->size()) : 0;
            }
            db_it->second.objects_.erase(short_name);
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

void DataService::clear_local_index_for_db(const CMString& db_path) {
    std::lock_guard<std::mutex> lock(mutex_);
    local_idx_.erase(db_path);
    DBG("clear_local_index_for_db: cleared local_idx for db_path={}", db_path);
}

void DataService::clear_remote_index_for_db(const CMString& db_path) {
    std::lock_guard<std::mutex> lock(mutex_);
    remote_idx_.erase(db_path);
    DBG("clear_remote_index_for_db: cleared remote_idx for db_path={}", db_path);
}

bool DataService::has_local_object(const CMString& object_name) const {
    auto [db_path, short_name] = split_full(object_name);
    std::lock_guard<std::mutex> lock(mutex_);
    auto db_it = local_idx_.find(db_path);
    if (db_it == local_idx_.end()) return false;
    auto it = db_it->second.objects_.find(short_name);
    return it != db_it->second.objects_.end() && it->second &&
           it->second->completion_state_.load(std::memory_order_acquire) == CompletionState::COMPLETE;
}

void DataService::on_object_flushed(const CMString& object_name) {
    auto [db_path, short_name] = split_full(object_name);
    std::lock_guard<std::mutex> lock(mutex_);
    auto db_it = local_idx_.find(db_path);
    if (db_it == local_idx_.end()) return;
    auto it = db_it->second.objects_.find(short_name);
    if (it == db_it->second.objects_.end() || !it->second) return;
    it->second->flushed_ = true;
    db_it->second.write_cv_.notify_all();
}

void DataService::restore_entries(const CMString& db_path,
                                    const CMVector<IndexEntry>& entries) {
    CMUnorderedMap<CMString, CMVector<IndexEntry>> grouped;
    for (const auto& e : entries) {
        auto [entry_db_path, short_name] = split_full(e.object_name_);
        const CMString& key = entry_db_path.empty() ? e.object_name_ : short_name;
        grouped[key].push_back(e);
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto& db_map = local_idx_[db_path].objects_;
    for (auto& [short_name, obj_entries] : grouped) {
        auto& info = db_map[short_name];
        if (!info) {
            info = CMMakeShared<LocalObjectInfo>();
        }
        info->db_path_ = db_path;
        for (auto& e : obj_entries) {
            info->entries_.push_back(std::move(e));
        }
        info->completion_state_.store(CompletionState::COMPLETE, std::memory_order_release);
        info->flushed_ = true;
    }

    if (!grouped.empty()) {
        DBG("restore_entries: restored {} objects for db_path={}", grouped.size(), db_path);
    }
}

std::optional<CMVector<IndexEntry>> DataService::find_local_entries(const CMString& object_name) const {
    auto [db_path, short_name] = split_full(object_name);
    std::lock_guard<std::mutex> lock(mutex_);
    auto db_it = local_idx_.find(db_path);
    if (db_it == local_idx_.end()) return std::nullopt;
    auto it = db_it->second.objects_.find(short_name);
    if (it == db_it->second.objects_.end() || !it->second) return std::nullopt;
    return it->second->entries_;
}

// ============================================================
// Remote Index Management
// ============================================================

void DataService::update_remote_idx(const CMString& object_name,
                                      uint64_t worker_id,
                                      const CMString& host,
                                      int32_t port,
                                      int64_t size_bytes) {
    register_worker(worker_id, host, port);
    add_remote_location(object_name, worker_id);
    if (size_bytes > 0) {
        auto [db_path, short_name] = split_full(object_name);
        std::lock_guard<std::mutex> lock(mutex_);
        remote_idx_[db_path][short_name].size_bytes_ = size_bytes;
    }
}

int64_t DataService::get_remote_size(const CMString& object_name) const {
    auto [db_path, short_name] = split_full(object_name);
    std::lock_guard<std::mutex> lock(mutex_);
    auto db_it = remote_idx_.find(db_path);
    if (db_it != remote_idx_.end()) {
        auto it = db_it->second.find(short_name);
        if (it != db_it->second.end()) {
            return it->second.size_bytes_;
        }
    }
    return 0;
}

void DataService::add_remote_location(const CMString& object_name, uint64_t worker_id) {
    auto [db_path, short_name] = split_full(object_name);
    std::lock_guard<std::mutex> lock(mutex_);
    auto& meta = remote_idx_[db_path][short_name];
    if (std::find(meta.workers_.begin(), meta.workers_.end(), worker_id) == meta.workers_.end()) {
        meta.workers_.push_back(worker_id);
    }
}

void DataService::remove_remote_location(const CMString& object_name) {
    auto [db_path, short_name] = split_full(object_name);
    std::lock_guard<std::mutex> lock(mutex_);
    auto db_it = remote_idx_.find(db_path);
    if (db_it != remote_idx_.end()) {
        db_it->second.erase(short_name);
    }
}

void DataService::remove_remote_location(const CMString& object_name, uint64_t worker_id) {
    auto [db_path, short_name] = split_full(object_name);
    std::lock_guard<std::mutex> lock(mutex_);
    auto db_it = remote_idx_.find(db_path);
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
    auto [db_path, short_name] = split_full(object_name);
    std::lock_guard<std::mutex> lock(mutex_);
    auto db_it = remote_idx_.find(db_path);
    if (db_it != remote_idx_.end()) {
        auto it = db_it->second.find(short_name);
        if (it != db_it->second.end()) {
            return it->second.workers_;  // 拷贝（锁内完成），调用方拿独立副本
        }
    }
    return {};
}

bool DataService::has_remote_location(const CMString& object_name) const {
    auto [db_path, short_name] = split_full(object_name);
    std::lock_guard<std::mutex> lock(mutex_);
    auto db_it = remote_idx_.find(db_path);
    if (db_it != remote_idx_.end()) {
        auto it = db_it->second.find(short_name);
        return it != db_it->second.end() && !it->second.workers_.empty();
    }
    return false;
}

RemoteObjectInfo DataService::lookup_remote_idx(const CMString& object_name) const {
    auto [db_path, short_name] = split_full(object_name);
    std::lock_guard<std::mutex> lock(mutex_);
    auto db_it = remote_idx_.find(db_path);
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

CMVector<RemoteObjectInfo> DataService::lookup_all_remote_idx(const CMString& object_name) const {
    auto [db_path, short_name] = split_full(object_name);
    std::lock_guard<std::mutex> lock(mutex_);
    CMVector<RemoteObjectInfo> out;
    auto db_it = remote_idx_.find(db_path);
    if (db_it == remote_idx_.end()) return out;
    auto it = db_it->second.find(short_name);
    if (it == db_it->second.end()) return out;
    out.reserve(it->second.workers_.size());
    for (uint64_t wid : it->second.workers_) {
        auto wit = worker_registry_.find(wid);
        if (wit != worker_registry_.end()) {
            out.push_back(wit->second);
        }
    }
    return out;
}

void DataService::remove_remote_index(const CMString& object_name) {
    auto [db_path, short_name] = split_full(object_name);
    std::lock_guard<std::mutex> lock(mutex_);
    auto db_it = remote_idx_.find(db_path);
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

CMVector<RemoteObjectInfo> DataService::get_all_workers() const {
    std::lock_guard<std::mutex> lock(mutex_);
    CMVector<RemoteObjectInfo> out;
    out.reserve(worker_registry_.size());
    for (const auto& [wid, info] : worker_registry_) {
        out.push_back(info);
    }
    return out;
}

// ============================================================
// Private Helpers — Name Parsing
// ============================================================

std::pair<CMString, CMString> DataService::split_full(const CMString& full) {
    // full_name = "db_path:short"，split 用 rfind(':')。
    // 用 rfind(':') 切分 —— short_name 不含 ':'，最后一个 ':' 必是分隔符。
    auto pos = full.rfind(':');
    if (pos == CMString::npos) {
        return {CMString{}, full};
    }
    return {full.substr(0, pos), full.substr(pos + 1)};
}

CMString DataService::get_db_path_for_object(const CMString& object_name) const {
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
    DataReader reader(paths.db_path_, paths.data_path_, paths.writer_id_);
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
    auto [db_path, short_name] = split_full(object_name);
    CMVector<IndexEntry> entries;
    DbPaths paths;
    bool is_temp = false;
    FlyBufferPtr temp_data;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto db_it = local_idx_.find(db_path);
        if (db_it == local_idx_.end()) return {false, ReadResult{}};
        auto it = db_it->second.objects_.find(short_name);
        if (it == db_it->second.objects_.end() || !it->second) {
            return {false, ReadResult{}};
        }
        auto& info = *it->second;
        if (info.completion_state_.load(std::memory_order_acquire) != CompletionState::COMPLETE || !info.flushed_) {
            return {false, ReadResult{}};
        }

        if (info.is_temp_) {
            is_temp = true;
            temp_data = info.temp_compressed_data_;
        } else {
            entries = info.entries_;

            auto path_it = db_paths_.find(db_path);
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

std::pair<bool, FlyBufferPtr> DataService::try_read_local_raw(const CMString& object_name,
                                                               bool wait_local_write) {
    // Short-circuit: serve compressed bytes from the ObjectCache low tier when
    // available. This benefits both the remote DataServer serve path (which
    // calls this directly) and the local read_raw_compressed Tier-1 path —
    // avoiding index lookup + disk IO entirely on a cache hit.
    if (auto [hit, cached] = fly::ObjectCache::instance().get_low(object_name); hit) {
        return {true, cached};
    }

    auto [db_path, short_name] = split_full(object_name);
    CMVector<IndexEntry> entries;
    DbPaths paths;
    bool is_temp = false;
    FlyBufferPtr temp_data;

    // 锁内查找并填充读取所需字段。返回 diag：
    //   0=not_found_db/no_path, 1=not_found_obj, 2=not_ready(INCOMPLETE/FAILED), 3=found
    auto lookup_under_lock = [&]() -> int {
        auto db_it = local_idx_.find(db_path);
        if (db_it == local_idx_.end()) return 0;
        auto it = db_it->second.objects_.find(short_name);
        if (it == db_it->second.objects_.end() || !it->second) return 1;
        auto& info = *it->second;
        if (info.completion_state_.load(std::memory_order_acquire) != CompletionState::COMPLETE) {
            return 2;  // INCOMPLETE 或 FAILED
        }
        if (info.is_temp_) {
            is_temp = true;
            temp_data = info.temp_compressed_data_;
            return 3;
        }
        entries = info.entries_;
        auto path_it = db_paths_.find(db_path);
        if (path_it == db_paths_.end()) return 0;
        paths = path_it->second;
        return 3;
    };

    int diag = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        diag = lookup_under_lock();
    }

    // INCOMPLETE/FAILED 且调用方要求 wait 本地写完成：在 per-db cv 上 wait。
    // wait 期间 mutex_ 释放（cv.wait 原子地释放锁并阻塞），WriteBackQueue 线程
    // 能获锁设 COMPLETE/FAILED 并 notify_all 唤醒。predicate 用 atomic acquire 读，
    // 确保看到 writer 的 release 写。无限等待（信任本地写最终完成）。
    if (diag == 2 && wait_local_write) {
        DBG("[TIER1-WAIT] obj={} INCOMPLETE, waiting for local write completion", object_name);
        std::unique_lock<std::mutex> lk(mutex_);
        auto db_it = local_idx_.find(db_path);
        if (db_it != local_idx_.end()) {
            // 找到对象引用，wait 其 completion_state_ 脱离 INCOMPLETE。
            auto it = db_it->second.objects_.find(short_name);
            if (it != db_it->second.objects_.end() && it->second) {
                CMSharedPtr<LocalObjectInfo> info = it->second;  // 拷贝 shared_ptr 防悬空
                db_it->second.write_cv_.wait(lk, [&] {
                    return info->completion_state_.load(std::memory_order_acquire)
                           != CompletionState::INCOMPLETE;
                });
                // 唤醒后重查：重新填充读取字段。lk 仍持有（wait 返回时已重新获锁）。
                is_temp = false;
                temp_data.reset();
                entries.clear();
                diag = lookup_under_lock();
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
    auto [db_path, short_name] = split_full(object_name);
    std::lock_guard<std::mutex> lock(mutex_);
    auto db_it = local_idx_.find(db_path);
    if (db_it == local_idx_.end()) return false;
    auto it = db_it->second.objects_.find(short_name);
    if (it == db_it->second.objects_.end() || !it->second) return false;
    return it->second->completion_state_.load(std::memory_order_acquire) == CompletionState::INCOMPLETE;
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

std::tuple<bool, FlyBufferPtr, CMString, CMString, bool> DataService::read_raw_compressed(const CMString& object_name) {
    auto [found, raw] = try_read_local_raw(object_name);
    if (found) {
        auto [db_path, short_name] = split_full(object_name);
        bool is_temp_entry = false;
        DbPaths paths;
        CMVector<IndexEntry> entries;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto db_it = local_idx_.find(db_path);
            if (db_it != local_idx_.end()) {
                auto it = db_it->second.objects_.find(short_name);
                if (it != db_it->second.objects_.end() && it->second) {
                    is_temp_entry = it->second->is_temp_;
                    if (!is_temp_entry) {
                        entries = it->second->entries_;
                    }
                }
            }
            auto path_it = db_paths_.find(db_path);
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
        if (!entries.empty() && !paths.db_path_.empty()) {
            FlyBufferPtr entry_raw = do_read_raw_entries(entries, paths);
            if (entry_raw && !entry_raw->empty()) {
                DecompressingStreamBuf dsbuf(entry_raw->data(), entry_raw->size());
                py_name = dsbuf.py_name();
            }
            write_hash = entries.back().write_context_hash_;
        }
        return {true, raw, std::move(py_name), std::move(write_hash), false};
    }

    // ── TIER2/TIER3 orchestration ──
    // TIER2: iterate every known replica (local remote_idx) once per round with
    // exponential backoff. On full failure, TIER3 queries master for ALL replica
    // locations, refreshes remote_idx, and re-enters TIER2 once (tier3_queried
    // guards against TIER2↔TIER3 bouncing). If TIER3 also has no location, fail.
    DirectCompressedReadCallback cb;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        cb = direct_compressed_read_handler_;
    }
    RemoteCompressedReadCallback remote_cb;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        remote_cb = remote_compressed_read_handler_;
    }

    constexpr int64_t kInitialDelayMs = 10;
    constexpr int64_t kMaxDelayMs = 500;
    constexpr auto kNetworkDeadline = std::chrono::seconds(30);

    bool tier3_queried = false;
    bool last_can_still_produce = false;

    while (true) {
        // ── TIER2 round ──
        if (cb) {
            int64_t delay_ms = kInitialDelayMs;
            auto net_start = std::chrono::steady_clock::now();
            bool tier2_done = false;

            while (!tier2_done) {
                auto replicas = lookup_all_remote_idx(object_name);
                if (replicas.empty()) {
                    break;  // no local replicas → need TIER3 to populate
                }

                bool saw_not_ready = false;
                // Prefer better-connected replicas first. stable_sort keeps the
                // registration order for equal scores, so unknown peers (score
                // 0) stay in their original position — behavior is unchanged
                // before any quality data is gathered. Disabled via config →
                // no-op (registration order preserved exactly as before).
                if (Config::instance()->get_int("net_probe_enabled")) {
                    std::stable_sort(replicas.begin(), replicas.end(),
                        [](const RemoteObjectInfo& a, const RemoteObjectInfo& b) {
                            return NetQualityMonitor::instance().score(a.host_) >
                                   NetQualityMonitor::instance().score(b.host_);
                        });
                }
                for (const auto& loc : replicas) {
                    auto [cb_found, cb_data, cb_py_name, cb_hash, cb_rerr] =
                        cb(loc.host_, loc.port_, object_name);
                    if (cb_found) {
                        DBG("[TIER2] obj={}, hit worker={}", object_name, loc.worker_id_);
                        return {true, cb_data, std::move(cb_py_name), std::move(cb_hash), false};
                    }
                    if (cb_rerr == ReadError::OBJECT_NOT_FOUND) {
                        remove_remote_location(object_name, loc.worker_id_);
                    } else if (cb_rerr == ReadError::DATA_NOT_READY) {
                        saw_not_ready = true;
                    } else if (cb_rerr == ReadError::SHUTDOWN) {
                        return {false, nullptr, {}, {}, false};
                    }
                    // NETWORK: transient, keep replica, retry next round.
                }

                // Round fully failed. DATA_NOT_READY → unbounded; else bound by
                // the network deadline.
                if (!saw_not_ready &&
                    std::chrono::steady_clock::now() - net_start >= kNetworkDeadline) {
                    tier2_done = true;
                    break;
                }

                int64_t jitter = std::max<int64_t>(1, delay_ms / 10);
                std::uniform_int_distribution<int64_t> dist(-jitter, jitter);
                int64_t actual = delay_ms + dist(g_backoff_rng);
                std::this_thread::sleep_for(std::chrono::milliseconds(actual));
                delay_ms = std::min(delay_ms * 2, kMaxDelayMs);
            }
        }

        // ── TIER2 exhausted: either no replicas or all rounds failed ──
        if (tier3_queried) {
            // Already queried master and retried TIER2 — give up.
            return {false, nullptr, {}, {}, last_can_still_produce};
        }
        if (!remote_cb) {
            // No TIER3 handler (e.g. master process): nothing more to do.
            return {false, nullptr, {}, {}, false};
        }

        // ── TIER3: pure location query ──
        auto [refreshed, csp] = remote_cb(object_name);
        tier3_queried = true;
        last_can_still_produce = csp;
        DBG("[TIER3] obj={}, refreshed={}, can_produce={}", object_name, refreshed, csp);
        if (!refreshed) {
            // Master has no location either — fail with master's verdict.
            return {false, nullptr, {}, {}, csp};
        }
        // Locations refreshed → loop back to re-enter TIER2 with the new replicas.
    }
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
    auto [db_path, short_name] = split_full(object_name);
    std::lock_guard<std::mutex> lock(mutex_);
    auto db_it = local_idx_.find(db_path);
    if (db_it != local_idx_.end()) {
        auto it = db_it->second.objects_.find(short_name);
        if (it != db_it->second.objects_.end() && it->second && !it->second->entries_.empty()) {
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

void DataService::clear_write_back() {
    if (write_back_queue_) {
        write_back_queue_->clear_pending();
    }
}

bool DataService::is_write_back_running() const {
    return write_back_queue_ && write_back_queue_->is_running();
}

void DataService::on_temp_write_started(const CMString& db_path, const CMString& object_name) {
    if (!temp_eviction_store_) {
        temp_max_bytes_ = Config::instance()->get_int("temp_store_size");
        if (temp_max_bytes_ <= 0) temp_max_bytes_ = 2147483648LL;
        temp_eviction_store_ = CMMakeUnique<fly::TempStore>(temp_max_bytes_);
    }

    auto [_, short_name] = split_full(object_name);
    CMSharedPtr<LocalObjectInfo> info = CMMakeShared<LocalObjectInfo>();
    info->db_path_ = db_path;
    info->is_temp_ = true;
    info->completion_state_.store(CompletionState::INCOMPLETE, std::memory_order_relaxed);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        local_idx_[db_path].objects_[short_name] = info;
    }

    DBG("[TEMP-WRITE-STARTED] obj={}, db_path={}", object_name, db_path);
}

void DataService::on_temp_write(const CMString& db_path, const CMString& object_name, FlyBufferPtr compressed_data) {
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

        auto& db_entry = local_idx_[db_path];
        auto it = db_entry.objects_.find(short_name);
        if (it == db_entry.objects_.end() || !it->second) {
            ERR("[TEMP-WRITE] on_temp_write: no entry found for obj={}, db_path={}", object_name, db_path);
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
        info->completion_state_.store(CompletionState::COMPLETE, std::memory_order_release);

        temp_lru_order_.push_back(object_name);
        temp_total_bytes_ += data_size;

        DBG("[TEMP-WRITE] on_temp_write complete: obj={}, db_path={}, data_size={}, lru_count={}",
            object_name, db_path, data_size, temp_lru_order_.size());

        while (temp_total_bytes_ > temp_max_bytes_ && temp_lru_order_.size() > 1) {
            CMString oldest = temp_lru_order_.front();
            temp_lru_order_.erase(temp_lru_order_.begin());

            auto [old_db_path, old_short_name] = split_full(oldest);
            auto old_db_it = local_idx_.find(old_db_path);
            if (old_db_it != local_idx_.end()) {
                auto old_ent = old_db_it->second.objects_.find(old_short_name);
                if (old_ent != old_db_it->second.objects_.end() && old_ent->second && old_ent->second->is_temp_
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

        // temp 写完成也 notify：等待 temp 对象的 reader 唤醒。
        db_entry.write_cv_.notify_all();
    }
}

void DataService::cleanup_temp_entries(const CMString& db_path) {
    CMVector<CMString> names_to_clean;
    int64_t freed_bytes = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto db_it = local_idx_.find(db_path);
        if (db_it == local_idx_.end()) return;
        for (auto it = db_it->second.objects_.begin(); it != db_it->second.objects_.end();) {
            if (it->second && it->second->is_temp_) {
                freed_bytes += it->second->temp_compressed_data_ ? static_cast<int64_t>(it->second->temp_compressed_data_->size()) : 0;
                names_to_clean.push_back(db_path + ":" + it->first);
                it = db_it->second.objects_.erase(it);
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
    auto [db_path, short_name] = split_full(object_name);
    std::lock_guard<std::mutex> lock(mutex_);
    auto db_it = remote_idx_.find(db_path);
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
    auto [db_path, short_name] = split_full(object_name);
    std::lock_guard<std::mutex> lock(mutex_);
    BackupDecision decision;
    decision.target_replicas_ = target_replicas;
    decision.read_count_ = 0;
    decision.current_replicas_ = 0;

    auto db_it = remote_idx_.find(db_path);
    if (db_it == remote_idx_.end()) {
        DBG("[AUTO-BACKUP] evaluate: obj={}, db_path={} not found in remote_idx", object_name, db_path);
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
    
    for (auto& [db_path, objects] : remote_idx_) {
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
    auto [db_path, short_name] = split_full(object_name);
    std::lock_guard<std::mutex> lock(mutex_);
    auto db_it = remote_idx_.find(db_path);
    if (db_it == remote_idx_.end()) return 0;
    auto obj_it = db_it->second.find(short_name);
    if (obj_it == db_it->second.end()) return 0;
    return obj_it->second.read_count_;
}

}  // namespace fly
