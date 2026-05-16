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
    FLY_EXPORT_ATTR("error", &fly::TaskExecResult::error)
    FLY_EXPORT_ATTR("outputs", &fly::TaskExecResult::outputs);

FLY_EXPORT_CLASS(fly::TaskExecutor, "EXTaskExecutor")
    FLY_EXPORT_INIT()
    FLY_EXPORT_METHOD("execute", [](fly::TaskExecutor& self, uint64_t task_id, const fly::CMString& task_name, const fly::CMString& task_module, const fly::CMVector<fly::CMString>& args) {
        return self.execute(task_id, task_name, task_module, args);
    })
    FLY_EXPORT_METHOD("is_running", &fly::TaskExecutor::is_running)
    FLY_EXPORT_METHOD("cancel", &fly::TaskExecutor::cancel)
    FLY_EXPORT_METHOD("set_exec_func", [](fly::TaskExecutor& self, fly_export::object py_func) {
        auto cpp_func = [py_func](uint64_t task_id, const fly::CMString& task_name,
                                   const fly::CMString& task_module,
                                   const fly::CMVector<fly::CMString>& args) -> fly::TaskExecResult {
            fly_export::gil_scoped_acquire acquire;
            try {
                fly_export::object result = py_func(task_id, task_name, task_module, args);
                fly::TaskExecResult cpp_result;
                cpp_result.task_id = fly_export::cast<uint64_t>(result.attr("task_id"));
                cpp_result.status = fly_export::cast<fly::TaskExecStatus>(result.attr("status"));
                cpp_result.output = fly_export::cast<fly::CMString>(result.attr("output"));
                cpp_result.error = fly_export::cast<fly::CMString>(result.attr("error"));
                cpp_result.outputs = fly_export::cast<fly::CMVector<fly::CMString>>(result.attr("outputs"));
                return cpp_result;
            } catch (const fly_export::python_error& e) {
                fly::TaskExecResult cpp_result;
                cpp_result.task_id = task_id;
                cpp_result.status = fly::TaskExecStatus::FAILED;
                cpp_result.output = "";
                cpp_result.error = e.what();
                cpp_result.outputs = {};
                return cpp_result;
            }
        };
        self.set_exec_func(cpp_func);
    });

FLY_EXPORT_CLASS(fly::MasterAgent, "EXAgentMaster")
    FLY_EXPORT_INIT(fly::CMString, uint16_t)
    FLY_EXPORT_METHOD("start", &fly::MasterAgent::start)
    FLY_EXPORT_METHOD("stop", &fly::MasterAgent::stop)
    FLY_EXPORT_METHOD("is_running", &fly::MasterAgent::is_running)
    FLY_EXPORT_METHOD("get_connected_workers", &fly::MasterAgent::get_connected_workers)
    FLY_EXPORT_METHOD("get_connection_count", &fly::MasterAgent::get_connection_count)
    FLY_EXPORT_METHOD("submit_task", [](fly::MasterAgent& self, uint64_t task_id,
                                         const fly::CMString& name,
                                         const fly::CMString& module,
                                         const fly::CMVector<fly::CMString>& args) {
        self.submit_task(task_id, name, module, args, {}, {});
    })
    FLY_EXPORT_METHOD("submit_task_with_deps", [](fly::MasterAgent& self, uint64_t task_id,
                                                   const fly::CMString& name,
                                                   const fly::CMString& module,
                                                   const fly::CMVector<fly::CMString>& args,
                                                   const fly::CMVector<fly::CMString>& inputs,
                                                   const fly::CMVector<fly::CMString>& outputs) {
        self.submit_task(task_id, name, module, args, inputs, outputs);
    })
    FLY_EXPORT_METHOD("get_pending_tasks", &fly::MasterAgent::get_pending_tasks)
    FLY_EXPORT_METHOD("get_running_tasks", &fly::MasterAgent::get_running_tasks)
    FLY_EXPORT_METHOD("get_completed_tasks", &fly::MasterAgent::get_completed_tasks)
    FLY_EXPORT_METHOD("get_idle_workers", &fly::MasterAgent::get_idle_workers);

FLY_EXPORT_CLASS(fly::WorkerAgent, "EXAgentWorker")
    FLY_EXPORT_INIT(uint64_t, fly::CMString, uint16_t)
    FLY_EXPORT_METHOD("start", &fly::WorkerAgent::start)
    FLY_EXPORT_METHOD("stop", &fly::WorkerAgent::stop)
    FLY_EXPORT_METHOD("is_running", &fly::WorkerAgent::is_running)
    FLY_EXPORT_METHOD("get_worker_id", &fly::WorkerAgent::get_worker_id)
    FLY_EXPORT_METHOD("set_executor", &fly::WorkerAgent::set_executor)
    FLY_EXPORT_METHOD("is_registered", &fly::WorkerAgent::is_registered);

}