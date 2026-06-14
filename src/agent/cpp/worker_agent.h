#pragma once

#include <network/cpp/reactor.h>
#include <network/cpp/transport.h>
#include <network/cpp/message_types.h>
#include <network/cpp/data_client.h>
#include <network/cpp/data_client_pool.h>
#include <core/cpp/config.h>
#include <agent/cpp/task_executor.h>
#include <common/cpp/worker_context.h>
#include <storage/cpp/database.h>
#include <common/cpp/common_types.h>
#include <cstdint>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <filesystem>

namespace fly {

struct PendingTask {
    uint64_t task_id_;
    CMString task_name_;
    CMString task_module_;
    CMVector<CMString> args_;
    CMString write_context_hash_;
};

struct PendingDbPath {
    CMString db_id_;
    CMString base_path_;
    CMString data_path_;
    bool completed_ = false;
    bool success_ = false;
};

struct PendingWriteRegister {
    CMString object_name_;
    bool completed_ = false;
    bool success_ = false;
    CMString error_message_;
    TaskErrorType error_type_ = TaskErrorType::UNKNOWN;
};

struct PendingBackup {
    CMString object_name_;
    CMString db_id_;
    bool completed_ = false;
    bool success_ = false;
};

class WorkerAgent {
public:
    WorkerAgent(uint64_t worker_id, const CMString& master_host, uint16_t master_port,
                const CMVector<CMString>& attributes = {});
    ~WorkerAgent();
    
    void start();
    void stop();
    bool is_running() const;
    uint64_t get_worker_id() const;
    
    void set_executor(CMSharedPtr<TaskExecutor> executor);

    void begin_task(uint64_t task_id, const CMString& write_context_hash = "");
    void record_write(const CMString& db_id, const CMString& object_name);
    CMVector<CMString> end_task(uint64_t task_id);
    
    bool is_registered() const;
    
    void submit_task(const CMString& name, const CMString& module,
                     const CMVector<CMString>& args,
                     const CMVector<CMString>& inputs,
                     const CMVector<CMString>& required_capabilities = {},
                     const CMString& write_context_hash = "");
    
    bool has_pending_task() const;
    bool poll_task();
    bool poll_task_blocking(int timeout_ms = 100);
    
    void register_database(const CMString& db_id, CMSharedPtr<Database> db);
    CMSharedPtr<Database> get_database(const CMString& db_id) const;
    
    std::tuple<bool, CMString, CMString, bool> request_remote_data(const CMString& object_name);
    std::pair<bool, ReadResult> request_data_from_worker(const CMString& host, int32_t port,
                                                          const CMString& object_name);

    bool request_db_path(const CMString& db_id);

    std::pair<CMString, TaskErrorType> register_write_with_master(const CMString& db_id, const CMString& object_name);
    void request_database_freeze(const CMString& db_id);
    void request_object_remove(const CMString& db_id, const CMString& object_name);
    void request_backup(const CMString& db_id, const CMString& object_name);

    void set_worker_property(const CMString& prop);
    void set_worker_property(const CMVector<CMString>& props);
    void remove_worker_property(const CMString& prop);
    void remove_worker_property(const CMVector<CMString>& props);
    CMVector<CMString> get_worker_properties() const;

private:
    uint64_t worker_id_;
    CMString master_host_;
    uint16_t master_port_;
    CMVector<CMString> attributes_;
    mutable std::mutex attributes_mutex_;
    std::atomic<bool> running_{false};
    std::atomic<bool> registered_{false};
    std::atomic<bool> shutdown_triggered_{false};
    
    CMUniquePtr<Reactor> reactor_;
    std::thread reactor_thread_;
    uint64_t master_conn_;
    CMString data_server_host_;
    int32_t data_server_port_ = 0;
    
    std::thread heartbeat_thread_;
    std::atomic<bool> heartbeat_running_{false};
    std::mutex heartbeat_mutex_;
    std::condition_variable heartbeat_cv_;
    
    CMSharedPtr<TaskExecutor> executor_;

    uint64_t current_task_id_ = 0;
    CMVector<CMString> current_writes_;
    CMString current_write_hash_;
    
    mutable std::mutex task_queue_mutex_;
    std::condition_variable task_queue_cv_;
    std::queue<PendingTask> task_queue_;
    std::atomic<int> outstanding_tasks_{0};
    
    CMUnorderedMap<CMString, CMSharedPtr<Database>> databases_;

    std::mutex pending_db_path_mutex_;
    std::condition_variable pending_db_path_cv_;
    CMUnorderedMap<CMString, CMSharedPtr<PendingDbPath>> pending_db_paths_;

    std::mutex pending_write_reg_mutex_;
    std::condition_variable pending_write_reg_cv_;
    CMUnorderedMap<CMString, CMSharedPtr<PendingWriteRegister>> pending_write_regs_;

    struct PendingRemove {
        std::mutex mutex_;
        std::condition_variable cv_;
        bool completed_ = false;
        bool success_ = false;
    };

    std::mutex pending_remove_mutex_;
    CMUnorderedMap<CMString, CMSharedPtr<PendingRemove>> pending_removes_;

    void on_register_ack(const RegisterAckMessage& msg);
    void on_task_assign(const TaskAssignMessage& msg);
    void on_shutdown(const ShutdownMessage& msg);
    void on_db_path_response(const DbPathResponseMessage& msg);
    void on_write_register_ack(uint64_t conn_id, const WriteRegisterAckMessage& msg);
    void on_object_removed(uint64_t conn_id, const ObjectRemovedMessage& msg);
    void on_remove_ack(uint64_t conn_id, const RemoveAckMessage& msg);
    void on_remove_command(uint64_t conn_id, const RemoveCommandMessage& msg);
    void on_idx_load_command(uint64_t conn_id, const IdxLoadCommandMessage& msg);
    void on_database_freeze_notification(uint64_t conn_id, const DatabaseFreezeNotification& msg);
    void execute_internal_task(const PendingTask& task);
    void on_disconnect(uint64_t conn_id);
    
    void heartbeat_loop();
    void touch_master_contact();
    void initiate_shutdown(const CMString& reason);
    void do_cleanup();

    DataClientPool data_client_pool_{Config::instance()->get_int("data_client_pool_size")};

    // Master liveness tracking — seconds since epoch (atomic for cross-thread access)
    std::atomic<int64_t> last_master_contact_{0};
    static constexpr int MASTER_TIMEOUT_SECONDS = 120;
};

}  // namespace fly