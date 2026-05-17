#pragma once

#include <common/cpp/common_types.h>
#include <cstdint>

namespace fly {

enum class TaskStatus : uint8_t {
    PENDING = 0,
    RUNNING = 1,
    COMPLETED = 2,
    FAILED = 3,
    CANCELLED = 4,
};

struct TaskMetadata {
    uint64_t task_id;
    CMString name;
    TaskStatus status;
    CMVector<CMString> inputs;
    CMVector<CMString> outputs;
    CMString config;
    uint64_t created_at;
    uint64_t started_at;
    uint64_t completed_at;
    CMString error_message;
    uint64_t assigned_worker_id;
};

struct DataLocation {
    uint64_t worker_id = 0;
    CMString data_host;
    int32_t data_port = 0;
};

class MetadataManager {
public:
    void create_task(uint64_t task_id, const CMString& name,
                     const CMVector<CMString>& inputs,
                     const CMVector<CMString>& outputs,
                     const CMString& config);
    void update_task_status(uint64_t task_id, TaskStatus status);
    void set_error(uint64_t task_id, const CMString& error);
    void set_assigned_worker(uint64_t task_id, uint64_t worker_id);
    void set_timestamps(uint64_t task_id, uint64_t created, uint64_t started, uint64_t completed);
    
    TaskMetadata* get_task(uint64_t task_id);
    CMVector<TaskMetadata> get_tasks_by_status(TaskStatus status);
    CMVector<TaskMetadata> get_all_tasks();
    bool has_task(uint64_t task_id);
    void remove_task(uint64_t task_id);
    
    // Data location tracking: object_name → which worker has it
    void record_data_location(const CMString& object_name, uint64_t worker_id);
    bool has_data_location(const CMString& object_name) const;
    DataLocation query_data_location(const CMString& object_name) const;
    
    // Worker data server registration: worker_id → (host, port)
    void register_worker_data_server(uint64_t worker_id, const CMString& host, int32_t port);
    DataLocation get_worker_data_server(uint64_t worker_id) const;
    
private:
    CMMap<uint64_t, TaskMetadata> tasks_;
    // object_name → worker_id (which worker wrote this object)
    CMMap<CMString, uint64_t> object_to_worker_;
    // worker_id → DataLocation (host:port of worker's data server)
    CMMap<uint64_t, DataLocation> worker_data_servers_;
};

}  // namespace fly