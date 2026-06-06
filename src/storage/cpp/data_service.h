#pragma once

#include <storage/cpp/index_entry.h>
#include <storage/cpp/data_reader.h>
#include <storage/cpp/write_back_queue.h>
#include <storage/cpp/temp_store.h>
#include <common/cpp/common_types.h>
#include <common/cpp/concurrent_map.h>
#include <cstdint>
#include <mutex>
#include <utility>
#include <network/cpp/io_thread_pool.h>
#include <atomic>
#include <functional>
#include <string>
#include <condition_variable>

namespace fly {

struct RemoteObjectMeta {
    CMVector<uint64_t> workers;
    uint64_t read_count = 0;
    int64_t last_access_time = 0;   // epoch seconds
};

struct BackupDecision {
    bool should_backup = false;
    uint32_t current_replicas = 0;
    uint32_t target_replicas = 0;
};

struct RemoteObjectInfo {
    uint64_t worker_id = 0;
    CMString host;
    int32_t port = 0;
};

struct TransferResult {
    uint64_t conn_id = 0;
    CMString object_name;
    bool success = false;
    CMString error_message;
    CMString compressed_data;
    CMString py_name;
    CMString write_context_hash;
};

enum class CompletionState {
    INCOMPLETE,
    COMPLETE,
    FAILED
};

struct LocalObjectInfo {
    CMString db_id;
    CMVector<IndexEntry> entries;
    bool flushed = false;
    CompletionState completion_state = CompletionState::INCOMPLETE;
    CMString error_message;
    bool is_temp = false;
    CMString temp_compressed_data;

    std::mutex cv_mutex;
    std::condition_variable cv;
};

class DataService : public std::enable_shared_from_this<DataService> {
public:
    static DataService& instance();
    static CMSharedPtr<DataService> instance_ptr();

    using RemoteCompressedReadCallback = std::function<std::tuple<bool, CMString, CMString, bool>(
        const CMString& object_name)>;
    using DirectCompressedReadCallback = std::function<std::tuple<bool, CMString, CMString, CMString>(
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

    void update_remote_idx(const CMString& object_name,
                            uint64_t worker_id,
                            const CMString& host,
                            int32_t port);

    void add_remote_location(const CMString& object_name, uint64_t worker_id);
    void remove_remote_location(const CMString& object_name);
    void remove_remote_location(const CMString& object_name, uint64_t worker_id);
    CMVector<uint64_t> get_remote_workers(const CMString& object_name) const;

    bool has_remote_location(const CMString& object_name) const;
    RemoteObjectInfo lookup_remote_idx(const CMString& object_name) const;

    void remove_remote_index(const CMString& object_name);

    // ============================================================
    // Worker Registry
    // ============================================================

    void register_worker(uint64_t worker_id,
                            const CMString& host,
                            int32_t port);

    RemoteObjectInfo get_worker_address(uint64_t worker_id) const;

    // ============================================================
    // Read Operations (3-tier fallback)
    // ============================================================

    void set_remote_compressed_read_handler(RemoteCompressedReadCallback cb);
    void set_direct_compressed_read_handler(DirectCompressedReadCallback cb);

    std::pair<bool, ReadResult> try_read_local(const CMString& object_name);

    std::pair<bool, CMString> try_read_local_raw(const CMString& object_name);

    std::tuple<bool, CMString, CMString> try_read_local_raw_or_wait(
        const CMString& object_name, int timeout_ms = -1);

    std::pair<bool, ReadResult> try_read_remote(const CMString& object_name);

    std::pair<bool, ReadResult> try_read_local_or_wait(const CMString& object_name,
                                                        int timeout_ms = 3000);

    std::tuple<bool, CMString, CMString, CMString, bool> read_raw_compressed(const CMString& object_name);

    void mark_temp_entry(const CMString& object_name, const CMString& compressed_data);
    void unmark_temp_entry(const CMString& object_name);
    bool is_temp_entry(const CMString& object_name) const;
    std::pair<bool, CMString> get_temp_data(const CMString& object_name) const;

    void on_temp_write(const CMString& db_id, const CMString& object_name, CMString&& compressed_data);
    void cleanup_temp_entries(const CMString& db_id);

    // ============================================================
    // Transfer Server
    // ============================================================

    using TransferCallback = std::function<void(const TransferResult&)>;

    void start_transfer_server(int thread_count, TransferCallback callback);
    void stop_transfer_server();
    bool is_transfer_server_running() const;
    void submit_transfer(uint64_t conn_id, const CMString& object_name);
    CMSharedPtr<IOThreadPool> get_transfer_pool() const;

    // ============================================================
    // Write-Back Queue
    // ============================================================

    void start_write_back();
    void stop_write_back();
    void enqueue_write_back(fly::WriteRequest&& task);
    void drain_write_back();
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
    DataService() = default;
    ~DataService();

    struct Creator_;
    friend struct Creator_;

    struct DbPaths {
        CMString base_path;
        CMString data_path;
        CMString writer_id;
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
    CMString do_read_raw_entries(const CMVector<IndexEntry>& entries,
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

    ConcurrentUnorderedMap<CMString, CMString> temp_entries_;

    CMVector<CMString> temp_lru_order_;
    int64_t temp_total_bytes_ = 0;
    int64_t temp_max_bytes_ = 0;
    CMUniquePtr<fly::TempStore> temp_eviction_store_;

    CMSharedPtr<IOThreadPool> transfer_pool_;
    TransferCallback transfer_callback_;
    std::atomic<bool> transfer_running_{false};

    CMUniquePtr<fly::WriteBackQueue> write_back_queue_;

    RemoteCompressedReadCallback remote_compressed_read_handler_;
    DirectCompressedReadCallback direct_compressed_read_handler_;
};

}  // namespace fly
