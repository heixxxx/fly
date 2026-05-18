#include <export/cpp/export_macros.h>
#include <Python.h>
#include <agent/cpp/task_executor.h>
#include <agent/cpp/master_agent.h>
#include <agent/cpp/worker_agent.h>
#include <storage/cpp/data_service.h>
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
                cpp_result.task_id = fly_export::cast<uint64_t>(result[fly_export::str("task_id")]);
                long status_val = PyLong_AsLong(result[fly_export::str("status")].ptr());
                if (status_val == -1 && PyErr_Occurred()) {
                    PyErr_Clear();
                    status_val = 1;
                }
                cpp_result.status = static_cast<fly::TaskExecStatus>(status_val);
                cpp_result.output = fly_export::cast<fly::CMString>(result[fly_export::str("output")]);
                cpp_result.error = fly_export::cast<fly::CMString>(result[fly_export::str("error")]);
                cpp_result.outputs = fly_export::cast<fly::CMVector<fly::CMString>>(result[fly_export::str("outputs")]);
                cpp_result.frozen_dbs = fly_export::cast<fly::CMVector<fly::CMString>>(result[fly_export::str("frozen_dbs")]);
                return cpp_result;
            } catch (const fly_export::python_error& e) {
                fly::TaskExecResult cpp_result;
                cpp_result.task_id = task_id;
                cpp_result.status = fly::TaskExecStatus::FAILED;
                cpp_result.output = "";
                cpp_result.error = e.what();
                cpp_result.outputs = {};
                cpp_result.frozen_dbs = {};
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
    FLY_EXPORT_METHOD("register_database", [](fly::MasterAgent& self,
                                                const fly::CMString& db_id,
                                                const fly::CMString& base_path,
                                                const fly::CMString& data_path) {
        self.register_database(db_id, base_path, data_path);
    })
    FLY_EXPORT_METHOD("is_db_frozen", &fly::MasterAgent::is_db_frozen)
    FLY_EXPORT_METHOD("get_or_create_database", [](fly::MasterAgent& self,
                                                      const fly::CMString& base_path,
                                                      const fly::CMString& data_path,
                                                      uint64_t writer_id) -> CMSharedPtr<Database> {
        return self.get_or_create_database(base_path, data_path, writer_id);
    })
    FLY_EXPORT_METHOD("get_pending_tasks", &fly::MasterAgent::get_pending_tasks)
    FLY_EXPORT_METHOD("get_running_tasks", &fly::MasterAgent::get_running_tasks)
    FLY_EXPORT_METHOD("get_completed_tasks", &fly::MasterAgent::get_completed_tasks)
    FLY_EXPORT_METHOD("get_failed_tasks", &fly::MasterAgent::get_failed_tasks)
    FLY_EXPORT_METHOD("get_task_error", &fly::MasterAgent::get_task_error)
    FLY_EXPORT_METHOD("get_idle_workers", &fly::MasterAgent::get_idle_workers)
    FLY_EXPORT_METHOD("get_port", &fly::MasterAgent::get_port)
    FLY_EXPORT_METHOD("get_data_server_port", &fly::MasterAgent::get_data_server_port)
    FLY_EXPORT_METHOD("request_remote_data", [](fly::MasterAgent& self, const fly::CMString& object_name) -> fly_export::tuple {
        auto result = self.request_remote_data(object_name);
        return fly_export::make_tuple(
            fly_export::bytes(
                reinterpret_cast<const char*>(result.data_buffer.data()),
                result.data_buffer.size()),
            result.py_name
        );
    })
    FLY_EXPORT_METHOD("request_data_from_worker", [](fly::MasterAgent& self,
                                                        const fly::CMString& host,
                                                        int32_t port,
                                                        const fly::CMString& object_name) -> fly_export::tuple {
        auto result = self.request_data_from_worker(host, port, object_name);
        return fly_export::make_tuple(
            fly_export::bytes(
                reinterpret_cast<const char*>(result.data_buffer.data()),
                result.data_buffer.size()),
            result.py_name
        );
    })
    FLY_EXPORT_METHOD("set_data_service", [](fly::MasterAgent& self, fly::DataService& ds) {
        self.set_data_service(&ds);
    });

FLY_EXPORT_CLASS(fly::WorkerAgent, "EXAgentWorker")
    FLY_EXPORT_INIT(uint64_t, fly::CMString, uint16_t)
    FLY_EXPORT_METHOD("start", &fly::WorkerAgent::start)
    FLY_EXPORT_METHOD("stop", &fly::WorkerAgent::stop)
    FLY_EXPORT_METHOD("is_running", &fly::WorkerAgent::is_running)
    FLY_EXPORT_METHOD("get_worker_id", &fly::WorkerAgent::get_worker_id)
    FLY_EXPORT_METHOD("set_executor", [](fly::WorkerAgent& self, CMSharedPtr<fly::TaskExecutor> executor) {
        self.set_executor(std::move(executor));
    })
    FLY_EXPORT_METHOD("is_registered", &fly::WorkerAgent::is_registered)
    FLY_EXPORT_METHOD("poll_task", &fly::WorkerAgent::poll_task)
    FLY_EXPORT_METHOD("has_pending_task", &fly::WorkerAgent::has_pending_task)
    FLY_EXPORT_METHOD("submit_task", [](fly::WorkerAgent& self,
                                         const fly::CMString& name,
                                         const fly::CMString& module,
                                         const fly::CMVector<fly::CMString>& args,
                                         const fly::CMVector<fly::CMString>& inputs) {
        self.submit_task(name, module, args, inputs);
    })
    FLY_EXPORT_METHOD("register_database", [](fly::WorkerAgent& self,
                                                const fly::CMString& db_id,
                                                CMSharedPtr<Database> db) {
        self.register_database(db_id, std::move(db));
    })
    FLY_EXPORT_METHOD("get_database", [](fly::WorkerAgent& self,
                                           const fly::CMString& db_id) -> CMSharedPtr<Database> {
        return self.get_database(db_id);
    })
    FLY_EXPORT_METHOD("request_remote_data", [](fly::WorkerAgent& self, const fly::CMString& object_name) -> fly_export::tuple {
        auto result = self.request_remote_data(object_name);
        return fly_export::make_tuple(
            fly_export::bytes(
                reinterpret_cast<const char*>(result.data_buffer.data()),
                result.data_buffer.size()),
            result.py_name
        );
    })
    FLY_EXPORT_METHOD("request_data_from_worker", [](fly::WorkerAgent& self,
                                                        const fly::CMString& host,
                                                        int32_t port,
                                                        const fly::CMString& object_name) -> fly_export::tuple {
        auto result = self.request_data_from_worker(host, port, object_name);
        return fly_export::make_tuple(
            fly_export::bytes(
                reinterpret_cast<const char*>(result.data_buffer.data()),
                result.data_buffer.size()),
            result.py_name
        );
    })
    FLY_EXPORT_METHOD("request_db_path", [](fly::WorkerAgent& self,
                                               const fly::CMString& db_id) -> bool {
        return self.request_db_path(db_id);
    })
    FLY_EXPORT_METHOD("set_data_service", [](fly::WorkerAgent& self, fly::DataService& ds) {
        self.set_data_service(&ds);
    });

}