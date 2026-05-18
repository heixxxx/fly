#pragma once

#include <storage/cpp/index_entry.h>
#include <storage/cpp/data_reader.h>
#include <common/cpp/common_types.h>
#include <cstdint>
#include <mutex>
#include <utility>
#include <network/cpp/io_thread_pool.h>
#include <atomic>
#include <functional>
#include <string>
#include <condition_variable>

namespace fly {

struct RemoteObjectInfo {
    uint64_t worker_id = 0;
    CMString host;
    int32_t port = 0;
};

struct TransferResult {
    uint64_t conn_id = 0;
    CMString object_name;
    bool success = false;
    CMString data;
    CMString error_message;
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

    std::mutex cv_mutex;
    std::condition_variable cv;
};

class DataService {
public:
    static DataService& instance();

    using RemoteReadCallback = std::function<ReadResult(const CMString& object_name)>;
    using DirectReadCallback = std::function<ReadResult(const CMString& host, int32_t port,
                                                         const CMString& object_name)>;

    void set_remote_read_handler(RemoteReadCallback cb);
    void set_direct_read_handler(DirectReadCallback cb);

    void register_database(const CMString& db_id,
                            const CMString& base_path,
                            const CMString& data_path,
                            uint64_t writer_id = 0);

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

    void update_remote_idx(const CMString& object_name,
                            uint64_t worker_id,
                            const CMString& host,
                            int32_t port);

    bool has_remote_location(const CMString& object_name) const;
    RemoteObjectInfo lookup_remote_idx(const CMString& object_name) const;

    void register_worker(uint64_t worker_id,
                            const CMString& host,
                            int32_t port);

    RemoteObjectInfo get_worker_address(uint64_t worker_id) const;

    std::pair<bool, ReadResult> try_read_local(const CMString& object_name);

    std::pair<bool, ReadResult> try_read_local_or_wait(const CMString& object_name,
                                                        int timeout_ms = 3000);

    ReadResult read_raw(const CMString& object_name, int max_retries = 3);

    bool has_local_object(const CMString& object_name) const;

    using TransferCallback = std::function<void(const TransferResult&)>;

    void start_transfer_server(int thread_count, TransferCallback callback);
    void stop_transfer_server();
    bool is_transfer_server_running() const;
    void submit_transfer(uint64_t conn_id, const CMString& object_name);
    std::shared_ptr<IOThreadPool> get_transfer_pool() const;

private:
    DataService() = default;

    struct DbPaths {
        CMString base_path;
        CMString data_path;
        uint64_t writer_id = 0;
    };

    mutable std::mutex mutex_;

    CMUnorderedMap<CMString, std::shared_ptr<LocalObjectInfo>> local_idx_;

    CMUnorderedMap<CMString, RemoteObjectInfo> remote_idx_;

    CMMap<uint64_t, RemoteObjectInfo> worker_registry_;

    CMUnorderedMap<CMString, DbPaths> db_paths_;

    std::shared_ptr<IOThreadPool> transfer_pool_;
    TransferCallback transfer_callback_;
    std::atomic<bool> transfer_running_{false};

    RemoteReadCallback remote_read_handler_;
    DirectReadCallback direct_read_handler_;
};

}  // namespace fly
