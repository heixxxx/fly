#include <export/cpp/export_macros.h>
#include <task/cpp/dependency_graph.h>
#include <task/cpp/worker_manager.h>
#include <task/cpp/task_scheduler.h>
#include <task/cpp/task_manager.h>
#include <task/cpp/heartbeat_monitor.h>
#include <memory>

FLY_EXPORT_MODULE(_fly_task) {

FLY_EXPORT_ENUM(fly::WorkerStatus, "EXTaskWorkerStatus")
    FLY_EXPORT_ENUM_VALUE("IDLE", fly::WorkerStatus::IDLE)
    FLY_EXPORT_ENUM_VALUE("BUSY", fly::WorkerStatus::BUSY)
    FLY_EXPORT_ENUM_VALUE("DEAD", fly::WorkerStatus::DEAD);

FLY_EXPORT_ENUM(fly::TaskStatus, "EXTaskTaskStatus")
    FLY_EXPORT_ENUM_VALUE("PENDING", fly::TaskStatus::PENDING)
    FLY_EXPORT_ENUM_VALUE("RUNNING", fly::TaskStatus::RUNNING)
    FLY_EXPORT_ENUM_VALUE("COMPLETED", fly::TaskStatus::COMPLETED)
    FLY_EXPORT_ENUM_VALUE("FAILED", fly::TaskStatus::FAILED)
    FLY_EXPORT_ENUM_VALUE("CANCELLED", fly::TaskStatus::CANCELLED);

FLY_EXPORT_CLASS(fly::WorkerInfo, "EXTaskWorkerInfo")
    FLY_EXPORT_READONLY_ATTR("worker_id", &fly::WorkerInfo::worker_id_)
    FLY_EXPORT_READONLY_ATTR("address", &fly::WorkerInfo::address_)
    FLY_EXPORT_READONLY_ATTR("port", &fly::WorkerInfo::port_)
    FLY_EXPORT_READONLY_ATTR("status", &fly::WorkerInfo::status_)
    FLY_EXPORT_READONLY_ATTR("capabilities", &fly::WorkerInfo::capabilities_)
    FLY_EXPORT_READONLY_ATTR("last_heartbeat", &fly::WorkerInfo::last_heartbeat_)
    FLY_EXPORT_READONLY_ATTR("current_task_id", &fly::WorkerInfo::current_task_id_);

FLY_EXPORT_CLASS(fly::TaskMetadata, "EXTaskTaskMetadata")
    FLY_EXPORT_READONLY_ATTR("task_id", &fly::TaskMetadata::task_id_)
    // 提交字段现统一存储在 submission_，通过 lambda getter 暴露保持 Python API 不变。
    FLY_EXPORT_READONLY_PROPERTY("name", [](const fly::TaskMetadata& m) { return m.submission_.name_; })
    FLY_EXPORT_READONLY_ATTR("status", &fly::TaskMetadata::status_)
    FLY_EXPORT_READONLY_PROPERTY("inputs", [](const fly::TaskMetadata& m) { return m.submission_.inputs_; })
    FLY_EXPORT_READONLY_PROPERTY("outputs", [](const fly::TaskMetadata& m) { return m.submission_.outputs_; })
    FLY_EXPORT_READONLY_ATTR("config", &fly::TaskMetadata::config_)
    FLY_EXPORT_READONLY_PROPERTY("required_capabilities", [](const fly::TaskMetadata& m) { return m.submission_.required_capabilities_; })
    FLY_EXPORT_READONLY_PROPERTY("owner_db_path", [](const fly::TaskMetadata& m) { return m.submission_.owner_db_path_; })
    FLY_EXPORT_READONLY_ATTR("created_at", &fly::TaskMetadata::created_at_)
    FLY_EXPORT_READONLY_ATTR("started_at", &fly::TaskMetadata::started_at_)
    FLY_EXPORT_READONLY_ATTR("completed_at", &fly::TaskMetadata::completed_at_)
    FLY_EXPORT_READONLY_ATTR("error_message", &fly::TaskMetadata::error_message_)
    FLY_EXPORT_READONLY_ATTR("assigned_worker_id", &fly::TaskMetadata::assigned_worker_id_);

FLY_EXPORT_CLASS(fly::ScheduleResult, "EXTaskScheduleResult")
    FLY_EXPORT_INIT()
    FLY_EXPORT_ATTR("task_id", &fly::ScheduleResult::task_id_)
    FLY_EXPORT_ATTR("worker_id", &fly::ScheduleResult::worker_id_)
    FLY_EXPORT_ATTR("scheduled", &fly::ScheduleResult::scheduled_)
    FLY_EXPORT_ATTR("degraded", &fly::ScheduleResult::degraded_);

FLY_EXPORT_CLASS(fly::DependencyGraph, "EXTaskDependencyGraph")
    FLY_EXPORT_INIT()
    FLY_EXPORT_METHOD("add_task", [](fly::DependencyGraph& self, uint64_t task_id, const fly::CMVector<fly::CMString>& inputs) {
        self.add_task(task_id, inputs);
    })
    FLY_EXPORT_METHOD("add_task_with_requirements", [](fly::DependencyGraph& self, uint64_t task_id, const fly::CMVector<fly::CMString>& inputs, const fly::CMVector<fly::CMString>& required_capabilities, float attribute_timeout, int priority) {
        fly::TaskRequirements reqs;
        reqs.capabilities_ = required_capabilities;
        reqs.timeout_seconds_ = attribute_timeout;
        reqs.priority_ = priority;
        self.add_task(task_id, inputs, reqs);
    })
    FLY_EXPORT_METHOD("mark_data_ready", [](fly::DependencyGraph& self, const fly::CMString& data_path) {
        self.mark_data_ready(data_path);
    })
    FLY_EXPORT_METHOD("get_ready_tasks", &fly::DependencyGraph::get_ready_tasks)
    FLY_EXPORT_METHOD("is_task_ready", &fly::DependencyGraph::is_task_ready)
    FLY_EXPORT_METHOD("get_task_requirements", [](fly::DependencyGraph& self, uint64_t task_id) -> fly::CMVector<fly::CMString> {
        return self.get_task_requirements(task_id).capabilities_;
    })
    FLY_EXPORT_METHOD("remove_task", &fly::DependencyGraph::remove_task);

FLY_EXPORT_CLASS(fly::WorkerManager, "EXTaskWorkerManager")
    FLY_EXPORT_INIT()
    FLY_EXPORT_METHOD("register_worker", [](fly::WorkerManager& self, uint64_t worker_id, const fly::CMString& address, uint16_t port, const fly::CMVector<fly::CMString>& capabilities) {
        self.register_worker(worker_id, address, port, capabilities);
    })
    FLY_EXPORT_METHOD("unregister_worker", &fly::WorkerManager::unregister_worker)
    FLY_EXPORT_METHOD("update_worker_status", &fly::WorkerManager::update_worker_status)
    FLY_EXPORT_METHOD("record_heartbeat", &fly::WorkerManager::record_heartbeat)
    FLY_EXPORT_METHOD("assign_task", &fly::WorkerManager::assign_task)
    FLY_EXPORT_METHOD("complete_task", &fly::WorkerManager::complete_task)
    FLY_EXPORT_METHOD("get_idle_workers", &fly::WorkerManager::get_idle_workers)
    FLY_EXPORT_METHOD("get_workers_with_capability", &fly::WorkerManager::get_workers_with_capability)
    FLY_EXPORT_METHOD("get_worker_count", &fly::WorkerManager::get_worker_count)
    FLY_EXPORT_METHOD("get_idle_worker_count", &fly::WorkerManager::get_idle_worker_count);

FLY_EXPORT_CLASS(fly::TaskScheduler, "EXTaskTaskScheduler")
    FLY_EXPORT_INIT(fly::DependencyGraph*, fly::WorkerManager*)
    FLY_EXPORT_METHOD("schedule_next", &fly::TaskScheduler::schedule_next)
    FLY_EXPORT_METHOD("schedule_all_available", &fly::TaskScheduler::schedule_all_available)
    FLY_EXPORT_METHOD("set_locality_preference", &fly::TaskScheduler::set_locality_preference);

FLY_EXPORT_CLASS(fly::TaskManager, "EXTaskManager")
    FLY_EXPORT_INIT()
    FLY_EXPORT_METHOD("create_task", [](fly::TaskManager& self, uint64_t task_id, const fly::CMString& name, const fly::CMVector<fly::CMString>& inputs, const fly::CMVector<fly::CMString>& outputs, const fly::CMString& config, const fly::CMVector<fly::CMString>& required_capabilities, float attribute_timeout, int priority) {
        fly::TaskSubmissionSpec spec;
        spec.name_ = name;
        spec.inputs_ = inputs;
        spec.outputs_ = outputs;
        spec.required_capabilities_ = required_capabilities;
        spec.attribute_timeout_ = attribute_timeout;
        spec.priority_ = priority;
        self.create_task(task_id, spec, config);
    })
    FLY_EXPORT_METHOD("update_task_status", &fly::TaskManager::update_task_status)
    FLY_EXPORT_METHOD("set_error", &fly::TaskManager::set_error)
    FLY_EXPORT_METHOD("set_assigned_worker", &fly::TaskManager::set_assigned_worker)
    FLY_EXPORT_METHOD("set_timestamps", &fly::TaskManager::set_timestamps)
    FLY_EXPORT_METHOD("set_write_context_hash", &fly::TaskManager::set_write_context_hash)
    FLY_EXPORT_METHOD("fail_task", &fly::TaskManager::fail_task)
    FLY_EXPORT_METHOD("assign_task", &fly::TaskManager::assign_task)
    FLY_EXPORT_METHOD("unassign_task", &fly::TaskManager::unassign_task)
    FLY_EXPORT_METHOD("get_tasks_by_status", &fly::TaskManager::get_tasks_by_status)
    FLY_EXPORT_METHOD("get_all_tasks", &fly::TaskManager::get_all_tasks)
    FLY_EXPORT_METHOD("has_tasks_with_status", &fly::TaskManager::has_tasks_with_status)
    FLY_EXPORT_METHOD("count_tasks_by_status", &fly::TaskManager::count_tasks_by_status)
    FLY_EXPORT_METHOD("get_task_ids_by_status", &fly::TaskManager::get_task_ids_by_status)
    FLY_EXPORT_METHOD("has_task", &fly::TaskManager::has_task)
    FLY_EXPORT_METHOD("remove_task", &fly::TaskManager::remove_task);

FLY_EXPORT_CLASS(fly::HeartbeatMonitor, "EXTaskHeartbeatMonitor")
    FLY_EXPORT_INIT(fly::WorkerManager*, uint64_t)
    // 保留单参签名（Python/QA 兼容）：exempt 默认空（宽限豁免由 master 内部传入）。
    FLY_EXPORT_METHOD("check_all_workers",
        [](fly::HeartbeatMonitor& self, uint64_t now) { self.check_all_workers(now, {}); })
    FLY_EXPORT_METHOD("check_all_workers",
        [](fly::HeartbeatMonitor& self, uint64_t now,
           const fly::CMVector<uint64_t>& exempt) { self.check_all_workers(now, exempt); })
    FLY_EXPORT_METHOD("get_timeout", &fly::HeartbeatMonitor::get_timeout)
    FLY_EXPORT_METHOD("set_timeout", &fly::HeartbeatMonitor::set_timeout)
    FLY_EXPORT_METHOD("get_dead_workers", &fly::HeartbeatMonitor::get_dead_workers);

}