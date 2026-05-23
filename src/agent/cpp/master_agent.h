#pragma once

#include <network/cpp/reactor.h>
#include <network/cpp/transport.h>
#include <network/cpp/message_types.h>
#include <network/cpp/data_client.h>
#include <storage/cpp/database.h>
#include <storage/cpp/data_service.h>
#include <task/cpp/dependency_graph.h>
#include <task/cpp/worker_manager.h>
#include <task/cpp/task_scheduler.h>
#include <task/cpp/task_manager.h>
#include <task/cpp/heartbeat_monitor.h>
#include <log/cpp/logger.h>
#include <common/cpp/common_types.h>
#include <common/cpp/worker_context.h>
#include <serialization/cpp/serialization_macros.h>
#include <cstdint>
#include <thread>
#include <atomic>
#include <map>
#include <set>
#include <memory>

namespace fly {

struct FailedTaskRecord {
    uint64_t task_id = 0;
    CMString name;
    CMString module;
    CMVector<CMString> args;
    CMVector<CMString> inputs;
    CMVector<CMString> outputs;
    CMVector<CMString> required_capabilities;
    CMString error_message;

    FLY_SERIALIZE(task_id, name, module, args, inputs, outputs,
                  required_capabilities, error_message);
};

struct FailedTaskFile {
    CMVector<FailedTaskRecord> records;
    FLY_SERIALIZE(records);
};

class MasterAgent {
public:
    MasterAgent(const CMString& host, uint16_t port);
    ~MasterAgent();

    void start();
    void stop();
    bool is_running() const;

    void set_data_service(DataService* ds);


    CMVector<uint64_t> get_connected_workers() const;
    size_t get_connection_count() const;

    void submit_task(uint64_t task_id, const CMString& name,
                    const CMString& module, const CMVector<CMString>& args,
                    const CMVector<CMString>& inputs = {},
                    const CMVector<CMString>& outputs = {},
                    const CMVector<CMString>& required_capabilities = {});

    CMVector<uint64_t> get_pending_tasks() const;
    CMVector<uint64_t> get_running_tasks() const;
    CMVector<uint64_t> get_completed_tasks() const;
    CMVector<uint64_t> get_failed_tasks() const;
    CMString get_task_error(uint64_t task_id) const;

    CMVector<uint64_t> get_idle_workers() const;

    void restart_failed_tasks(const CMString& file_path);

    void broadcast_object_removed(const CMString& db_id, const CMString& object_name);

    uint16_t get_port() const { return port_; }
    int32_t get_data_server_port() const { return data_server_port_; }

    void register_database(const CMString& db_id, const CMString& base_path, const CMString& data_path = "");
    bool is_db_frozen(const CMString& db_id) const;
    CMSharedPtr<Database> get_or_create_database(const CMString& base_path, const CMString& data_path = "", uint64_t writer_id = 0);

    ReadResult request_remote_data(const CMString& object_name);
    ReadResult request_data_from_worker(const CMString& host, int32_t port,
                                         const CMString& object_name);

    void setup_write_context();

    // load_db support methods
    CMVector<IndexEntry> restore_master_idx(const CMString& db_id, const CMString& base_path, uint64_t writer_id);
    void send_idx_load_commands(const CMString& db_id, const CMString& base_path, const CMVector<uint64_t>& old_worker_ids);
    void rebuild_remote_idx(const CMString& db_id, const CMString& base_path, const CMVector<::WorkerInfo>& workers);

private:
    CMString host_;
    uint16_t port_;
    int32_t data_server_port_ = 0;
    std::atomic<bool> running_{false};

    CMUniquePtr<Reactor> reactor_;
    std::thread reactor_thread_;

    CMMap<uint64_t, uint64_t> conn_to_worker_;
    CMMap<uint64_t, uint64_t> worker_to_conn_;

    CMUniquePtr<DependencyGraph> graph_;
    CMUniquePtr<WorkerManager> worker_manager_;
    CMUniquePtr<TaskScheduler> scheduler_;
    CMUniquePtr<TaskManager> metadata_;
    CMUniquePtr<HeartbeatMonitor> heartbeat_monitor_;
    std::thread heartbeat_check_thread_;
    std::atomic<bool> heartbeat_check_running_{false};
    std::mutex heartbeat_check_mutex_;
    std::condition_variable heartbeat_check_cv_;

    CMMap<uint64_t, CMString> task_modules_;
    CMMap<uint64_t, CMVector<CMString>> task_args_;

    CMMap<CMString, CMMap<CMString, CMString>> db_registry_;
    CMMap<CMString, CMSharedPtr<Database>> db_instances_;
    CMSet<CMString> frozen_dbs_;
    static std::atomic<uint64_t> remote_task_counter_;

    void schedule_tasks();
    void assign_task_to_worker(uint64_t task_id, uint64_t worker_id);
    void heartbeat_check_loop();

    std::mutex schedule_mutex_;

    void on_worker_register(uint64_t conn_id, const RegisterMessage& msg);
    void on_heartbeat(uint64_t conn_id, const HeartbeatMessage& msg);
    void on_data_ready(uint64_t conn_id, const DataReadyMessage& msg);
    void on_task_complete(uint64_t conn_id, const TaskCompleteMessage& msg);
    void on_task_failed(uint64_t conn_id, const TaskFailedMessage& msg);
    void on_disconnect(uint64_t conn_id);
    void on_error(uint64_t conn_id, int error_code);
    void on_data_request(uint64_t conn_id, const DataRequestMessage& msg);
    void on_write_register(uint64_t conn_id, const WriteRegisterMessage& msg);
    void on_worker_property_update(uint64_t conn_id, const WorkerPropertyUpdateMessage& msg);
    void on_object_removed(uint64_t conn_id, const ObjectRemovedMessage& msg);

    void persist_failed_task(const FailedTaskRecord& record);
    void remove_persisted_task(uint64_t task_id);
    CMString get_failed_tasks_file_path() const;

    static void master_record_write_trampoline(void* ctx, const CMString& db_id, const CMString& name);
    void on_master_record_write(const CMString& db_id, const CMString& name);

    static void master_register_write_trampoline(void* ctx, const CMString& db_id, const CMString& name);
    void on_master_register_write(const CMString& db_id, const CMString& name);

    std::atomic<bool> fatal_error_{false};

    DataService* data_service_ = nullptr;

    DataService& ds();

    CMMap<uint64_t, CMString> worker_to_hostname_;
    CMMap<uint64_t, CMString> worker_to_ip_;
    CMString master_hostname_;
    CMSet<std::pair<CMString, uint64_t>> recorded_workers_;
};

}  // namespace fly
