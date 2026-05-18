#pragma once

#include <network/cpp/reactor.h>
#include <network/cpp/transport.h>
#include <network/cpp/message_types.h>
#include <network/cpp/data_client.h>
#include <agent/cpp/task_executor.h>
#include <agent/cpp/worker_context.h>
#include <storage/cpp/database.h>
#include <common/cpp/common_types.h>
#include <cstdint>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <map>

namespace fly {

struct PendingTask {
    uint64_t task_id;
    CMString task_name;
    CMString task_module;
    CMVector<CMString> args;
};

struct PendingDbPath {
    CMString db_id;
    CMString base_path;
    CMString data_path;
    bool completed = false;
    bool success = false;
};

struct PendingWriteRegister {
    CMString object_name;
    bool completed = false;
    bool success = false;
    CMString error_message;
    TaskErrorType error_type = TaskErrorType::UNKNOWN;
};

class WorkerAgent {
public:
    WorkerAgent(uint64_t worker_id, const CMString& master_host, uint16_t master_port);
    ~WorkerAgent();
    
    void start();
    void stop();
    bool is_running() const;
    uint64_t get_worker_id() const;
    
    void set_executor(std::shared_ptr<TaskExecutor> executor);

    void set_data_service(DataService* ds);
    
    void begin_task(uint64_t task_id);
    void record_write(const CMString& db_id, const CMString& object_name);
    CMVector<CMString> end_task(uint64_t task_id);
    
    bool is_registered() const;
    
    void submit_task(const CMString& name, const CMString& module,
                     const CMVector<CMString>& args,
                     const CMVector<CMString>& inputs);
    
    bool has_pending_task() const;
    bool poll_task();
    
    void register_database(const CMString& db_id, std::shared_ptr<Database> db);
    std::shared_ptr<Database> get_database(const CMString& db_id) const;
    
    ReadResult request_remote_data(const CMString& object_name);
    ReadResult request_data_from_worker(const CMString& host, int32_t port,
                                         const CMString& object_name);

    bool request_db_path(const CMString& db_id);

    void register_write_with_master(const CMString& db_id, const CMString& object_name);

private:
    uint64_t worker_id_;
    CMString master_host_;
    uint16_t master_port_;
    std::atomic<bool> running_{false};
    std::atomic<bool> registered_{false};
    
    std::unique_ptr<Reactor> reactor_;
    std::thread reactor_thread_;
    uint64_t master_conn_;
    int32_t data_server_port_ = 0;
    
    std::thread heartbeat_thread_;
    std::atomic<bool> heartbeat_running_{false};
    std::mutex heartbeat_mutex_;
    std::condition_variable heartbeat_cv_;
    
    std::shared_ptr<TaskExecutor> executor_;
    
    static void record_write_trampoline(void* ctx, const CMString& db_id, const CMString& name);
    static void register_write_trampoline(void* ctx, const CMString& db_id, const CMString& name);
    
    uint64_t current_task_id_ = 0;
    CMVector<CMString> current_writes_;
    
    mutable std::mutex task_queue_mutex_;
    std::queue<PendingTask> task_queue_;
    
    CMMap<CMString, std::shared_ptr<Database>> databases_;

    std::mutex pending_db_path_mutex_;
    CMMap<CMString, std::shared_ptr<PendingDbPath>> pending_db_paths_;

    std::mutex pending_write_reg_mutex_;
    CMMap<CMString, std::shared_ptr<PendingWriteRegister>> pending_write_regs_;

    void on_register_ack(const RegisterAckMessage& msg);
    void on_task_assign(const TaskAssignMessage& msg);
    void on_shutdown(const ShutdownMessage& msg);
    void on_db_path_response(const DbPathResponseMessage& msg);
    void on_data_request(uint64_t conn_id, const DataRequestMessage& msg);
    void on_write_register_ack(uint64_t conn_id, const WriteRegisterAckMessage& msg);
    void on_disconnect(uint64_t conn_id);
    
    void heartbeat_loop();
    void touch_master_contact();
    void initiate_shutdown(const CMString& reason);

    DataService* data_service_ = nullptr;

    DataService& ds();

    // Master liveness tracking — seconds since epoch (atomic for cross-thread access)
    std::atomic<int64_t> last_master_contact_{0};
    static constexpr int MASTER_TIMEOUT_SECONDS = 60;
};

}  // namespace fly