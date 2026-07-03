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

// Canonical db_id length. db_id is base62 (4 path-hash + 6 random chars),
// followed by ':' then the short object name. Used to split a full object name
// at a fixed offset (no separator scan). Defined as a function over an internal
// constant so the length stays an implementation detail of the .cpp.
inline constexpr size_t db_id_len() { return 10; }

// Split a full name "db_id:short_name" into (db_id, short_name) at the fixed
// db_id offset. The separator at position db_id_len() must be ':'.
// Returns {db_id, short_name}; db_id 为空表示输入过短或格式错误（调用方判空即可）。
// 允许 short_name 为空（"db_id:" 是 "clear all" 哨兵）。
inline std::pair<CMString, CMString> split_full_name(const CMString& full) {
    if (full.size() < db_id_len() + 1 || full[db_id_len()] != ':') {
        return {{}, {}};
    }
    return {full.substr(0, db_id_len()), full.substr(db_id_len() + 1)};
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
    CMString db_id_;
    CMVector<IndexEntry> entries_;
    bool flushed_ = false;
    CompletionState completion_state_ = CompletionState::INCOMPLETE;
    CMString error_message_;
    bool is_temp_ = false;
    FlyBufferPtr temp_compressed_data_;  // shared_ptr: zero-copy reads, automatic lifetime

    std::mutex cv_mutex_;
    std::condition_variable cv_;
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

    void register_database(const CMString& db_id,
                            const CMString& base_path,
                            const CMString& data_path,
                            const CMString& writer_id = "");

    void unregister_database(const CMString& db_id);

    bool has_database(const CMString& db_id) const;

    // ============================================================
    // Local Index Management
    // ============================================================

    void on_object_written(const CMString& db_id,
                            const CMString& object_name,
                            const IndexEntry& entry);

    void on_flush(const CMString& db_id);

    void on_write_started(const CMString& db_id,
                           const CMString& object_name);

    void on_write_completed(const CMString& db_id,
                             const CMString& object_name,
                             const CMVector<IndexEntry>& entries);

    void on_write_failed(const CMString& db_id,
                          const CMString& object_name,
                          const CMString& error_message);

    void remove_local_index(const CMString& object_name);

    bool has_local_object(const CMString& object_name) const;

    void on_object_flushed(const CMString& object_name);

    void restore_entries(const CMString& db_id,
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

    std::pair<bool, FlyBufferPtr> try_read_local_raw(const CMString& object_name);

    bool is_write_in_progress(const CMString& object_name) const;

    std::tuple<bool, FlyBufferPtr, CMString> try_read_local_raw_or_wait(
        const CMString& object_name, int timeout_ms = -1);

    std::pair<bool, ReadResult> try_read_remote(const CMString& object_name);

    std::pair<bool, ReadResult> try_read_local_or_wait(const CMString& object_name,
                                                        int timeout_ms = 3000);

    std::tuple<bool, FlyBufferPtr, CMString, CMString, bool> read_raw_compressed(const CMString& object_name);

    void on_temp_write_started(const CMString& db_id, const CMString& object_name);
    void on_temp_write(const CMString& db_id, const CMString& object_name, FlyBufferPtr compressed_data);
    void cleanup_temp_entries(const CMString& db_id);

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
        CMString base_path_;
        CMString data_path_;
        CMString writer_id_;
    };

    // ============================================================
    // Private Helpers — Name Parsing
    // ============================================================

    static std::pair<CMString, CMString> split_full(const CMString& full);
    CMString get_db_id_for_object(const CMString& object_name) const;

    // ============================================================
    // Private Helpers — Read Operations
    // ============================================================

    ReadResult do_read_local_entries(const CMVector<IndexEntry>& entries,
                                     const DbPaths& paths);
    FlyBufferPtr do_read_raw_entries(const CMVector<IndexEntry>& entries,
                                 const DbPaths& paths);

    mutable std::mutex mutex_;

    CMUnorderedMap<CMString /*db_id*/,
        CMUnorderedMap<CMString /*short_name*/,
            CMSharedPtr<LocalObjectInfo>>> local_idx_;

    CMUnorderedMap<CMString /*db_id*/,
        CMUnorderedMap<CMString /*short_name*/,
            RemoteObjectMeta>> remote_idx_;

    CMUnorderedMap<uint64_t, RemoteObjectInfo> worker_registry_;

    CMUnorderedMap<CMString, DbPaths> db_paths_;

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
