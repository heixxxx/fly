#pragma once

#include <storage/cpp/index_entry.h>
#include <storage/cpp/data_reader.h>
#include <storage/cpp/write_back_queue.h>
#include <storage/cpp/temp_store.h>
#include <common/cpp/common_types.h>
#include <common/cpp/concurrent_map.h>
#include <common/cpp/error_types.h>
#include <cstdint>
#include <mutex>
#include <utility>
#include <atomic>
#include <functional>
#include <string>
#include <condition_variable>

namespace fly {

// full_name = "db_path:short_name"，split 用 rfind(':')（db_path 变长含 '/'）。
// split 用 rfind(':') —— short_name 是逻辑对象名（如 "matrix"、"result/obj"）不含 ':'，
// 最后一个 ':' 必是分隔符。db_path 在 Database 构造时校验不含 ':'（双保险）。
inline std::pair<CMString, CMString> split_full_name(const CMString& full) {
    auto pos = full.rfind(':');
    if (pos == CMString::npos) {
        return {{}, {}};
    }
    return {full.substr(0, pos), full.substr(pos + 1)};
}

struct RemoteObjectMeta {
    CMVector<uint64_t> workers_;
    uint64_t read_count_ = 0;
    int64_t last_access_time_ = 0;   // epoch seconds
    int64_t size_bytes_ = 0;         // 压缩后字节数（data locality 调度亲和度打分用）
};

struct BackupDecision {
    uint64_t read_count_ = 0;
    bool should_backup_ = false;
    uint32_t current_replicas_ = 0;
    uint32_t target_replicas_ = 0;
};

struct RemoteObjectInfo {
    uint64_t worker_id_ = 0;
    CMString host_;
    int32_t port_ = 0;
};

enum class CompletionState {
    INCOMPLETE,
    COMPLETE,
    FAILED
};

struct LocalObjectInfo {
    CMString db_path_;
    CMVector<IndexEntry> entries_;
    // atomic：锁外可被读路径 acquire 读（如 try_read_local_raw 的 predicate），
    // 写路径在 mutex_ 保护下 release 写。替代旧的"空 cv_lock 块"屏障补丁。
    std::atomic<CompletionState> completion_state_{CompletionState::INCOMPLETE};
    bool flushed_ = false;
    CMString error_message_;
    bool is_temp_ = false;
    FlyBufferPtr temp_compressed_data_;  // shared_ptr: zero-copy reads, automatic lifetime
};

// per-db 本地索引：一个 db_path 下所有对象的索引 + 一个共享 cv。
// cv 用于读路径等待本 db 任意对象的写完成（替代旧的 per-object mutex+cv，
// 避免 88B/对象 × 百万对象的内存爆炸）。notify_all 唤醒该 db 所有 waiter，
// 各自 predicate 检查目标对象 completion_state_，未完成则重睡。
struct DbLocalIndex {
    CMUnorderedMap<CMString /*short_name*/, CMSharedPtr<LocalObjectInfo>> objects_;
    std::condition_variable write_cv_;
};

class DataServer;

class DataService {
public:
    DataService();
    ~DataService();

    static CMSharedPtr<DataService> instance();
    // TIER3 callback: query master for ALL replica locations of the object,
    // populate local remote_idx with them, and signal the outcome. It does NOT
    // fetch object data — reading is TIER2's job.
    //   returns (locations_refreshed, can_still_produce):
    //     - locations_refreshed=true  : master returned >=1 replica; remote_idx
    //       updated; read_raw_compressed re-enters TIER2 to try them.
    //     - locations_refreshed=false : master has no location; can_still_produce
    //       indicates whether some task may still produce the object.
    using RemoteCompressedReadCallback = std::function<std::tuple<bool, bool>(
        const CMString& object_name)>;
    // Returns (found, data, py_name, write_context_hash, read_error). read_error
    // classifies a failure so TIER2 can decide: DATA_NOT_READY/NETWORK keep the
    // replica for retry, OBJECT_NOT_FOUND drops it, SHUTDOWN aborts.
    using DirectCompressedReadCallback = std::function<std::tuple<bool, FlyBufferPtr, CMString, CMString, ReadError>(
        const CMString& host, int32_t port, const CMString& object_name)>;

    // ============================================================
    // Database Registry
    // ============================================================

    void register_database(const CMString& db_path,
                            const CMString& data_path,
                            const CMString& writer_id = "");

    void unregister_database(const CMString& db_path);

    bool has_database(const CMString& db_path) const;

    // ============================================================
    // DB Migration Redirect (替代 db_path 的逻辑锚点)
    // ============================================================

    // 解析 db_path：若 {db_path}/_MIGRATED_TO 存在，返回迁移 target 的 db_path
    // （链式迁移 A→B→C 递归展平）；否则返回 db_path 原值。结果缓存进
    // migrated_db_paths_（path 未变期间只 stat 一次）。merge 后 master 主动调
    // set_migrated_path 更新缓存，避免重复 stat。
    CMString resolve_migrated_path(const CMString& db_path);

    // 读 {db_path}/_MIGRATED_TO 的 target_data_path。无迁移文件返回空。
    // Database 构造跟随迁移时用此获取 target 的 data_path（源 data_path 已失效）。
    CMString read_migrated_data_path(const CMString& db_path);

    // 主动更新/清除迁移缓存（merge 产生新迁移后 master 调用）。
    // target 为空表示清除该 source 的迁移记录。
    void set_migrated_path(const CMString& source_db_path,
                            const CMString& target_db_path);

    // 在 source_db_path 写 _MIGRATED_TO 迁移标识文件。merge 跨 path 时调用。
    static void write_migration_marker(const CMString& source_db_path,
                                        const CMString& target_db_path,
                                        const CMString& target_data_path);

    // ============================================================
    // Local Index Management
    // ============================================================

    void on_object_written(const CMString& db_path,
                            const CMString& object_name,
                            const IndexEntry& entry);

    void on_flush(const CMString& db_path);

    void on_write_started(const CMString& db_path,
                           const CMString& object_name);

    void on_write_completed(const CMString& db_path,
                             const CMString& object_name,
                             const CMVector<IndexEntry>& entries);

    void on_write_failed(const CMString& db_path,
                          const CMString& object_name,
                          const CMString& error_message);

    void remove_local_index(const CMString& object_name);

    // 整 db 清除 local_idx_（不碰 ObjectCache —— 用于 merge 后清理：数据位置迁移，
    // 内容未变，cache 仍是正确副本）。merge_db 把源对象迁到 master host 后，
    // master/源 worker 的 local_idx_ 仍残留指向已删源 .dat 的 entry，导致 TIER1
    // 读必失败 + ERR 日志。此接口整体清掉一个 db 的 local_idx_。
    void clear_local_index_for_db(const CMString& db_path);

    // 整 db 清除 remote_idx_（不碰 ObjectCache）。merge 后各 worker 缓存的源 worker
    // 位置已失效（源 .dat 已删），不清会导致 TIER2 远程读试源 worker 失败。
    void clear_remote_index_for_db(const CMString& db_path);

    bool has_local_object(const CMString& object_name) const;

    void on_object_flushed(const CMString& object_name);

    void restore_entries(const CMString& db_path,
                           const CMVector<IndexEntry>& entries);

    std::optional<CMVector<IndexEntry>> find_local_entries(const CMString& object_name) const;

    // ============================================================
    // Remote Index Management
    // ============================================================

    // 登记/更新远程对象位置。size_bytes > 0 时更新对象的压缩字节数；
    // size_bytes == 0 时保持已记录的 size 不变（防御 rebuild 等无 size 的路径）。
    void update_remote_idx(const CMString& object_name,
                            uint64_t worker_id,
                            const CMString& host,
                            int32_t port,
                            int64_t size_bytes = 0);

    void add_remote_location(const CMString& object_name, uint64_t worker_id);
    void remove_remote_location(const CMString& object_name);
    void remove_remote_location(const CMString& object_name, uint64_t worker_id);
    // 返回持有该对象的 worker_id 列表（按值拷贝，在内部锁保护下完成，调用方拿独立副本，线程安全）。
    CMVector<uint64_t> get_remote_workers(const CMString& object_name) const;

    bool has_remote_location(const CMString& object_name) const;
    RemoteObjectInfo lookup_remote_idx(const CMString& object_name) const;
    // 返回对象的全部副本地址（每个副本含 worker_id/host/port）。无记录返回空。
    // 与 lookup_remote_idx（只返回首个）相对，供 TIER2 多副本轮询使用。
    CMVector<RemoteObjectInfo> lookup_all_remote_idx(const CMString& object_name) const;
    // 返回对象的压缩后字节数（未登记返回 0）。
    int64_t get_remote_size(const CMString& object_name) const;

    void remove_remote_index(const CMString& object_name);

    // ============================================================
    // Worker Registry
    // ============================================================

    void register_worker(uint64_t worker_id,
                            const CMString& host,
                            int32_t port);

    RemoteObjectInfo get_worker_address(uint64_t worker_id) const;

    // Snapshot of all registered data-server peers (worker_id → host:port).
    // Used by the bandwidth-probe thread to pick probe targets. Returns a
    // detached copy under the internal lock.
    CMVector<RemoteObjectInfo> get_all_workers() const;

    // ============================================================
    // Read Operations (3-tier fallback)
    // ============================================================

    void set_remote_compressed_read_handler(RemoteCompressedReadCallback cb);
    void set_direct_compressed_read_handler(DirectCompressedReadCallback cb);

    std::pair<bool, ReadResult> try_read_local(const CMString& object_name);

    // wait_local_write=true（默认）：遇 INCOMPLETE 时在 per-db cv 上 wait 本地写完成
    // （无限等待，信任本地写最终完成；FAILED 唤醒后返回 false 走 TIER2）。用于本地
    // read_raw_compressed 的 TIER1 快路径。wait_local_write=false：INCOMPLETE 立即返回
    // false（上层据 is_write_in_progress 判定 DATA_NOT_READY）。用于 DataServer 远程
    // serve 路径——IO 线程池（默认 4 线程）不能阻塞，避免并发 wait 耗尽 serve 能力。
    std::pair<bool, FlyBufferPtr> try_read_local_raw(const CMString& object_name,
                                                      bool wait_local_write = true);

    bool is_write_in_progress(const CMString& object_name) const;

    std::pair<bool, ReadResult> try_read_remote(const CMString& object_name);

    std::tuple<bool, FlyBufferPtr, CMString, CMString, bool> read_raw_compressed(const CMString& object_name);

    void on_temp_write_started(const CMString& db_path, const CMString& object_name);
    void on_temp_write(const CMString& db_path, const CMString& object_name, FlyBufferPtr compressed_data);
    void cleanup_temp_entries(const CMString& db_path);

    // ============================================================
    // Data Server (independent data transfer network layer)
    // ============================================================

    void start_data_server(const CMString& host, int port, int io_thread_count);
    void stop_data_server();
    int get_data_port() const;
    CMString get_write_context_hash(const CMString& object_name) const;

    // ============================================================
    // Write-Back Queue
    // ============================================================

    void start_write_back();
    void stop_write_back();
    void enqueue_write_back(fly::WriteRequest&& task);
    void drain_write_back();
    // 丢弃 WBQ 中所有未处理的写请求（task 异常撤销用）。详见 WriteBackQueue::clear_pending。
    void clear_write_back();
    bool is_write_back_running() const;

    // ============================================================
    // Lifecycle
    // ============================================================

    void reset();

    // ============================================================
    // Auto-Backup Access Tracking (inline in remote_idx_)
    // ============================================================

    void record_remote_access(const CMString& object_name);

    BackupDecision evaluate_auto_backup(const CMString& object_name,
                                         uint64_t threshold,
                                         uint32_t target_replicas) const;

    void decay_remote_access(int64_t protection_seconds, int decay_factor_percent);

    uint64_t get_access_read_count(const CMString& object_name) const;

private:
    struct DbPaths {
        CMString db_path_;
        CMString data_path_;
        CMString writer_id_;
    };

    // ============================================================
    // Private Helpers — Name Parsing
    // ============================================================

    static std::pair<CMString, CMString> split_full(const CMString& full);
    CMString get_db_path_for_object(const CMString& object_name) const;

    // ============================================================
    // Private Helpers — Read Operations
    // ============================================================

    ReadResult do_read_local_entries(const CMVector<IndexEntry>& entries,
                                     const DbPaths& paths);
    FlyBufferPtr do_read_raw_entries(const CMVector<IndexEntry>& entries,
                                 const DbPaths& paths);

    mutable std::mutex mutex_;

    CMUnorderedMap<CMString /*db_path*/, DbLocalIndex> local_idx_;

    CMUnorderedMap<CMString /*db_path*/,
        CMUnorderedMap<CMString /*short_name*/,
            RemoteObjectMeta>> remote_idx_;

    CMUnorderedMap<uint64_t, RemoteObjectInfo> worker_registry_;

    CMUnorderedMap<CMString, DbPaths> db_paths_;

    // 迁移重定向缓存：source_db_path → resolved target_db_path（链式展平）。
    // resolve_migrated_path miss 时 stat _MIGRATED_TO 一次并缓存。merge 后 master
    // 主动 set_migrated_path 更新。
    CMUnorderedMap<CMString, CMString> migrated_db_paths_;

    CMVector<CMString> temp_lru_order_;
    int64_t temp_total_bytes_ = 0;
    int64_t temp_max_bytes_ = 0;
    CMUniquePtr<fly::TempStore> temp_eviction_store_;

    CMUniquePtr<DataServer> data_server_;

    CMUniquePtr<fly::WriteBackQueue> write_back_queue_;

    RemoteCompressedReadCallback remote_compressed_read_handler_;
    DirectCompressedReadCallback direct_compressed_read_handler_;
};

}  // namespace fly
