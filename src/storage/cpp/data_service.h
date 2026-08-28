#pragma once

#include <storage/cpp/index_entry.h>
#include <storage/cpp/data_reader.h>
#include <storage/cpp/write_back_queue.h>
#include <storage/cpp/temp_store.h>
#include <common/cpp/common_types.h>
#include <common/cpp/chunk_source.h>
#include <common/cpp/concurrent_map.h>
#include <common/cpp/error_types.h>
#include <common/cpp/writer_pref_rwlock.h>
#include <cstdint>
#include <functional>
#include <mutex>
#include <shared_mutex>
#include <utility>
#include <atomic>
#include <functional>
#include <string>
#include <condition_variable>
#include <chrono>

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
    uint64_t accumulated_bytes_ = 0; // 累积传输字节（worker suggest 用，suggest 后 reset）
    int64_t last_suggest_time_ = 0;  // 上次 suggest 时间（cooldown 用）
};

struct RemoteObjectInfo {
    uint64_t worker_id_ = 0;
    CMString host_;
    int32_t port_ = 0;
    // 读侧排序依据（registry 登记/覆盖时刷新）。storage_only 优先：数据面
    // 副本不跑用户 task，进程可靠且数据服务资源稳定。alive_ 仅 master 进程
    // 维护（权威视图判死/复活）；worker 侧不判死，恒 true，靠连接失败自然
    // 处理。
    bool storage_only_ = false;
    bool alive_ = true;
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
    bool is_temp_ = false;
    FlyBufferPtr temp_compressed_data_;  // shared_ptr: zero-copy reads, automatic lifetime
};

// per-db 本地索引：一个 db_path 下所有对象的索引 + 一个共享 cv。
// cv 用于读路径等待本 db 任意对象的写完成（替代旧的 per-object mutex+cv，
// 避免 88B/对象 × 百万对象的内存爆炸）。notify_all 唤醒该 db 所有 waiter，
// 各自 predicate 检查目标对象 completion_state_，未完成则重睡。
struct DbLocalIndex {
    CMUnorderedMap<CMString /*short_name*/, CMSharedPtr<LocalObjectInfo>> objects_;
    // condition_variable_any：可配合 shared_mutex 的 unique_lock（std::condition_variable
    // 只接受 unique_lock<std::mutex>）。wait 仅用于本地读等待写完成路径（非 DataServer
    // 并发读热路径），condition_variable_any 的额外开销可接受。
    std::condition_variable_any write_cv_;
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
    // storage_only 透传 registry（worker 侧从 DataLocation 消息回填时携带，
    // master 填充方权威；缺省 false 与旧调用兼容）。
    void update_remote_idx(const CMString& object_name,
                            uint64_t worker_id,
                            const CMString& host,
                            int32_t port,
                            int64_t size_bytes = 0,
                            bool storage_only = false);

    void add_remote_location(const CMString& object_name, uint64_t worker_id);
    void remove_remote_location(const CMString& object_name);
    void remove_remote_location(const CMString& object_name, uint64_t worker_id);
    // 返回持有该对象的 worker_id 列表（按值拷贝，在内部锁保护下完成，调用方拿独立副本，线程安全）。
    CMVector<uint64_t> get_remote_workers(const CMString& object_name) const;
    // 反查：remote_idx 中 holder 含 worker_id 的全部对象全名（数据全灭快速失败用；
    // 判死低频事件，O(n) 遍历可接受）。
    CMVector<CMString> get_objects_of_worker(uint64_t worker_id) const;
    // 聚合：一次遍历返回多个 worker 名下全部副本对象的 size_bytes_ 总和（backup
    // 目标选择的磁盘水位依据；只输出请求集合中出现的 worker）。
    CMUnorderedMap<uint64_t, int64_t> get_worker_bytes_batch(
        const CMUnorderedSet<uint64_t>& worker_ids) const;

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
                            int32_t port,
                            bool storage_only = false);
    // master 判死/复活时刷新 registry 的 alive_ 标记（整条覆盖式注册会将其
    // 重置为 true，重连注册天然恢复）。
    void set_worker_alive(uint64_t worker_id, bool alive);
    // registry 查询：worker 是否登记为 storage_only（master 填充 DataLocation
    // 等发送点用；未登记返回 false）。
    bool is_storage_worker(uint64_t worker_id) const;

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

    // bypass_local=true：零容忍重取路径（§5）——本地 record 已判定损坏，
    // 跳过 TIER1 走 TIER2 远程副本（无副本即失败，不回读坏源）。
    // 校验预算耗尽（TIER2 内 CHECKSUM 重取仍败）抛 DataCorruptionError。
    std::tuple<bool, FlyBufferPtr, CMString, CMString, bool> read_raw_compressed(
        const CMString& object_name, bool bypass_local = false);

    // L2 分片服务位置查询（chunked-transfer-design §4.5/§7.1 #20）：
    // 返回对象最近 entry 的落盘定位（绝对文件路径 + 区间），DataServer 据此
    // pread 分片发送（不整读进内存）。found=false：对象不在本地 / temp 内存
    // 对象 / 索引不完整——调用方回退整帧快路径。
    struct ChunkedLocation {
        CMString file_path;
        uint64_t offset = 0;
        uint64_t size = 0;
    };
    std::pair<bool, ChunkedLocation> find_chunked_location(const CMString& object_name);

    // ── L3 流式读（§8.1）──
    // streaming cb：包装 DataClientPool::request_raw_exchange + NetworkChunkSource
    //（WorkerAgent 注册）。返回 (success, source, block_area_len, rerr)。
    using StreamingReadCallback = std::function<std::tuple<bool,
                                                           CMSharedPtr<fly::ChunkSource>,
                                                           uint64_t,
                                                           ReadError>(
        const CMString& host, int32_t port, const CMString& object_name)>;
    void set_streaming_read_handler(StreamingReadCallback cb);

    struct StreamingReadResult {
        bool success = false;
        CMSharedPtr<fly::ChunkSource> source;   // Memory（TIER1）或 Network（TIER2）
        uint64_t block_area_len = 0;
        CMString py_name;
        CMString write_context_hash;
        ReadError rerr = ReadError::NONE;
        CMString error;
    };
    // 流式读编排：TIER1（本地 → Memory 源）/ TIER2（streaming cb，首副本
    // best-effort——失败由调用方回退 read_raw_compressed 完整编排）。
    // 零容忍语义：源校验状态由消费端查 source->failed()；完整重取编排在
    // 回退路径（read_object_compressed）。
    StreamingReadResult read_streaming(const CMString& object_name);

    void on_temp_write_started(const CMString& db_path, const CMString& object_name);
    // disk_entry：temp 落盘产物（temp_data_*.dat）的 IndexEntry。提供时填入
    // entries_（内存 LRU miss 后的盘读 fallback 路径）；缺失（std::nullopt，
    // 落盘失败或旧路径）则纯内存语义。
    void on_temp_write(const CMString& db_path, const CMString& object_name,
                       FlyBufferPtr compressed_data,
                       const std::optional<IndexEntry>& disk_entry = std::nullopt);
    // 恢复 temp 落盘条目（on_idx_load_command 加载 {wid}.temp.idx 后调用）：
    // 灌 local_idx_（is_temp=true + entries_，无内存 data）——跨进程 temp 可见
    // （task 级断点：已完成 task 的 temp 输出 ready）。
    void restore_temp_entries(const CMString& db_path,
                              const CMVector<IndexEntry>& entries);
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

    void record_remote_access(const CMString& object_name, int64_t size_bytes = 0);
    // worker TIER2 读后检查是否该上报 backup suggest（增量阈值 + cooldown + reset）
    void maybe_suggest_backup(const CMString& object_name);

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
    // read_raw_compressed 的 TIER1 命中装配段：二次查 local_idx_/db_paths_ 取
    // is_temp/entries/paths（解析 py_name/write_hash 用），temp 与正式条目分流。
    std::tuple<bool, FlyBufferPtr, CMString, CMString, bool> read_tier1_hit(
        const CMString& object_name, const FlyBufferPtr& raw);
    // read_raw_compressed 的 TIER2 循环：本地 remote_idx 已知副本的跨机直读
    //（指数退避 10ms→500ms + 30s 网络期限；OBJECT_NOT_FOUND 踢副本、
    // DATA_NOT_READY 不限期、SHUTDOWN 即退）。命中返回 found=true；副本耗尽
    // 或期限到返回 found=false（调用方决定是否进 TIER3）。
    std::tuple<bool, FlyBufferPtr, CMString, CMString, bool> try_tier2_read(
        const CMString& object_name, const DirectCompressedReadCallback& cb);

    // 分片锁：按数据域拆分，读多写少场景用 shared_mutex（读 shared_lock 并发、写
    // unique_lock 独占）。替代原单一 mutex_ 的全串行化（多线程并发读吞吐反而低于
    // 单线程的负伸缩，见 docs/perf-baselines.md）。
    //   local_mutex_   : local_idx_（含 DbLocalIndex.objects_ + write_cv_）+ temp 相关
    //   remote_mutex_  : remote_idx_
    //   worker_mutex_  : worker_registry_（地址唯一权威）
    //   db_paths_mutex_: db_paths_ + migrated_db_paths_
    // cv（write_cv_）绑定 local_mutex_：wait/notify 在其 unique_lock 下进行。
    // 跨域读（lookup_remote_idx/lookup_all_remote_idx）持 remote+worker 双 shared_lock，
    // shared_lock 互相兼容，无死锁。
    mutable std::shared_mutex local_mutex_;
    // remote_mutex_ 用写者优先锁：CPU 饥饿下，读者（has_remote_location 轮询、
    // DataQuery lookup）持 shared_lock 期间被 OS 抢占会饿死写者（update_remote_idx
    // 的 add_remote_location 需 unique_lock）。写者优先保证写者申请后新读者阻塞，
    // 写者不会无限等待。详见 writer_pref_rwlock.h。
    mutable WriterPrefRwLock remote_mutex_;
    mutable std::shared_mutex worker_mutex_;
    mutable std::shared_mutex db_paths_mutex_;
    mutable std::shared_mutex cb_mutex_;        // 2 个 read handler callback

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
    StreamingReadCallback streaming_read_handler_;  // L3 流式 TIER2 cb
};

}  // namespace fly
