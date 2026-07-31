#include <export/cpp/export_macros.h>
#include <Python.h>
#include <agent/cpp/task_executor.h>
#include <agent/cpp/master_agent.h>
#include <agent/cpp/worker_agent.h>
#include <storage/cpp/data_service.h>
#include <memory>
#include <tuple>

FLY_EXPORT_MODULE(_fly_agent) {

FLY_EXPORT_ENUM(fly::TaskExecStatus, "EXTaskExecStatus")
    FLY_EXPORT_ENUM_VALUE("SUCCESS", fly::TaskExecStatus::SUCCESS)
    FLY_EXPORT_ENUM_VALUE("FAILED", fly::TaskExecStatus::FAILED)
    FLY_EXPORT_ENUM_VALUE("TIMEOUT", fly::TaskExecStatus::TIMEOUT);

FLY_EXPORT_CLASS(fly::TaskExecResult, "EXTaskExecResult")
    FLY_EXPORT_INIT()
    FLY_EXPORT_ATTR("task_id", &fly::TaskExecResult::task_id_)
    FLY_EXPORT_ATTR("status", &fly::TaskExecResult::status_)
    FLY_EXPORT_ATTR("output", &fly::TaskExecResult::output_)
    FLY_EXPORT_ATTR("error", &fly::TaskExecResult::error_)
    FLY_EXPORT_ATTR("outputs", &fly::TaskExecResult::outputs_);

FLY_EXPORT_CLASS(fly::TaskExecutor, "EXTaskExecutor")
    FLY_EXPORT_INIT()
    FLY_EXPORT_METHOD("execute", [](fly::TaskExecutor& self, uint64_t task_id, const fly::CMString& task_name, const fly::CMString& task_module, const fly::CMVector<fly::CMString>& args) {
        return self.execute(task_id, task_name, task_module, args);
    })
    FLY_EXPORT_METHOD("is_running", &fly::TaskExecutor::is_running)
    FLY_EXPORT_METHOD("set_exec_func", [](fly::TaskExecutor& self, fly_export::object py_func) {
        auto cpp_func = [py_func](uint64_t task_id, const fly::CMString& task_name,
                                    const fly::CMString& task_module,
                                    const fly::CMVector<fly::CMString>& args) -> fly::TaskExecResult {
            fly_export::gil_scoped_acquire acquire;
            try {
                fly_export::object result = py_func(task_id, task_name, task_module, args);
                fly::TaskExecResult cpp_result;
                cpp_result.task_id_ = fly_export::cast<uint64_t>(result[fly_export::str("task_id")]);
                long status_val = PyLong_AsLong(result[fly_export::str("status")].ptr());
                if (status_val == -1 && PyErr_Occurred()) {
                    PyErr_Clear();
                    status_val = 1;
                }
                cpp_result.status_ = static_cast<fly::TaskExecStatus>(status_val);
                cpp_result.output_ = fly_export::cast<fly::CMString>(result[fly_export::str("output")]);
                cpp_result.error_ = fly_export::cast<fly::CMString>(result[fly_export::str("error")]);
                cpp_result.outputs_ = fly_export::cast<fly::CMVector<fly::CMString>>(result[fly_export::str("outputs")]);
                cpp_result.frozen_dbs_ = fly_export::cast<fly::CMVector<fly::CMString>>(result[fly_export::str("frozen_dbs")]);
                return cpp_result;
            } catch (const fly_export::python_error& e) {
                fly::TaskExecResult cpp_result;
                cpp_result.task_id_ = task_id;
                cpp_result.status_ = fly::TaskExecStatus::FAILED;
                cpp_result.output_ = "";
                cpp_result.error_ = e.what();
                cpp_result.outputs_ = {};
                cpp_result.frozen_dbs_ = {};
                return cpp_result;
            }
        };
        self.set_exec_func(cpp_func);
    })
    FLY_EXPORT_METHOD("clear_exec_func", &fly::TaskExecutor::clear_exec_func);

FLY_EXPORT_CLASS(fly::MasterAgent, "EXAgentMaster")
    FLY_EXPORT_INIT(fly::CMString, uint16_t)
    FLY_EXPORT_METHOD("start", &fly::MasterAgent::start)
    FLY_EXPORT_METHOD("stop", &fly::MasterAgent::stop)
    FLY_EXPORT_METHOD("is_running", &fly::MasterAgent::is_running)
    FLY_EXPORT_METHOD("get_connected_workers", &fly::MasterAgent::get_connected_workers)
    FLY_EXPORT_METHOD("get_worker_hostnames", [](fly::MasterAgent& self) -> fly_export::list {
        auto pairs = self.get_worker_hostnames();
        fly_export::list result;
        for (const auto& [worker_id, hostname] : pairs) {
            result.append(fly_export::make_tuple(worker_id, hostname));
        }
        return result;
    })
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
        self.submit_task(task_id, name, module, args, inputs, outputs, {});
    })
    FLY_EXPORT_METHOD("submit_task_with_requirements", [](fly::MasterAgent& self, uint64_t task_id,
                                                             const fly::CMString& name,
                                                             const fly::CMString& module,
                                                             const fly::CMVector<fly::CMString>& args,
                                                             const fly::CMVector<fly::CMString>& inputs,
                                                             const fly::CMVector<fly::CMString>& outputs,
                                                             const fly::CMVector<fly::CMString>& required_capabilities,
                                                             float attribute_timeout,
                                                             const fly::CMString& write_context_hash,
                                                             const fly::CMVector<fly::CMString>& vars,
                                                             int priority) {
        self.submit_task(task_id, name, module, args, inputs, outputs, required_capabilities, attribute_timeout, write_context_hash, vars, priority);
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
    FLY_EXPORT_METHOD("restart_failed_tasks", [](fly::MasterAgent& self, const fly::CMString& file_path) {
        self.restart_failed_tasks(file_path);
    })
    FLY_EXPORT_METHOD("broadcast_object_removed", [](fly::MasterAgent& self,
                                                        const fly::CMString& db_id,
                                                        const fly::CMString& object_name) {
        self.broadcast_object_removed(db_id, object_name);
    })
    FLY_EXPORT_METHOD("setup_write_context", [](fly::MasterAgent& self) {
        self.setup_write_context();
    })
    FLY_EXPORT_METHOD("restore_master_idx", [](fly::MasterAgent& self,
                                                  const fly::CMString& db_id,
                                                  const fly::CMString& base_path,
                                                  const fly::CMString& writer_id) -> fly::CMVector<IndexEntry> {
        return self.restore_master_idx(db_id, base_path, writer_id);
    })
    // 轻量读 idx（不灌 master local_idx，不 mark_data_ready）—— merge_db Phase 3 专用。
    FLY_EXPORT_METHOD("read_idx_entries", [](fly::MasterAgent& self,
                                               const fly::CMString& base_path,
                                               const fly::CMString& writer_id) -> fly::CMVector<IndexEntry> {
        return self.read_idx_entries(base_path, writer_id);
    })
    FLY_EXPORT_METHOD("send_idx_load_commands", [](fly::MasterAgent& self,
                                                     const fly::CMString& db_id,
                                                     const fly::CMString& base_path,
                                                     const fly::CMVector<fly::CMString>& writer_ids) {
        self.send_idx_load_commands(db_id, base_path, writer_ids);
    })
    FLY_EXPORT_METHOD("rebuild_remote_idx", [](fly::MasterAgent& self,
                                                   const fly::CMString& db_id,
                                                   const fly::CMString& base_path,
                                                   const fly::CMVector<::WorkerInfo>& workers) {
        self.rebuild_remote_idx(db_id, base_path, workers);
    })
    FLY_EXPORT_METHOD("set_master_hostname", &fly::MasterAgent::set_master_hostname)
    FLY_EXPORT_METHOD("send_idx_load_to_worker", [](fly::MasterAgent& self,
                                                      const fly::CMString& db_id,
                                                      const fly::CMString& base_path,
                                                      const fly::CMVector<fly::CMString>& writer_ids,
                                                      uint64_t worker_id) {
        self.send_idx_load_to_worker(db_id, base_path, writer_ids, worker_id);
    })
    FLY_EXPORT_METHOD("rebuild_remote_idx_for_worker", [](fly::MasterAgent& self,
                                                            const fly::CMString& db_id,
                                                            const fly::CMString& base_path,
                                                            const fly::CMVector<fly::CMString>& writer_ids,
                                                            uint64_t worker_id) {
        self.rebuild_remote_idx_for_worker(db_id, base_path, writer_ids, worker_id);
    })
    // ── DB Merge support (fly.merge_db 主动 API) ──
    // 派发单个 __merge_object internal task，返回 task_id。
    FLY_EXPORT_METHOD("send_merge_task", [](fly::MasterAgent& self,
                                              uint64_t target_worker_id,
                                              const fly::CMString& short_name,
                                              const fly::CMString& db_id,
                                              const fly::CMString& base_path,
                                              const fly::CMString& target_data_path,
                                              const fly::CMString& source_host) -> uint64_t {
        return self.send_merge_task(target_worker_id, short_name, db_id, base_path, target_data_path, source_host);
    })
    // 命令源 worker 删除本地 .dat（data_path 显式传入）。
    FLY_EXPORT_METHOD("send_delete_data", [](fly::MasterAgent& self,
                                               uint64_t source_worker_id,
                                               const fly::CMString& db_id,
                                               const fly::CMString& base_path,
                                               const fly::CMString& data_path,
                                               const fly::CMVector<fly::CMString>& writer_ids) {
        self.send_delete_data(source_worker_id, db_id, base_path, data_path, writer_ids);
    })
    // 等待一批 DeleteData 的 ack 全部返回。返回 (all_ok, failed_worker_ids)。
    FLY_EXPORT_METHOD("wait_delete_data_acks", [](fly::MasterAgent& self,
                                                    const fly::CMVector<uint64_t>& source_worker_ids,
                                                    const fly::CMString& db_id,
                                                    int64_t timeout_seconds) -> fly_export::object {
        fly::CMVector<uint64_t> failed;
        bool ok = self.wait_delete_data_acks(source_worker_ids, db_id, timeout_seconds, &failed);
        return fly_export::make_tuple(ok, std::move(failed));
    })
    // 等待一批 merge task 完成。返回 (all_ok, completed_objects, failed_objects)。
    // 注意：lambda body 内不能有"顶层逗号"（预处理器不识别花括号分组，会把多变量声明的
    // 逗号当成宏参数分隔），所以 completed/failed 分别声明。
    // 用 fly_export::make_tuple 返回 Python tuple（std::tuple 的 nanobind 转换需额外注册，
    // 直接构造 Python tuple 更简单）。
    FLY_EXPORT_METHOD("wait_merge_tasks_complete", [](fly::MasterAgent& self,
                                                        const fly::CMVector<uint64_t>& task_ids,
                                                        int64_t timeout_seconds) -> fly_export::object {
        fly::CMVector<fly::CMString> completed;
        fly::CMVector<fly::CMString> failed;
        bool ok = self.wait_merge_tasks_complete(task_ids, timeout_seconds, &completed, &failed);
        return fly_export::make_tuple(ok, std::move(completed), std::move(failed));
    })
    // merge 全部成功后的状态清理：广播 MergeCleanup + 清 master 自身旧索引 + 重建 remote_idx。
    FLY_EXPORT_METHOD("cleanup_after_merge", [](fly::MasterAgent& self,
                                                  const fly::CMString& db_id,
                                                  const fly::CMVector<fly::CMString>& merged_object_full_names,
                                                  const fly::CMVector<uint64_t>& source_worker_ids,
                                                  const fly::CMVector<uint64_t>& merge_target_worker_ids,
                                                  const fly::CMString& merge_base_path,
                                                  const fly::CMString& merge_data_path) {
        self.cleanup_after_merge(db_id, merged_object_full_names, source_worker_ids,
                                  merge_target_worker_ids, merge_base_path, merge_data_path);
    });

// VarPayload: a {var_name, value, type_name} triple inlined into TaskAssignMessage.
// Exported so the Python executor can read master-inlined vars.
FLY_EXPORT_CLASS(fly::VarPayload, "EXVarPayload")
    FLY_EXPORT_READONLY_ATTR("var_name", &fly::VarPayload::var_name)
    // value holds arbitrary serialized bytes (pickle / FLY_ENCODE_TO_BUFFER) that may
    // not be valid UTF-8; expose as raw bytes, not str.
    FLY_EXPORT_READONLY_PROPERTY("value",
        [](const fly::VarPayload& vp) -> fly_export::bytes {
            return fly_export::bytes(vp.value.data(), vp.value.size());
        })
    FLY_EXPORT_READONLY_ATTR("type_name", &fly::VarPayload::type_name);

FLY_EXPORT_CLASS(fly::WorkerAgent, "EXAgentWorker")
    FLY_EXPORT_INIT(uint64_t, fly::CMString, uint16_t)
    FLY_EXPORT_INIT(uint64_t, fly::CMString, uint16_t, fly::CMVector<fly::CMString>)
    FLY_EXPORT_METHOD("start", &fly::WorkerAgent::start)
    FLY_EXPORT_METHOD("stop", &fly::WorkerAgent::stop)
    FLY_EXPORT_METHOD("is_running", &fly::WorkerAgent::is_running)
    FLY_EXPORT_METHOD("get_worker_id", &fly::WorkerAgent::get_worker_id)
    FLY_EXPORT_METHOD("set_executor", [](fly::WorkerAgent& self, CMSharedPtr<fly::TaskExecutor> executor) {
        self.set_executor(std::move(executor));
    })
    FLY_EXPORT_METHOD("is_registered", &fly::WorkerAgent::is_registered)
    FLY_EXPORT_METHOD("poll_task", &fly::WorkerAgent::poll_task)
    FLY_EXPORT_METHOD("poll_task_blocking", &fly::WorkerAgent::poll_task_blocking)
    FLY_EXPORT_METHOD("has_pending_task", &fly::WorkerAgent::has_pending_task)
    FLY_EXPORT_METHOD("submit_task", [](fly::WorkerAgent& self,
                                         const fly::CMString& name,
                                         const fly::CMString& module,
                                         const fly::CMVector<fly::CMString>& args,
                                         const fly::CMVector<fly::CMString>& inputs,
                                         const fly::CMVector<fly::CMString>& required_capabilities,
                                         float attribute_timeout,
                                         const fly::CMString& write_context_hash,
                                         const fly::CMVector<fly::CMString>& vars,
                                         int priority) {
        self.submit_task(name, module, args, inputs, required_capabilities, attribute_timeout, write_context_hash, vars, priority);
    })
    FLY_EXPORT_METHOD("take_pending_task_vars", [](fly::WorkerAgent& self) -> fly::CMVector<fly::VarPayload> {
        return self.take_pending_task_vars();
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
    FLY_EXPORT_DEF("request_remote_data", [](fly::WorkerAgent& self, const fly::CMString& object_name) -> fly_export::tuple {
        auto [refreshed, can_still_produce] = self.request_remote_data(object_name);
        return fly_export::make_tuple(refreshed, can_still_produce);
    })
    FLY_EXPORT_METHOD("request_db_path", [](fly::WorkerAgent& self,
                                               const fly::CMString& db_id) -> bool {
        return self.request_db_path(db_id);
    })
    FLY_EXPORT_METHOD("set_worker_property", [](fly::WorkerAgent& self,
                                                   const fly::CMVector<fly::CMString>& props) {
        self.set_worker_property(props);
    })
    FLY_EXPORT_METHOD("remove_worker_property", [](fly::WorkerAgent& self,
                                                      const fly::CMVector<fly::CMString>& props) {
        self.remove_worker_property(props);
    })
    FLY_EXPORT_METHOD("get_worker_properties", [](fly::WorkerAgent& self) {
        return self.get_worker_properties();
    });

}