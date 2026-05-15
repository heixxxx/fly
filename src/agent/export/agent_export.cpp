#include <export/cpp/export_macros.h>
#include <agent/cpp/task_executor.h>
#include <agent/cpp/master_agent.h>
#include <agent/cpp/worker_agent.h>
#include <memory>

FLY_EXPORT_MODULE(_fly_agent) {

FLY_EXPORT_ENUM(fly::TaskExecStatus, "EXTaskExecStatus")
    FLY_EXPORT_ENUM_VALUE("SUCCESS", fly::TaskExecStatus::SUCCESS)
    FLY_EXPORT_ENUM_VALUE("FAILED", fly::TaskExecStatus::FAILED)
    FLY_EXPORT_ENUM_VALUE("TIMEOUT", fly::TaskExecStatus::TIMEOUT);

FLY_EXPORT_CLASS(fly::TaskExecResult, "EXTaskExecResult")
    FLY_EXPORT_INIT()
    FLY_EXPORT_ATTR("task_id", &fly::TaskExecResult::task_id)
    FLY_EXPORT_ATTR("status", &fly::TaskExecResult::status)
    FLY_EXPORT_ATTR("output", &fly::TaskExecResult::output)
    FLY_EXPORT_ATTR("error", &fly::TaskExecResult::error);

FLY_EXPORT_CLASS(fly::TaskExecutor, "EXTaskExecutor")
    FLY_EXPORT_INIT()
    FLY_EXPORT_METHOD("execute", [](fly::TaskExecutor& self, uint64_t task_id, const fly::CMString& task_name, const fly::CMString& task_module, const fly::CMVector<fly::CMString>& args) {
        return self.execute(task_id, task_name, task_module, args);
    })
    FLY_EXPORT_METHOD("is_running", &fly::TaskExecutor::is_running)
    FLY_EXPORT_METHOD("cancel", &fly::TaskExecutor::cancel);

FLY_EXPORT_CLASS(fly::MasterAgent, "EXAgentMaster")
    FLY_EXPORT_INIT(fly::CMString, uint16_t)
    FLY_EXPORT_METHOD("start", &fly::MasterAgent::start)
    FLY_EXPORT_METHOD("stop", &fly::MasterAgent::stop)
    FLY_EXPORT_METHOD("is_running", &fly::MasterAgent::is_running);

FLY_EXPORT_CLASS(fly::WorkerAgent, "EXAgentWorker")
    FLY_EXPORT_INIT(uint64_t, fly::CMString, uint16_t)
    FLY_EXPORT_METHOD("start", &fly::WorkerAgent::start)
    FLY_EXPORT_METHOD("stop", &fly::WorkerAgent::stop)
    FLY_EXPORT_METHOD("is_running", &fly::WorkerAgent::is_running)
    FLY_EXPORT_METHOD("get_worker_id", &fly::WorkerAgent::get_worker_id);

}