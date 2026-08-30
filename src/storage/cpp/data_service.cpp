#include <storage/cpp/data_service.h>
#include <storage/cpp/data_server.h>
#include <storage/cpp/data_reader.h>
#include <storage/cpp/memory_chunk_source.h>
#include <storage/cpp/disk_chunk_source.h>
#include <fcntl.h>
#include <unistd.h>
#include <storage/cpp/compressor.h>
#include <storage/cpp/decompressing_streambuf.h>
#include <storage/cpp/db_meta.h>
#include <storage/cpp/object_cache.h>
#include <common/cpp/worker_context.h>   // WorkerAgentContext::suggest_backup（maybe_suggest_backup 用）
#include <core/cpp/process_info.h>       // master 进程豁免 remove_remote_location（权威 remote_idx 保护）
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
    // 先在锁外停止后台服务：stop_data_server/drain_write_back/stop_write_back
    // 操作的是 data_server_/write_back_queue_（独立资源，不需要 DataService 的索引锁）。
    // 必须锁外执行：drain_write_back 会等 WBQ worker 跑完，而 WBQ worker 完成写时
    // 回调 on_write_completed 需要获取 mutex_ —— 若 reset 持锁调 drain，构成
    // AB-BA 死锁（reset 持锁等 worker，worker 等 lock）。
    stop_data_server();
    drain_write_back();
    stop_write_back();

    // 逐域清空（分片锁后无单一 mutex_，各域独立加写锁）。后台服务已在锁外停止，
    // 此时无并发写线程，逐域加锁仅为与可能并发的读线程建立 happens-before。
    {
        std::unique_lock<std::shared_mutex> lock(local_mutex_);
        local_idx_.clear();
    }
    {
        std::unique_lock<fly::WriterPrefRwLock> lock(remote_mutex_);
        remote_idx_.clear();
    }
    {
        std::unique_lock<std::shared_mutex> lock(worker_mutex_);
        worker_registry_.clear();
    }
    {
        std::unique_lock<std::shared_mutex> lock(db_paths_mutex_);
        db_paths_.clear();
        db_refs_.clear();
        migrated_db_paths_.clear();
    }
    {
        std::unique_lock<std::shared_mutex> lock(cb_mutex_);
        remote_compressed_read_handler_ = nullptr;
        direct_compressed_read_handler_ = nullptr;
    }
}

// ============================================================
// Database Registry
// ============================================================

void DataService::register_database(const CMString& db_path,
                                     const CMString& data_path,
                                     const CMString& writer_id) {
    std::unique_lock<std::shared_mutex> lock(db_paths_mutex_);
    // 引用计数（§4.7 缓存取消后的必要配套）：db chain find_db 等路径会临时
    // 构造 Database 实例（读一次即 GC）——实例析构不得移除仍有持有者的
    // db_paths_（否则 serve 侧 TIER1 查询 diag=0 NOT_FOUND——旧代码由
    // low-cache 进程级兜底掩盖，缓存取消后暴露）。
    db_refs_[db_path] += 1;
    db_paths_[db_path] = {db_path, data_path, writer_id};
}

void DataService::unregister_database(const CMString& db_path) {
    if (db_path.empty()) return;  // 被拒构造的 Database（db_path_ 已清空）
    std::unique_lock<std::shared_mutex> lock(db_paths_mutex_);
    auto it = db_refs_.find(db_path);
    if (it == db_refs_.end() || it->second <= 1) {
        // 最后一实例（或异常状态）：真正移除。
        db_refs_.erase(db_path);
        db_paths_.erase(db_path);
        return;
    }
    it->second -= 1;  // 仍有其他 Database 实例持有该 db
}

bool DataService::has_database(const CMString& db_path) const {
    std::shared_lock<std::shared_mutex> lock(db_paths_mutex_);
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
        std::shared_lock<std::shared_mutex> lock(db_paths_mutex_);
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
        std::unique_lock<std::shared_mutex> lock(db_paths_mutex_);
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
    std::unique_lock<std::shared_mutex> lock(db_paths_mutex_);
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
    std::unique_lock<std::shared_mutex> lock(local_mutex_);
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
    std::unique_lock<std::shared_mutex> lock(local_mutex_);
    auto db_it = local_idx_.find(db_path);
    if (db_it == local_idx_.end()) return;
    for (auto& [name, info] : db_it->second.objects_) {
        if (info) {
            info->flushed_ = true;
        }
    }
    // 持锁 notify：cv 在 map value (DbLocalIndex) 内，锁外访问其引用有
    // use-after-free 风险（db 条目可能被 merge 删除）。notify 本身不获取锁，
    // 不会死锁；waiter 唤醒后重新抢 local_mutex_ 本就是串行的，convoy 影响可忽略。
    db_it->second.write_cv_.notify_all();
}

void DataService::on_write_started(const CMString& db_path,
                                     const CMString& object_name) {
    auto [_, short_name] = split_full(object_name);

    std::unique_lock<std::shared_mutex> lock(local_mutex_);
    auto& objs = local_idx_[db_path].objects_;
    auto it = objs.find(short_name);
    if (it != objs.end() && it->second &&
        it->second->completion_state_.load(std::memory_order_acquire) == CompletionState::COMPLETE) {
        // Problem 3：对象已 COMPLETE（盘上数据完整 + entries_ 已就位）。重复写路径上
        // commit_write 在 register_write（重复检测）之前调用本方法 —— 若此处用新的
        // INCOMPLETE 无条件覆盖，会丢弃 entries_ 向量；随后 register_write 判定
        // WRITE_DUPLICATE_SKIPPED 触发 on_write_failed erase 条目，导致等待该对象的本地
        // 读取者掉落到 TIER2/远程（数据其实已完整在盘上）。保留 COMPLETE 条目不动即可。
        return;
    }
    CMSharedPtr<LocalObjectInfo> info = CMMakeShared<LocalObjectInfo>();
    info->db_path_ = db_path;
    info->completion_state_.store(CompletionState::INCOMPLETE, std::memory_order_relaxed);
    objs[short_name] = info;
}

void DataService::on_write_completed(const CMString& db_path,
                                      const CMString& object_name,
                                      const CMVector<IndexEntry>& entries) {
    auto [_, short_name] = split_full(object_name);
    std::unique_lock<std::shared_mutex> lock(local_mutex_);
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
    DBG("on_write_failed: db={}, obj={}, reason={}", db_path, object_name, error_message);
    std::unique_lock<std::shared_mutex> lock(local_mutex_);
    auto db_it = local_idx_.find(db_path);
    if (db_it == local_idx_.end()) return;
    auto it = db_it->second.objects_.find(short_name);
    if (it == db_it->second.objects_.end() || !it->second) return;
    it->second->completion_state_.store(CompletionState::FAILED, std::memory_order_release);
    db_it->second.objects_.erase(it);
    // FAILED 也 notify：等待 INCOMPLETE→终态的 reader 被唤醒（predicate 返回 true，
    // 重查为 FAILED → 返回 false 走 TIER2 兜底）。
    db_it->second.write_cv_.notify_all();
}

void DataService::remove_local_index(const CMString& object_name) {
    auto [db_path, short_name] = split_full(object_name);
    {
        std::unique_lock<std::shared_mutex> lock(local_mutex_);
        auto db_it = local_idx_.find(db_path);
        if (db_it != local_idx_.end()) {
            db_it->second.objects_.erase(short_name);
        }
    }
    // Invalidate cached bytes (low/high tier) for this object so subsequent
    // reads don't return stale data after removal.
    fly::ObjectCache::instance().remove(object_name);
}

void DataService::clear_local_index_for_db(const CMString& db_path) {
    std::unique_lock<std::shared_mutex> lock(local_mutex_);
    local_idx_.erase(db_path);
    DBG("clear_local_index_for_db: cleared local_idx for db_path={}", db_path);
}

void DataService::clear_remote_index_for_db(const CMString& db_path) {
    std::unique_lock<fly::WriterPrefRwLock> lock(remote_mutex_);
    remote_idx_.erase(db_path);
    DBG("clear_remote_index_for_db: cleared remote_idx for db_path={}", db_path);
}

bool DataService::has_local_object(const CMString& object_name) const {
    auto [db_path, short_name] = split_full(object_name);
    std::shared_lock<std::shared_mutex> lock(local_mutex_);
    auto db_it = local_idx_.find(db_path);
    if (db_it == local_idx_.end()) return false;
    auto it = db_it->second.objects_.find(short_name);
    return it != db_it->second.objects_.end() && it->second &&
           it->second->completion_state_.load(std::memory_order_acquire) == CompletionState::COMPLETE;
}

void DataService::on_object_flushed(const CMString& object_name) {
    auto [db_path, short_name] = split_full(object_name);
    std::unique_lock<std::shared_mutex> lock(local_mutex_);
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

    CMVector<CMString> touched_full_names;
    {
        std::unique_lock<std::shared_mutex> lock(local_mutex_);
        auto& db_map = local_idx_[db_path].objects_;
        for (auto& [short_name, obj_entries] : grouped) {
            auto& info = db_map[short_name];
            if (!info) {
                info = CMMakeShared<LocalObjectInfo>();
            }
            info->db_path_ = db_path;
            for (auto& e : obj_entries) {
                // 等价去重：同对象已有任一 entry 的 write_context_hash_ 与来者
                // 相同 → 字节等价（backup 副本 vs 接管/重载副本），跳过避免
                // entries_ 膨胀。hash 为空不判等价（无指纹，保守加载）。
                // 注意禁止按「对象已存在」无条件跳过——backup 之后源若重写，
                // 新版本 entry 必须加载，读路径按 entries.back() 选最新。
                if (!e.write_context_hash_.empty()) {
                    bool duplicate = false;
                    for (const auto& existing : info->entries_) {
                        if (existing.write_context_hash_ == e.write_context_hash_) {
                            duplicate = true;
                            break;
                        }
                    }
                    if (duplicate) {
                        DBG("restore_entries: skip equivalent entry obj={}:{} "
                            "(write_context_hash match)", db_path, short_name);
                        continue;
                    }
                }
                info->entries_.push_back(std::move(e));
            }
            info->completion_state_.store(CompletionState::COMPLETE, std::memory_order_release);
            info->flushed_ = true;
            touched_full_names.push_back(db_path + ":" + short_name);
        }
    }

    // 热路径 restore（同 host storage 接管）必须失效 ObjectCache：缓存里可能
    // 有该对象的旧字节（backup 时刻），cache hit 会绕过 entries.back() 的
    // 最新副本选优。集群重启的 load_db 无此问题（进程重启 cache 为空），
    // 失效对它是 no-op。先例：remove_remote_index 末尾同款清理。
    for (const auto& full : touched_full_names) {
        fly::ObjectCache::instance().remove(full);
    }

    if (!grouped.empty()) {
        DBG("restore_entries: restored {} objects for db_path={}", grouped.size(), db_path);
    }
}

void DataService::restore_temp_entries(const CMString& db_path,
                                       const CMVector<IndexEntry>& entries) {
    // temp 落盘恢复（task 级断点）：.temp.{wid}.idx load 后灌 local_idx_——
    // is_temp=true + entries_（盘读路径，2026-08-30 起 temp 无内存态）。
    // 对象名是 short_name（LocalIndex 不含 db_path 前缀）。
    CMVector<CMString> touched_full_names;
    {
        std::unique_lock<std::shared_mutex> lock(local_mutex_);
        auto& db_map = local_idx_[db_path].objects_;
        for (const auto& e : entries) {
            const CMString& short_name = e.object_name_;
            auto& info = db_map[short_name];
            if (!info) {
                info = CMMakeShared<LocalObjectInfo>();
            }
            info->db_path_ = db_path;
            info->is_temp_ = true;
            // 重放恢复等价去重（同 restore_entries 惯例）：同 write_context_hash
            // 跳过。temp 写入 hash 留空 → 保守全量加载（重写新版本必须可见）。
            if (!e.write_context_hash_.empty()) {
                bool duplicate = false;
                for (const auto& existing : info->entries_) {
                    if (existing.write_context_hash_ == e.write_context_hash_) {
                        duplicate = true;
                        break;
                    }
                }
                if (duplicate) continue;
            }
            info->entries_.push_back(e);
            info->completion_state_.store(CompletionState::COMPLETE, std::memory_order_release);
            info->flushed_ = true;
            touched_full_names.push_back(db_path + ":" + short_name);
        }
    }
    for (const auto& full : touched_full_names) {
        fly::ObjectCache::instance().remove(full);
    }
    if (!entries.empty()) {
        DBG("restore_temp_entries: restored {} temp objects for db_path={}",
            entries.size(), db_path);
    }
}

std::optional<CMVector<IndexEntry>> DataService::find_local_entries(const CMString& object_name) const {
    auto [db_path, short_name] = split_full(object_name);
    std::shared_lock<std::shared_mutex> lock(local_mutex_);
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
                                      int64_t size_bytes,
                                      bool storage_only) {
    // 三次独立加锁不嵌套（register_worker 用 worker 锁，add_remote_location/size
    // 用 remote 锁），与分片前语义等价，无死锁风险。
    register_worker(worker_id, host, port, storage_only);
    add_remote_location(object_name, worker_id);
    if (size_bytes > 0) {
        auto [db_path, short_name] = split_full(object_name);
        std::unique_lock<fly::WriterPrefRwLock> lock(remote_mutex_);
        remote_idx_[db_path][short_name].size_bytes_ = size_bytes;
    }
}

int64_t DataService::get_remote_size(const CMString& object_name) const {
    auto [db_path, short_name] = split_full(object_name);
    std::shared_lock<fly::WriterPrefRwLock> lock(remote_mutex_);
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
    std::unique_lock<fly::WriterPrefRwLock> lock(remote_mutex_);
    auto& meta = remote_idx_[db_path][short_name];
    if (std::find(meta.workers_.begin(), meta.workers_.end(), worker_id) == meta.workers_.end()) {
        meta.workers_.push_back(worker_id);
    }
}

void DataService::remove_remote_location(const CMString& object_name) {
    auto [db_path, short_name] = split_full(object_name);
    std::unique_lock<fly::WriterPrefRwLock> lock(remote_mutex_);
    auto db_it = remote_idx_.find(db_path);
    if (db_it != remote_idx_.end()) {
        db_it->second.erase(short_name);
    }
}

void DataService::remove_remote_location(const CMString& object_name, uint64_t worker_id) {
    // 权威 remote_idx 保护（用户确认语义）：master 进程的 remote_idx 是全集群唯一
    // 位置权威源（TIER3 应答 + 调度位置注入都读它）——读失败踢副本是 worker 本地
    // 视图的自愈行为，master 进程豁免：worker 断连/挂掉不代表数据消失（可能重连
    // 恢复），权威视图不因读失败被污染（否则 worker 恢复后 master 再也找不到数据）。
    // 显式 remove_object 路径走单参重载（全删），不受此保护影响。
    if (!ProcessInfo::instance()->worker_mode()) {
        return;
    }
    auto [db_path, short_name] = split_full(object_name);
    std::unique_lock<fly::WriterPrefRwLock> lock(remote_mutex_);
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
    std::shared_lock<fly::WriterPrefRwLock> lock(remote_mutex_);
    auto db_it = remote_idx_.find(db_path);
    if (db_it != remote_idx_.end()) {
        auto it = db_it->second.find(short_name);
        if (it != db_it->second.end()) {
            return it->second.workers_;  // 拷贝（锁内完成），调用方拿独立副本
        }
    }
    return {};
}

CMVector<CMString> DataService::get_objects_of_worker(uint64_t worker_id) const {
    CMVector<CMString> objects;
    std::shared_lock<fly::WriterPrefRwLock> lock(remote_mutex_);
    for (const auto& [db_path, objs] : remote_idx_) {
        for (const auto& [short_name, meta] : objs) {
            for (uint64_t holder : meta.workers_) {
                if (holder == worker_id) {
                    objects.push_back(db_path + ":" + short_name);
                    break;
                }
            }
        }
    }
    return objects;
}

CMUnorderedMap<uint64_t, int64_t> DataService::get_worker_bytes_batch(
    const CMUnorderedSet<uint64_t>& worker_ids) const {
    CMUnorderedMap<uint64_t, int64_t> bytes;
    if (worker_ids.empty()) return bytes;
    for (uint64_t wid : worker_ids) bytes[wid] = 0;
    std::shared_lock<fly::WriterPrefRwLock> lock(remote_mutex_);
    for (const auto& [db_path, objs] : remote_idx_) {
        for (const auto& [short_name, meta] : objs) {
            // size_bytes_ 是对象级（非 per-holder）：每个 holder 盘上各有一份
            // 字节，分别计入各自水位。
            for (uint64_t holder : meta.workers_) {
                auto it = bytes.find(holder);
                if (it != bytes.end()) {
                    it->second += meta.size_bytes_;
                }
            }
        }
    }
    return bytes;
}

bool DataService::has_remote_location(const CMString& object_name) const {
    auto [db_path, short_name] = split_full(object_name);
    std::shared_lock<fly::WriterPrefRwLock> lock(remote_mutex_);
    auto db_it = remote_idx_.find(db_path);
    if (db_it != remote_idx_.end()) {
        auto it = db_it->second.find(short_name);
        return it != db_it->second.end() && !it->second.workers_.empty();
    }
    return false;
}


RemoteObjectInfo DataService::lookup_remote_idx(const CMString& object_name) const {
    auto [db_path, short_name] = split_full(object_name);
    // 跨域读：remote_idx（worker_id 列表）+ worker_registry（host/port）。
    // 双 shared_lock 并发持有，shared_lock 互相兼容，无死锁；worker_registry 保持
    // 地址唯一权威，无冗余数据漏改风险。
    std::shared_lock<fly::WriterPrefRwLock> rlock(remote_mutex_);
    std::shared_lock<std::shared_mutex> wlock(worker_mutex_);
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
    // 跨域读：双 shared_lock（同 lookup_remote_idx）。
    std::shared_lock<fly::WriterPrefRwLock> rlock(remote_mutex_);
    std::shared_lock<std::shared_mutex> wlock(worker_mutex_);
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
    rlock.unlock();
    wlock.unlock();

    // 副本遍历顺序（原 TIER2 循环内的 net_probe 排序收敛至此，一处生效、
    // 全部消费端一致——TIER2 逐副本试读与 TIER3 应答顺序同源）：
    //   1. 存活 storage_only 优先（数据面副本不跑用户 task，服务稳定）；
    //   2. 存活 hybrid；
    //   3. 已死（holder 判死后条目保留的语义下，死副本排尾，避免每次读
    //      白费一次 connect 超时；alive_ 仅 master 进程维护）。
    // 同级内按 net_probe 带宽分降序（stable_sort 保注册序——探测未覆盖的
    // peer 分数为 0，保持原位，行为与未启用时一致）；net_probe_enabled=0
    // 时不按分数重排。
    if (out.size() > 1) {
        const bool probe_enabled = Config::instance()->get_int("net_probe_enabled") != 0;
        std::stable_sort(out.begin(), out.end(),
            [probe_enabled](const RemoteObjectInfo& a, const RemoteObjectInfo& b) {
                auto rank = [](const RemoteObjectInfo& r) {
                    if (!r.alive_) return 2;
                    return r.storage_only_ ? 0 : 1;
                };
                if (rank(a) != rank(b)) return rank(a) < rank(b);
                if (!probe_enabled) return false;
                return NetQualityMonitor::instance().score(a.host_) >
                       NetQualityMonitor::instance().score(b.host_);
            });
    }
    return out;
}

void DataService::remove_remote_index(const CMString& object_name) {
    auto [db_path, short_name] = split_full(object_name);
    std::unique_lock<fly::WriterPrefRwLock> lock(remote_mutex_);
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
                                   int32_t port,
                                   bool storage_only) {
    std::unique_lock<std::shared_mutex> lock(worker_mutex_);
    worker_registry_[worker_id] = {worker_id, host, port, storage_only, true};
}

void DataService::set_worker_alive(uint64_t worker_id, bool alive) {
    std::unique_lock<std::shared_mutex> lock(worker_mutex_);
    auto it = worker_registry_.find(worker_id);
    if (it != worker_registry_.end()) {
        it->second.alive_ = alive;
    }
}

bool DataService::is_storage_worker(uint64_t worker_id) const {
    std::shared_lock<std::shared_mutex> lock(worker_mutex_);
    auto it = worker_registry_.find(worker_id);
    return it != worker_registry_.end() && it->second.storage_only_;
}

RemoteObjectInfo DataService::get_worker_address(uint64_t worker_id) const {
    std::shared_lock<std::shared_mutex> lock(worker_mutex_);
    auto it = worker_registry_.find(worker_id);
    if (it != worker_registry_.end()) {
        return it->second;
    }
    return RemoteObjectInfo{};
}

CMVector<RemoteObjectInfo> DataService::get_all_workers() const {
    std::shared_lock<std::shared_mutex> lock(worker_mutex_);
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
    // entry 已由调用方从 local_idx_ 内存索引取得，直接静态读取，避免 new DataReader
    // 触发 idx 文件全量解析（entries_ map 在此路径从未被消费，纯冗余）。
    return DataReader::read_raw_from_entry(entries.back(), paths.db_path_, paths.data_path_);
}

// ============================================================
// Read Operations (3-tier fallback)
// ============================================================

void DataService::set_remote_compressed_read_handler(RemoteCompressedReadCallback cb) {
    std::unique_lock<std::shared_mutex> lock(cb_mutex_);
    remote_compressed_read_handler_ = std::move(cb);
}

void DataService::set_direct_compressed_read_handler(DirectCompressedReadCallback cb) {
    std::unique_lock<std::shared_mutex> lock(cb_mutex_);
    direct_compressed_read_handler_ = std::move(cb);
}

std::pair<bool, ReadResult> DataService::try_read_local(const CMString& object_name) {
    auto [db_path, short_name] = split_full(object_name);
    CMVector<IndexEntry> entries;
    DbPaths paths;

    // db_paths_ 运行期几乎不变（register 在启动期），先取快照再查 local_idx_。
    // 两段独立 shared_lock，无跨域死锁。
    {
        std::shared_lock<std::shared_mutex> lock(db_paths_mutex_);
        auto path_it = db_paths_.find(db_path);
        if (path_it == db_paths_.end()) return {false, ReadResult{}};
        paths = path_it->second;
    }
    {
        std::shared_lock<std::shared_mutex> lock(local_mutex_);
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
        entries = info.entries_;
    }

    // 2026-08-30 去"①形态"裁定：temp 压缩 record 不驻内存——temp 与正式
    // 对象统一走 entries 盘读（.temp.data_*，write-through 落盘保证 COMPLETE
    // 时数据恒在盘上）。
    ReadResult result = do_read_local_entries(entries, paths);
    if (result.data_buffer_.empty()) return {false, ReadResult{}};
    return {true, std::move(result)};
}

std::pair<bool, DataService::ChunkedLocation> DataService::find_chunked_location(
        const CMString& object_name) {
    auto [db_path, short_name] = split_full(object_name);

    DbPaths paths;
    CMVector<IndexEntry> entries;
    {
        std::shared_lock<std::shared_mutex> plock(db_paths_mutex_);
        auto path_it = db_paths_.find(db_path);
        if (path_it == db_paths_.end()) return {false, {}};
        paths = path_it->second;
    }
    {
        std::shared_lock<std::shared_mutex> llock(local_mutex_);
        auto db_it = local_idx_.find(db_path);
        if (db_it == local_idx_.end()) return {false, {}};
        auto it = db_it->second.objects_.find(short_name);
        if (it == db_it->second.objects_.end() || !it->second) return {false, {}};
        auto& info = *it->second;
        // 2026-08-30 去"①形态"：temp 同样走盘 entry（.temp.data_*，write-through
        // 落盘）——不再排除，serve/TIER1 统一 pread 分片路径。
        if (info.completion_state_.load(std::memory_order_acquire) != CompletionState::COMPLETE) {
            return {false, {}};
        }
        if (info.entries_.empty()) return {false, {}};
        entries = info.entries_;
    }

    const IndexEntry& entry = entries.back();
    CMString file_path = DataReader::find_file_path(entry.file_name_, paths.db_path_,
                                                    paths.data_path_);
    if (file_path.empty()) return {false, {}};
    ChunkedLocation loc;
    loc.file_path = file_path;
    loc.offset = static_cast<uint64_t>(entry.offset_);
    loc.size = static_cast<uint64_t>(entry.size_);
    return {true, std::move(loc)};
}

void DataService::set_streaming_read_handler(StreamingReadCallback cb) {
    std::unique_lock<std::shared_mutex> lock(cb_mutex_);
    streaming_read_handler_ = std::move(cb);
}

DataService::StreamingReadResult DataService::read_streaming(const CMString& object_name) {
    StreamingReadResult out;

    // ── TIER1：本地 → DiskChunkSource（pread 拉取式，D3）──
    // 缓存取消后不整读进内存——按 find_chunked_location 定位 + trailer 预读
    //（一次小 pread 尾部）拿元数据，字节按需拉取（本地读内存有界）。
    {
        auto [loc_ok, loc] = find_chunked_location(object_name);
        if (loc_ok) {
            // trailer 预读：尾部 min(size, 4KB) 解析元数据（失败 = 损坏）。
            int tf = ::open(loc.file_path.c_str(), O_RDONLY);
            if (tf >= 0) {
                uint64_t tail_n = loc.size < 4096 ? loc.size : 4096;
                CMVector<char> tail(static_cast<size_t>(tail_n));
                ssize_t got = ::pread(tf, tail.data(), static_cast<size_t>(tail_n),
                                      static_cast<off_t>(loc.offset + loc.size - tail_n));
                ::close(tf);
                ObjectHeader hdr;
                size_t tl = 0;
                if (got > 0 && static_cast<uint64_t>(got) == tail_n &&
                    ObjectHeader::deserialize_trailer({tail.data(), tail.size()}, hdr, tl)) {
                    auto disk = CMMakeShared<fly::DiskChunkSource>(
                        loc.file_path, loc.offset, loc.size - tl, hdr.py_name_,
                        hdr.total_size_, hdr.chunk_count_,
                        static_cast<int>(hdr.compression_type_));
                    out.success = true;
                    out.py_name = hdr.py_name_;
                    out.block_area_len = loc.size - tl;
                    out.source = disk;
                    return out;
                }
            }
            // trailer 预读失败（极端长 py_name 或损坏）→ 走 TIER2/回退路径
            //（保守正确——MemoryChunkSource 的严格对账在回退路径执行）。
        }
        // temp 对象由上面的盘命中覆盖（.temp.data_* 的 entry 与正式 entry
        // 同构——2026-08-30 去"①形态"后 temp 无内存态）。
    }

    // ── TIER2：streaming cb（首副本 best-effort，§8.1 决策：失败由调用方
    //     回退 read_raw_compressed 完整编排——副本轮换/退避/零容忍在那里）。──

    // ── TIER2：streaming cb 副本轮换（D4，§14.3 用户裁定："始终不使用整体
    //     接收的方式"）。轮换语义与 try_tier2_read 同构：
    //     - 网络类（断连/超时）→ remove 副本 → 下一副本重新流式；
    //     - 校验类（块 CRC/DIGEST）→ 零容忍预算一次（§5）→ 仍败上抛 CHECKSUM
    //       （调用方 FATAL）；
    //     - 全副本失败一轮 → 指数退避 → TIER3 刷新 → 重进 TIER2（30s deadline）。
    //     消费中途失败的重开（对象级重来）在 export 层（ex_stg_open_read_stream
    //     捕获消费异常重新调 read_streaming——dead 副本已被 remove 出序）。
    StreamingReadCallback cb;
    {
        std::shared_lock<std::shared_mutex> lock(cb_mutex_);
        cb = streaming_read_handler_;
    }
    if (!cb) {
        out.error = "no streaming handler";
        out.rerr = ReadError::NETWORK;
        return out;
    }

    constexpr int64_t kInitialDelayMs = 10;
    constexpr int64_t kMaxDelayMs = 500;
    constexpr auto kDeadline = std::chrono::seconds(30);
    int64_t delay_ms = kInitialDelayMs;
    auto net_start = std::chrono::steady_clock::now();
    bool corruption_refetch_mode = false;
    int64_t corruption_budget = 1;  // §5：校验类一次重取

    while (true) {
        auto replicas = lookup_all_remote_idx(object_name);
        if (replicas.empty()) {
            if (corruption_refetch_mode) {
                out.error = "checksum failure persisted, no replica left";
                out.rerr = ReadError::CHECKSUM;
                return out;
            }
            // 首查空：先 TIER3 刷新（master 权威索引可能知道本 worker 未知的
            // 位置——典型：对象仅 master 持有）。刷新后仍空才是真正无源。
            // （2026-08-30 修复：此前首查空直接 NOT_FOUND，TIER3 只在"有副本
            // 但轮换失败"的轮次尾触发——master 持有对象场景被 export 层的
            // 整缓冲回退掩盖，清退回退后暴露。）
            RemoteCompressedReadCallback refresh_cb;
            {
                std::shared_lock<std::shared_mutex> lock(cb_mutex_);
                refresh_cb = remote_compressed_read_handler_;
            }
            if (refresh_cb) {
                auto [refreshed, csp] = refresh_cb(object_name);
                DBG("[STREAM-TIER3-firstempty] obj={}, refreshed={}, can_produce={}",
                    object_name, refreshed, csp);
                replicas = lookup_all_remote_idx(object_name);
            }
            if (replicas.empty()) {
                out.error = "no replica";
                out.rerr = ReadError::OBJECT_NOT_FOUND;
                return out;
            }
        }

        bool saw_not_ready = false;
        bool round_failed = false;
        for (const auto& loc : replicas) {
            auto [ok, source, block_area, rerr] = cb(loc.host_, loc.port_, object_name);
            if (ok && source) {
                // 流式 TIER2 命中同样累积读流量 + suggest 检查（与整缓冲
                // try_tier2_read 对称——2026-08-30 双拉修复暴露的漏接：旧
                // read_object 的 py_name probe 走整缓冲曾提供此计数）。
                record_remote_access(object_name, block_area);
                maybe_suggest_backup(object_name);
                out.success = true;
                out.source = source;
                out.block_area_len = block_area;
                out.write_context_hash = {};
                return out;
            }
            if (rerr == ReadError::CHECKSUM) {
                ERR("[STREAM-FATAL-DATA-CORRUPTION] tier2 streaming checksum failure: "
                    "obj={} worker={} host={}", object_name, loc.worker_id_, loc.host_);
                remove_remote_location(object_name, loc.worker_id_);
                if (corruption_budget > 0) {
                    corruption_budget--;
                    corruption_refetch_mode = true;
                    continue;  // 换副本重新流式（唯一一次）
                }
                out.error = "checksum failure persisted after one re-fetch";
                out.rerr = ReadError::CHECKSUM;
                return out;
            }
            if (rerr == ReadError::OBJECT_NOT_FOUND) {
                remove_remote_location(object_name, loc.worker_id_);
            } else if (rerr == ReadError::DATA_NOT_READY) {
                saw_not_ready = true;
            } else if (rerr == ReadError::SHUTDOWN) {
                out.error = "pool shutdown";
                out.rerr = ReadError::SHUTDOWN;
                return out;
            }
            // NETWORK：keep replica（TIER2 语义——瞬态，轮换后仍可退避重试）。
            round_failed = true;
            if (corruption_refetch_mode) {
                // 重取模式中任何非校验失败同样不可接受（§5）。
                out.error = "re-fetch after checksum failure failed";
                out.rerr = ReadError::CHECKSUM;
                return out;
            }
        }

        if (!saw_not_ready &&
            std::chrono::steady_clock::now() - net_start >= kDeadline) {
            out.error = "streaming tier2 deadline exceeded";
            out.rerr = ReadError::NETWORK;
            return out;
        }
        (void)round_failed;

        // 退避后 TIER3 刷新（remote_cb）→ 重进 TIER2。
        RemoteCompressedReadCallback remote_cb;
        {
            std::shared_lock<std::shared_mutex> lock(cb_mutex_);
            remote_cb = remote_compressed_read_handler_;
        }
        if (remote_cb) {
            auto [refreshed, csp] = remote_cb(object_name);
            DBG("[STREAM-TIER3] obj={}, refreshed={}, can_produce={}",
                object_name, refreshed, csp);
        }
        int64_t jitter = std::max<int64_t>(1, delay_ms / 10);
        std::uniform_int_distribution<int64_t> dist(-jitter, jitter);
        std::this_thread::sleep_for(
            std::chrono::milliseconds(delay_ms + dist(g_backoff_rng)));
        delay_ms = std::min(delay_ms * 2, kMaxDelayMs);
    }
}

std::pair<bool, FlyBufferPtr> DataService::try_read_local_raw(const CMString& object_name,
                                                               bool wait_local_write) {
    // §4.7 low-tier cache 取消：无缓存短路——恒走 index + 磁盘（写后立即读
    // 由 entry 登记时序/NOT_READY 轮询兜底）。
    auto [db_path, short_name] = split_full(object_name);
    CMVector<IndexEntry> entries;
    DbPaths paths;

    // db_paths_ 运行期几乎不变（register 在启动期），先取快照再查 local_idx_，
    // 避免 lookup lambda 跨 local+db_paths 两锁。db_paths 锁独立于 local 锁。
    {
        std::shared_lock<std::shared_mutex> lock(db_paths_mutex_);
        auto path_it = db_paths_.find(db_path);
        if (path_it != db_paths_.end()) {
            paths = path_it->second;
        }
    }

    // 锁内查找并填充读取所需字段。返回 diag：
    //   0=not_found_db/no_path, 1=not_found_obj, 2=not_ready(INCOMPLETE/FAILED), 3=found
    // 2026-08-30 去"①形态"：temp 与正式对象统一 entries 盘读（temp 压缩
    // record 不驻内存——write-through 落盘保证 COMPLETE 时恒在盘上）。
    auto lookup_under_lock = [&]() -> int {
        auto db_it = local_idx_.find(db_path);
        if (db_it == local_idx_.end()) return 0;
        auto it = db_it->second.objects_.find(short_name);
        if (it == db_it->second.objects_.end() || !it->second) return 1;
        auto& info = *it->second;
        if (info.completion_state_.load(std::memory_order_acquire) != CompletionState::COMPLETE) {
            return 2;  // INCOMPLETE 或 FAILED
        }
        entries = info.entries_;
        if (paths.db_path_.empty()) return 0;  // db_paths_ 无此 db（上面未取到）
        return 3;
    };

    int diag = 0;
    if (wait_local_write) {
        // wait 路径需 unique_lock（cv.wait 要求独占，wait 期间释放，唤醒后重获）。
        std::unique_lock<std::shared_mutex> lk(local_mutex_);
        diag = lookup_under_lock();

        // INCOMPLETE/FAILED 且调用方要求 wait 本地写完成：在 per-db cv 上 wait。
        // wait 期间 local_mutex_ 释放（cv.wait 原子地释放锁并阻塞），WriteBackQueue
        // 线程能获锁设 COMPLETE/FAILED 并 notify_all 唤醒。predicate 用 atomic
        // acquire 读，确保看到 writer 的 release 写。无限等待（信任本地写最终完成）。
        if (diag == 2) {
            DBG("[TIER1-WAIT] obj={} INCOMPLETE, waiting for local write completion", object_name);
            auto db_it = local_idx_.find(db_path);
            if (db_it != local_idx_.end()) {
                auto it = db_it->second.objects_.find(short_name);
                if (it != db_it->second.objects_.end() && it->second) {
                    CMSharedPtr<LocalObjectInfo> info = it->second;  // 拷贝 shared_ptr 防悬空
                    db_it->second.write_cv_.wait(lk, [&] {
                        return info->completion_state_.load(std::memory_order_acquire)
                               != CompletionState::INCOMPLETE;
                    });
                    // 唤醒后重查：重新填充读取字段。lk 仍持有（wait 返回时已重新获锁）。
                    entries.clear();
                    diag = lookup_under_lock();
                }
            }
        }
    } else {
        // DataServer 远程 serve 热路径（wait_local_write=false）：纯读，用 shared_lock
        // 允许多 epoll 线程并发查 local_idx_，互不阻塞。
        std::shared_lock<std::shared_mutex> lock(local_mutex_);
        diag = lookup_under_lock();
    }

    switch (diag) {
        case 0: DBG("[TIER1] NOT FOUND: obj={}", object_name); break;
        case 1: DBG("[TIER1] NOT FOUND: obj={}, short_name={}", object_name, short_name); break;
        case 2: DBG("[TEMP-READ-LOCAL] NOT READY: obj={}", object_name); break;
        case 3: DBG("[TEMP-READ-LOCAL] FOUND: obj={}, entries={}", object_name, entries.size()); break;
    }

    if (diag != 3) return {false, nullptr};

    // 2026-08-30 去"①形态"：temp/正式统一 entries 盘读（temp 压缩 record
    // 不驻内存；.temp.data_* 落盘文件的 IndexEntry 与正式 entry 同构）。
    FlyBufferPtr raw = do_read_raw_entries(entries, paths);
    if (!raw || raw->empty()) return {false, nullptr};

    // §4.7 low-tier put 移除（缓存取消）。
    return {true, raw};
}

bool DataService::is_write_in_progress(const CMString& object_name) const {
    auto [db_path, short_name] = split_full(object_name);
    std::shared_lock<std::shared_mutex> lock(local_mutex_);
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

std::tuple<bool, FlyBufferPtr, CMString, CMString, bool> DataService::read_tier1_hit(
        const CMString& object_name, const FlyBufferPtr& raw) {
    auto [db_path, short_name] = split_full(object_name);
    bool is_temp_entry = false;
    DbPaths paths;
    CMVector<IndexEntry> entries;
    // TIER1 命中后查 local_idx_ + db_paths_ 取 entries/is_temp/paths（用于解析
    // py_name/write_hash）。双 shared_lock 跨域读，与 try_read_local_raw 一致。
    // 注：此二次查询无法直接复用 try_read_local_raw 的结果（其签名返回
    // found/compressed_bytes，不含 entries/paths；该函数为 DataServer 远程 serve
    // 热路径共有，扩签名回归风险大于收益），保持独立查询以保证语义清晰。
    {
        std::shared_lock<std::shared_mutex> llock(local_mutex_);
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
    }
    {
        std::shared_lock<std::shared_mutex> plock(db_paths_mutex_);
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

std::tuple<bool, FlyBufferPtr, CMString, CMString, bool> DataService::try_tier2_read(
        const CMString& object_name, const DirectCompressedReadCallback& cb) {
    constexpr int64_t kInitialDelayMs = 10;
    constexpr int64_t kMaxDelayMs = 500;
    constexpr auto kNetworkDeadline = std::chrono::seconds(30);

    int64_t delay_ms = kInitialDelayMs;
    auto net_start = std::chrono::steady_clock::now();

    // ── 零容忍校验预算（chunked-transfer-design §5）──
    // 校验类失败（wire 根 CRC / 帧头 check）只允许【一次】对象级重取：
    // 预算内换副本立即重试（无退避——校验失败不是拥塞）；预算耗尽、或重取
    // 期间以任何方式失败（断连/超时/NOT_FOUND/NOT_READY）→ DataCorruptionError
    // 上抛（重取唯一可接受结果 = 干净通过全部校验）。
    bool corruption_refetch_mode = false;

    while (true) {
        auto replicas = lookup_all_remote_idx(object_name);
        if (replicas.empty()) {
            if (corruption_refetch_mode) {
                throw DataCorruptionError(
                    "[FATAL-DATA-CORRUPTION] object '" + object_name +
                    "': checksum failure persisted, no healthy replica left after one re-fetch");
            }
            return {false, nullptr, {}, {}, false};  // no local replicas → need TIER3
        }

        bool saw_not_ready = false;
        // 副本遍历顺序（storage 优先 / 死副本排尾 / net_probe 带宽分）
        // 已收敛到 lookup_all_remote_idx 内统一排序，此处直接消费。
        for (const auto& loc : replicas) {
            auto [cb_found, cb_data, cb_py_name, cb_hash, cb_rerr] =
                cb(loc.host_, loc.port_, object_name);
            if (cb_found) {
                DBG("[TIER2] obj={}, hit worker={}", object_name, loc.worker_id_);
                // worker TIER2 跨 worker 读命中：累积读流量 + 检查 backup suggest
                record_remote_access(object_name, cb_data ? static_cast<int64_t>(cb_data->size()) : 0);
                maybe_suggest_backup(object_name);
                return {true, cb_data, std::move(cb_py_name), std::move(cb_hash), false};
            }
            if (cb_rerr == ReadError::CHECKSUM) {
                // 校验类失败：副本坏——踢出（不再参与本轮/后续轮），扣预算。
                ERR("[FATAL-DATA-CORRUPTION] tier2 wire checksum failure: obj={} worker={} host={}",
                    object_name, loc.worker_id_, loc.host_);
                remove_remote_location(object_name, loc.worker_id_);
                if (!corruption_refetch_mode) {
                    corruption_refetch_mode = true;  // 消耗唯一一次重取预算
                    continue;  // 立即换下一副本（无退避）
                }
                throw DataCorruptionError(
                    "[FATAL-DATA-CORRUPTION] object '" + object_name +
                    "': checksum failure persisted after one re-fetch");
            }
            if (cb_rerr == ReadError::OBJECT_NOT_FOUND) {
                remove_remote_location(object_name, loc.worker_id_);
            } else if (cb_rerr == ReadError::DATA_NOT_READY) {
                saw_not_ready = true;
            } else if (cb_rerr == ReadError::SHUTDOWN) {
                return {false, nullptr, {}, {}, false};
            }
            // NETWORK: transient, keep replica, retry next round.

            // 重取模式中任何非校验失败同样不可接受（§5：CHECKSUM→断连=fatal）。
            if (corruption_refetch_mode) {
                throw DataCorruptionError(
                    "[FATAL-DATA-CORRUPTION] object '" + object_name +
                    "': re-fetch after checksum failure failed (replica error)");
            }
        }

        // Round fully failed. DATA_NOT_READY → unbounded; else bound by
        // the network deadline.
        if (corruption_refetch_mode) {
            // 走到这里 = 重取轮全失败（如全 NETWORK 被上面跳过？NETWORK 会
            // 即时 throw，NOT_FOUND 已 remove——此分支兜底：重取模式不进退避循环）。
            throw DataCorruptionError(
                "[FATAL-DATA-CORRUPTION] object '" + object_name +
                "': re-fetch round exhausted after checksum failure");
        }
        if (!saw_not_ready &&
            std::chrono::steady_clock::now() - net_start >= kNetworkDeadline) {
            return {false, nullptr, {}, {}, false};
        }

        int64_t jitter = std::max<int64_t>(1, delay_ms / 10);
        std::uniform_int_distribution<int64_t> dist(-jitter, jitter);
        int64_t actual = delay_ms + dist(g_backoff_rng);
        std::this_thread::sleep_for(std::chrono::milliseconds(actual));
        delay_ms = std::min(delay_ms * 2, kMaxDelayMs);
    }
}

std::tuple<bool, FlyBufferPtr, CMString, CMString, bool> DataService::read_raw_compressed(
        const CMString& object_name, bool bypass_local) {
    // ── TIER1: local ──
    // bypass_local=true：零容忍重取路径（§5）——本地 record 已判定损坏，
    // 跳过 TIER1 直接走 TIER2 远程副本（无副本即失败，不回读坏源）。
    if (!bypass_local) {
        auto [found, raw] = try_read_local_raw(object_name);
        if (found) {
            return read_tier1_hit(object_name, raw);
        }
    }

    // ── TIER2/TIER3 orchestration ──
    // TIER2: iterate every known replica (local remote_idx) once per round with
    // exponential backoff. On full failure, TIER3 queries master for ALL replica
    // locations, refreshes remote_idx, and re-enters TIER2 once (tier3_queried
    // guards against TIER2↔TIER3 bouncing). If TIER3 also has no location, fail.
    DirectCompressedReadCallback cb;
    {
        std::shared_lock<std::shared_mutex> lock(cb_mutex_);
        cb = direct_compressed_read_handler_;
    }
    RemoteCompressedReadCallback remote_cb;
    {
        std::shared_lock<std::shared_mutex> lock(cb_mutex_);
        remote_cb = remote_compressed_read_handler_;
    }

    bool tier3_queried = false;
    bool last_can_still_produce = false;

    while (true) {
        // ── TIER2 round loop ──
        if (cb) {
            auto [hit, data, py_name, hash, _] = try_tier2_read(object_name, cb);
            if (hit) {
                return {true, data, std::move(py_name), std::move(hash), false};
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
    std::shared_lock<std::shared_mutex> lock(local_mutex_);
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
    auto [_, short_name] = split_full(object_name);
    CMSharedPtr<LocalObjectInfo> info = CMMakeShared<LocalObjectInfo>();
    info->db_path_ = db_path;
    info->is_temp_ = true;
    info->completion_state_.store(CompletionState::INCOMPLETE, std::memory_order_relaxed);

    {
        std::unique_lock<std::shared_mutex> lock(local_mutex_);
        local_idx_[db_path].objects_[short_name] = info;
    }

    DBG("[TEMP-WRITE-STARTED] obj={}, db_path={}", object_name, db_path);
}

void DataService::on_temp_write(const CMString& db_path, const CMString& object_name,
                                const std::optional<IndexEntry>& disk_entry) {
    // 2026-08-30 去"①形态"（用户裁定）：不再接收内存 data——只登记盘 entry
    // + COMPLETE（write-through 落盘保证数据恒在盘上，读恒走 entries 盘路径）。
    auto [_, short_name] = split_full(object_name);

    {
        std::unique_lock<std::shared_mutex> lock(local_mutex_);
        auto& db_entry = local_idx_[db_path];
        auto it = db_entry.objects_.find(short_name);
        if (it == db_entry.objects_.end() || !it->second) {
            ERR("[TEMP-WRITE] on_temp_write: no entry found for obj={}, db_path={}", object_name, db_path);
            return;
        }
        auto& info = *it->second;
        if (disk_entry) {
            info.entries_.clear();
            info.entries_.push_back(*disk_entry);
        }
        info.completion_state_.store(CompletionState::COMPLETE, std::memory_order_release);

        DBG("[TEMP-WRITE] on_temp_write complete: obj={}, db_path={}, entry_size={}",
            object_name, db_path, disk_entry ? disk_entry->size_ : 0);

        // temp 写完成也 notify：等待 temp 对象的 reader 唤醒。
        db_entry.write_cv_.notify_all();
    }
}

void DataService::cleanup_temp_entries(const CMString& db_path) {
    std::unique_lock<std::shared_mutex> lock(local_mutex_);
    auto db_it = local_idx_.find(db_path);
    if (db_it == local_idx_.end()) return;
    for (auto it = db_it->second.objects_.begin(); it != db_it->second.objects_.end();) {
        if (it->second && it->second->is_temp_) {
            it = db_it->second.objects_.erase(it);
        } else {
            ++it;
        }
    }
}

// ============================================================
// Auto-Backup Access Tracking (inline in remote_idx_)
// ============================================================

void DataService::record_remote_access(const CMString& object_name, int64_t size_bytes) {
    auto [db_path, short_name] = split_full(object_name);
    std::unique_lock<fly::WriterPrefRwLock> lock(remote_mutex_);
    auto db_it = remote_idx_.find(db_path);
    if (db_it == remote_idx_.end()) return;
    auto obj_it = db_it->second.find(short_name);
    if (obj_it == db_it->second.end()) return;
    auto& meta = obj_it->second;
    meta.read_count_++;
    if (size_bytes > 0) {
        meta.size_bytes_ = size_bytes;
        meta.accumulated_bytes_ += size_bytes;
    }
    meta.last_access_time_ = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

void DataService::maybe_suggest_backup(const CMString& object_name) {
    // worker TIER2 读后检查：累积读流量达阈值 + cooldown 过 → suggest → reset。
    // worker 不时间衰减（累积值精确反映自上次 suggest 的增量）。suggest 后 reset（清零重新累积）。
    if (Config::instance()->get_int("auto_backup_enabled") != 1) return;
    auto [db_path, short_name] = split_full(object_name);
    std::unique_lock<fly::WriterPrefRwLock> lock(remote_mutex_);
    auto db_it = remote_idx_.find(db_path);
    if (db_it == remote_idx_.end()) return;
    auto obj_it = db_it->second.find(short_name);
    if (obj_it == db_it->second.end()) return;
    auto& meta = obj_it->second;
    int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    int64_t cooldown = Config::instance()->get_int("worker_suggest_cooldown");
    if (now - meta.last_suggest_time_ < cooldown) return;
    uint64_t bytes_thr = static_cast<uint64_t>(Config::instance()->get_int("worker_suggest_bytes_threshold"));
    uint64_t count_thr = static_cast<uint64_t>(Config::instance()->get_int("worker_suggest_count_threshold"));
    if (meta.accumulated_bytes_ < bytes_thr && meta.read_count_ < count_thr) return;
    // 达阈值 → suggest（经 WorkerAgentContext 回调，由 WorkerAgent 发消息给 master）
    fly::WorkerAgentContext::suggest_backup(object_name, meta.read_count_, meta.accumulated_bytes_, meta.size_bytes_);
    // reset（增量已上报，清零重新累积）
    meta.read_count_ = 0;
    meta.accumulated_bytes_ = 0;
    meta.last_suggest_time_ = now;
}

uint64_t DataService::get_access_read_count(const CMString& object_name) const {
    auto [db_path, short_name] = split_full(object_name);
    std::shared_lock<fly::WriterPrefRwLock> lock(remote_mutex_);
    auto db_it = remote_idx_.find(db_path);
    if (db_it == remote_idx_.end()) return 0;
    auto obj_it = db_it->second.find(short_name);
    if (obj_it == db_it->second.end()) return 0;
    return obj_it->second.read_count_;
}

}  // namespace fly
