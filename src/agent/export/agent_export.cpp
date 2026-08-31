#include <export/cpp/export_macros.h>
#include <Python.h>
#include <nanobind/stl/function.h>
#include <agent/cpp/task_executor.h>
#include <agent/cpp/master_agent.h>
#include <agent/cpp/graceful_shutdown.h>
#include <agent/cpp/worker_agent.h>
#include <storage/cpp/data_service.h>
#include <memory>
#include <tuple>
#include <cmath>

namespace {
// peer_rpc_call 的 GIL 释放包装（timeout_ms=0 无限等待时长阻塞，若持 GIL
// 会冻结同进程全部 Python 线程）。独立函数：避免 lambda 体在
// FLY_EXPORT_METHOD 两参宏内出现顶层裸逗号（如 std::pair<T,U> 声明）。
std::pair<uint8_t, fly::CMString> peer_call_gil_released(
        fly::WorkerAgent& self, uint64_t conn_id,
        const fly::CMString& payload_str, int timeout_ms) {
    std::pair<uint8_t, fly::CMString> result;
    {
        fly_export::gil_scoped_release release;
        result = self.peer_rpc_call(conn_id, payload_str, timeout_ms);
    }
    return result;
}

// ── 流式大 payload 导出辅助（参数含逗号，不能内联进两参宏）──

fly::PeerStreamWriter* peer_stream_writer_export(fly::WorkerAgent& self,
                                                 uint64_t conn_id,
                                                 std::string compression,
                                                 int level) {
    return self.peer_stream_writer(conn_id, fly::CMString(compression), level);
}

fly::PeerStreamWriter* peer_stream_respond_writer_export(fly::WorkerAgent& self,
                                                         uint64_t conn_id,
                                                         uint64_t rpc_id,
                                                         std::string compression,
                                                         int level) {
    return self.peer_stream_respond_writer(conn_id, rpc_id,
                                           fly::CMString(compression), level);
}

fly_export::object peer_stream_call_wait_export(fly::WorkerAgent& self,
                                                uint64_t rpc_id,
                                                int timeout_ms) {
    std::pair<uint8_t, fly::CMString> result;
    {
        fly_export::gil_scoped_release release;  // 无限等待语义：必须释放 GIL
        result = self.peer_stream_call_wait(rpc_id, timeout_ms);
    }
    // 裸 pair 直接返回 nanobind 转换失败——同 peer_rpc_call 用 make_tuple。
    return fly_export::make_tuple(
        result.first,
        fly_export::bytes(result.second.data(), result.second.size()));
}

// ── 执行上提（消灭 C++→Python 反调）：take/finish 原语的导出辅助 ──

// PendingTask → Python dict（task 描述：主循环据此路由执行）。
fly_export::dict task_desc_to_dict(const fly::PendingTask& t) {
    fly_export::dict d;
    d[fly_export::str("task_id")] = t.task_id_;
    d[fly_export::str("task_name")] = t.task_name_;
    d[fly_export::str("task_module")] = t.task_module_;
    d[fly_export::str("args")] = t.args_;
    d[fly_export::str("write_context_hash")] = t.write_context_hash_;
    return d;
}

// take_task：GIL 释放等待 + 出队（internal task 由 C++ 就地消化）。空等期间
// 不占 GIL——同进程 Python 线程（solver serve 等）可自由运行。返回 None 或
// task 描述 dict。
fly_export::object take_task_gil_released(fly::WorkerAgent& self, int timeout_ms) {
    fly::CMUniquePtr<fly::PendingTask> task;
    {
        fly_export::gil_scoped_release release;
        task = self.take_task(timeout_ms);
    }
    if (!task) return fly_export::none();
    return task_desc_to_dict(*task);
}

// finish_task：dict → TaskExecResult 转换（原 set_exec_func 回调内的转换段
// 搬迁，语义逐字段保持）+ GIL 释放收尾（纯 C++：资源跟踪/写段提交/上报）。
void finish_task_gil_released(fly::WorkerAgent& self,
                              fly_export::dict task_d,
                              fly_export::dict result_d) {
    // 重构 PendingTask：finish 只消费 task_id 与上下文钩子已注入的态。
    fly::PendingTask task;
    task.task_id_ = fly_export::cast<uint64_t>(task_d[fly_export::str("task_id")]);

    fly::TaskExecResult r;
    r.task_id_ = task.task_id_;
    long status_val = PyLong_AsLong(result_d[fly_export::str("status")].ptr());
    if (status_val == -1 && PyErr_Occurred()) {
        PyErr_Clear();
        status_val = 1;
    }
    r.status_ = static_cast<fly::TaskExecStatus>(status_val);
    r.output_ = fly_export::cast<fly::CMString>(result_d[fly_export::str("output")]);
    r.error_ = fly_export::cast<fly::CMString>(result_d[fly_export::str("error")]);
    r.outputs_ = fly_export::cast<fly::CMVector<fly::CMString>>(
        result_d[fly_export::str("outputs")]);
    r.frozen_dbs_ = fly_export::cast<fly::CMVector<fly::CMString>>(
        result_d[fly_export::str("frozen_dbs")]);
    // cluster monitor io_stats（executor 恒带该键；缺失/形态异常降级全 0，
    // 绝不影响主流程）。
    // 计时 float ms → 整数 ms 用 ceil：截断会把亚毫秒 IO（如 48B 压缩写
    // ~0.x ms）记成虚假的 0 耗时——非零 IO 至少记 1ms（100 轮压测实测
    // write_ms=0 撞过；cpu_time 已同理由 jiffies 改微秒差分）。
    try {
        fly_export::object io = result_d[fly_export::str("io_stats")];
        r.read_time_ms_ = static_cast<uint64_t>(std::ceil(
            fly_export::cast<double>(io[fly_export::str("read_ms")])));
        r.write_time_ms_ = static_cast<uint64_t>(std::ceil(
            fly_export::cast<double>(io[fly_export::str("write_ms")])));
        r.read_bytes_ = fly_export::cast<uint64_t>(
            io[fly_export::str("read_bytes")]);
        r.io_mem_peak_rss_ = fly_export::cast<uint64_t>(
            io[fly_export::str("mem_peak_rss")]);
        fly_export::list items =
            fly_export::cast<fly_export::list>(io[fly_export::str("items")]);
        for (fly_export::handle h : items) {
            // item 是 dict（不是对象），必须按键取值（attr 是属性访问）。
            fly_export::dict d = fly_export::cast<fly_export::dict>(h);
            fly::ObjectIoRecord rec;
            rec.object_name_ = fly_export::cast<fly::CMString>(
                d[fly_export::str("name")]);
            rec.is_write_ = fly_export::cast<bool>(d[fly_export::str("w")]);
            rec.bytes_ = fly_export::cast<uint64_t>(d[fly_export::str("bytes")]);
            rec.duration_ms_ = static_cast<uint64_t>(std::ceil(
                fly_export::cast<double>(d[fly_export::str("ms")])));
            rec.epoch_ms_ = fly_export::cast<uint64_t>(
                d[fly_export::str("epoch_ms")]);
            rec.task_id_ = task.task_id_;
            r.io_items_.push_back(rec);
        }
    } catch (const fly_export::python_error&) {
        PyErr_Clear();
    }

    {
        fly_export::gil_scoped_release release;
        self.finish_task(task, r);
    }
}
}  // namespace

FLY_EXPORT_MODULE(_fly_agent) {

// SIGTERM 信号灯（main.py 的 Python handler 首行调用）：置位后 worker 的
// is_running() 返回 false（poll 循环退出）、master heartbeat 线程触发 stop() drain。
FLY_EXPORT_FUNCTION("ex_agent_set_graceful_shutdown", []() {
    fly::set_graceful_shutdown();
});

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

// 流式大 payload 写端（file-like：pickle.dump(obj, w) 需要 write(bytes)）。
FLY_EXPORT_CLASS(fly::PeerStreamWriter, "EXPeerStreamWriter")
    FLY_EXPORT_DEF("write", [](fly::PeerStreamWriter& w, fly_export::bytes data) {
        // pickle.dump 调用线程持有 GIL；write 内部 send 阻塞（TCP 反压）期间
        // 释放 GIL——同进程其它 Python 线程（task 主循环等）不被冻结。
        fly_export::gil_scoped_release release;
        w.write(data.c_str(), data.size());
    })
    FLY_EXPORT_DEF("finish", [](fly::PeerStreamWriter& w) -> bool {
        fly_export::gil_scoped_release release;
        return w.finish();
    })
    FLY_EXPORT_DEF("rpc_id", [](fly::PeerStreamWriter& w) { return w.rpc_id(); })
    FLY_EXPORT_DEF("ok", [](fly::PeerStreamWriter& w) { return w.ok(); });

FLY_EXPORT_CLASS(fly::TaskExecutor, "EXTaskExecutor")
    FLY_EXPORT_INIT()
    FLY_EXPORT_METHOD("execute", [](fly::TaskExecutor& self, uint64_t task_id, const fly::CMString& task_name, const fly::CMString& task_module, const fly::CMVector<fly::CMString>& args) {
        return self.execute(task_id, task_name, task_module, args);
    })
    FLY_EXPORT_METHOD("is_running", &fly::TaskExecutor::is_running)
    // 执行上提（消灭 C++→Python 反调）：set_exec_func 的 Python 绑定已删——
    // 生产路径执行体在 Python 主循环（WorkerAgent.take_task/finish_task），
    // C++ 测试路径直接注入 C++ stub（类方法保留）。
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
    FLY_EXPORT_METHOD("get_storage_only_workers", &fly::MasterAgent::get_storage_only_workers)
    FLY_EXPORT_METHOD("get_connection_count", &fly::MasterAgent::get_connection_count)
    FLY_EXPORT_METHOD("expect_worker", &fly::MasterAgent::expect_worker)
    FLY_EXPORT_METHOD("cleanup_failed_merge", &fly::MasterAgent::cleanup_failed_merge)
    FLY_EXPORT_METHOD("get_expected_worker_count", &fly::MasterAgent::get_expected_worker_count)
    FLY_EXPORT_METHOD("all_workers_registered", &fly::MasterAgent::all_workers_registered)
    FLY_EXPORT_METHOD("submit_task", [](fly::MasterAgent& self, uint64_t task_id,
                                         const fly::CMString& name,
                                         const fly::CMString& module,
                                         const fly::CMVector<fly::CMString>& args) {
        fly::TaskSubmissionSpec spec;
        spec.name_ = name;
        spec.module_ = module;
        spec.args_ = args;
        self.submit_task(task_id, spec);
    })
    FLY_EXPORT_METHOD("submit_task_with_deps", [](fly::MasterAgent& self, uint64_t task_id,
                                                   const fly::CMString& name,
                                                   const fly::CMString& module,
                                                   const fly::CMVector<fly::CMString>& args,
                                                   const fly::CMVector<fly::CMString>& inputs,
                                                   const fly::CMVector<fly::CMString>& outputs) {
        fly::TaskSubmissionSpec spec;
        spec.name_ = name;
        spec.module_ = module;
        spec.args_ = args;
        spec.inputs_ = inputs;
        spec.outputs_ = outputs;
        self.submit_task(task_id, spec);
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
                                                             int priority,
                                                             const fly::CMString& owner_db_path) {
        fly::TaskSubmissionSpec spec;
        spec.name_ = name;
        spec.module_ = module;
        spec.args_ = args;
        spec.inputs_ = inputs;
        spec.outputs_ = outputs;
        spec.required_capabilities_ = required_capabilities;
        spec.attribute_timeout_ = attribute_timeout;
        spec.write_context_hash_ = write_context_hash;
        spec.vars_ = vars;
        spec.priority_ = priority;
        spec.owner_db_path_ = owner_db_path;
        self.submit_task(task_id, spec);
    })
    FLY_EXPORT_METHOD("register_database", [](fly::MasterAgent& self,
                                                const fly::CMString& db_path,
                                                const fly::CMString& data_path) {
        self.register_database(db_path, data_path);
    })
    FLY_EXPORT_METHOD("is_db_frozen", &fly::MasterAgent::is_db_frozen)
    FLY_EXPORT_METHOD("get_or_create_database", [](fly::MasterAgent& self,
                                                      const fly::CMString& db_path,
                                                      const fly::CMString& data_path,
                                                      uint64_t writer_id) -> CMSharedPtr<Database> {
        return self.get_or_create_database(db_path, data_path, writer_id);
    })
    FLY_EXPORT_METHOD("get_database", [](fly::MasterAgent& self,
                                         const fly::CMString& db_path) -> CMSharedPtr<Database> {
        return self.get_database(db_path);
    })
    FLY_EXPORT_METHOD("get_pending_tasks", &fly::MasterAgent::get_pending_tasks)
    FLY_EXPORT_METHOD("get_running_tasks", &fly::MasterAgent::get_running_tasks)
    FLY_EXPORT_METHOD("get_completed_tasks", &fly::MasterAgent::get_completed_tasks)
    FLY_EXPORT_METHOD("get_failed_tasks", &fly::MasterAgent::get_failed_tasks)
    FLY_EXPORT_METHOD("get_task_error", &fly::MasterAgent::get_task_error)
    FLY_EXPORT_METHOD("get_idle_workers", &fly::MasterAgent::get_idle_workers)
    FLY_EXPORT_METHOD("snapshot_worker_pool", [](fly::MasterAgent& self) -> fly_export::tuple {
        // 原子快照（ensure_workers 预检口径）：([ (worker_id, cap), ... ], pending)
        // 注意：lambda 体内禁用结构化绑定（[] 内逗号会被导出宏按参数切开）。
        const auto pool = self.snapshot_worker_pool();
        fly_export::list items;
        for (const auto& entry : pool.first) {
            items.append(fly_export::make_tuple(entry.first, entry.second));
        }
        return fly_export::make_tuple(items, pool.second);
    })
    FLY_EXPORT_METHOD("get_busy_workers", &fly::MasterAgent::get_busy_workers)
    FLY_EXPORT_METHOD("get_worker_capabilities", &fly::MasterAgent::get_worker_capabilities)
    FLY_EXPORT_METHOD("count_workers_with_all_capabilities", &fly::MasterAgent::count_workers_with_all_capabilities)
    FLY_EXPORT_METHOD("assign_worker_attributes", &fly::MasterAgent::assign_worker_attributes)
    FLY_EXPORT_METHOD("get_port", &fly::MasterAgent::get_port)
    FLY_EXPORT_METHOD("get_data_server_port", &fly::MasterAgent::get_data_server_port)
    FLY_EXPORT_METHOD("restart_failed_tasks", [](fly::MasterAgent& self, const fly::CMString& file_path) -> size_t {
        return self.restart_failed_tasks(file_path);
    })
    FLY_EXPORT_METHOD("broadcast_object_removed", [](fly::MasterAgent& self,
                                                        const fly::CMString& db_path,
                                                        const fly::CMString& object_name) {
        self.broadcast_object_removed(db_path, object_name);
    })
    FLY_EXPORT_METHOD("setup_write_context", [](fly::MasterAgent& self) {
        self.setup_write_context();
    })
    FLY_EXPORT_METHOD("set_record_worker_info_func",
        [](fly::MasterAgent& self,
           const std::function<void(const fly::CMString&, uint64_t, const fly::CMString&,
                                    const fly::CMString&, const fly::CMString&,
                                    const fly::CMString&)>& func) {
            self.set_record_worker_info_func(func);
        })
    FLY_EXPORT_METHOD("flush_worker_infos", [](fly::MasterAgent& self) {
        self.flush_worker_infos();
    })
    FLY_EXPORT_METHOD("register_db_uid", [](fly::MasterAgent& self,
                                             const fly::CMString& uid,
                                             const fly::CMString& db_path) {
        self.register_db_uid(uid, db_path);
    })
    FLY_EXPORT_METHOD("restore_master_idx", [](fly::MasterAgent& self,
                                                  const fly::CMString& db_path,
                                                                                                    const fly::CMString& writer_id) -> fly::CMVector<IndexEntry> {
        return self.restore_master_idx(db_path, writer_id);
    })
    // 轻量读 idx（不灌 master local_idx，不 mark_data_ready）—— merge_db Phase 3 专用。
    FLY_EXPORT_METHOD("read_idx_entries", [](fly::MasterAgent& self,
                                               const fly::CMString& db_path,
                                               const fly::CMString& writer_id) -> fly::CMVector<IndexEntry> {
        return self.read_idx_entries(db_path, writer_id);
    })
    FLY_EXPORT_METHOD("send_idx_load_commands", [](fly::MasterAgent& self,
                                                     const fly::CMString& db_path,
                                                                                                          const fly::CMVector<fly::CMString>& writer_ids) {
        self.send_idx_load_commands(db_path, writer_ids);
    })
    FLY_EXPORT_METHOD("rebuild_remote_idx", [](fly::MasterAgent& self,
                                                   const fly::CMString& db_path,
                                                                                                      const fly::CMVector<::WorkerInfo>& workers) {
        self.rebuild_remote_idx(db_path, workers);
    })
    FLY_EXPORT_METHOD("set_master_hostname", &fly::MasterAgent::set_master_hostname)
    FLY_EXPORT_METHOD("send_idx_load_to_worker", [](fly::MasterAgent& self,
                                                      const fly::CMString& db_path,
                                                                                                            const fly::CMVector<fly::CMString>& writer_ids,
                                                      uint64_t worker_id) {
        self.send_idx_load_to_worker(db_path, writer_ids, worker_id);
    })
    // load_db 可见性屏障：db_path 的待完成 IdxLoadAck 数（0=完成；-1=失败）。
    FLY_EXPORT_METHOD("idx_load_pending", &fly::MasterAgent::idx_load_pending)
    FLY_EXPORT_METHOD("rebuild_remote_idx_for_worker", [](fly::MasterAgent& self,
                                                            const fly::CMString& db_path,
                                                                                                                        const fly::CMVector<fly::CMString>& writer_ids,
                                                            uint64_t worker_id) {
        self.rebuild_remote_idx_for_worker(db_path, writer_ids, worker_id);
    })
    // ── DB Merge support (fly.merge_db 主动 API) ──
    // 派发单个 __merge_object internal task，返回 task_id。
    FLY_EXPORT_METHOD("send_merge_task", [](fly::MasterAgent& self,
                                              uint64_t target_worker_id,
                                              const fly::CMString& short_name,
                                              const fly::CMString& source_db_path,
                                              const fly::CMString& target_db_path,
                                              const fly::CMString& target_data_path,
                                              const fly::CMString& source_host) -> uint64_t {
        return self.send_merge_task(target_worker_id, short_name, source_db_path, target_db_path, target_data_path, source_host);
    })
    // 命令源 worker 删除本地 .dat（data_path 显式传入）。
    FLY_EXPORT_METHOD("send_delete_data", [](fly::MasterAgent& self,
                                               uint64_t source_worker_id,
                                               const fly::CMString& db_path,
                                                                                              const fly::CMString& data_path,
                                               const fly::CMVector<fly::CMString>& writer_ids) {
        self.send_delete_data(source_worker_id, db_path, data_path, writer_ids);
    })
    // 等待一批 DeleteData 的 ack 全部返回。返回 (all_ok, failed_worker_ids)。
    FLY_EXPORT_METHOD("wait_delete_data_acks", [](fly::MasterAgent& self,
                                                    const fly::CMVector<uint64_t>& source_worker_ids,
                                                    const fly::CMString& db_path,
                                                    int64_t timeout_seconds) -> fly_export::object {
        fly::CMVector<uint64_t> failed;
        bool ok = self.wait_delete_data_acks(source_worker_ids, db_path, timeout_seconds, &failed);
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
                                                  const fly::CMString& db_path,
                                                  const fly::CMVector<fly::CMString>& merged_object_full_names,
                                                  const fly::CMVector<uint64_t>& source_worker_ids,
                                                  const fly::CMVector<uint64_t>& merge_target_worker_ids,
                                                  const fly::CMString& merge_db_path,
                                                  const fly::CMString& merge_data_path) {
        self.cleanup_after_merge(db_path, merged_object_full_names, source_worker_ids,
                                  merge_target_worker_ids, merge_db_path, merge_data_path);
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
    // role（静态身份）：独立第 5 参，"hybrid"（默认）/ "storage_only"。
    FLY_EXPORT_INIT(uint64_t, fly::CMString, uint16_t, fly::CMVector<fly::CMString>, fly::CMString)
    FLY_EXPORT_METHOD("start", &fly::WorkerAgent::start)
    FLY_EXPORT_METHOD("stop", &fly::WorkerAgent::stop)
    FLY_EXPORT_METHOD("is_running", &fly::WorkerAgent::is_running)
    FLY_EXPORT_METHOD("get_worker_id", &fly::WorkerAgent::get_worker_id)
    // 退出码透传链末端（graceful=0/abnormal=3）：fly/main.py 经此取值
    // sys.exit——bsub/ssh 等外部观测方据进程退出码区分 worker 退出性质。
    FLY_EXPORT_METHOD("exit_code", &fly::WorkerAgent::exit_code)
    FLY_EXPORT_METHOD("is_registered", &fly::WorkerAgent::is_registered)
    // ── 执行上提原语（Python 主循环驱动）：take_task 空等不占 GIL（同进程
    // Python 线程不被压制）；finish_task 纯 C++ 收尾（GIL 释放执行）。旧的
    // poll_task/poll_task_blocking/set_executor 绑定已删——它们经 C++
    // executor 反调 Python，是 GIL 100ms 压制问题的根源。
    FLY_EXPORT_METHOD("take_task", &take_task_gil_released)
    FLY_EXPORT_METHOD("finish_task", &finish_task_gil_released)
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
                                         int priority,
                                         const fly::CMString& owner_db_path) -> uint64_t {
        return self.submit_task(name, module, args, inputs, required_capabilities, attribute_timeout, write_context_hash, vars, priority, owner_db_path);
    })
    FLY_EXPORT_METHOD("take_pending_task_vars", [](fly::WorkerAgent& self) -> fly::CMVector<fly::VarPayload> {
        return self.take_pending_task_vars();
    })
    FLY_EXPORT_METHOD("register_database", [](fly::WorkerAgent& self,
                                                const fly::CMString& db_path,
                                                CMSharedPtr<Database> db) {
        self.register_database(db_path, std::move(db));
    })
    FLY_EXPORT_METHOD("get_database", [](fly::WorkerAgent& self,
                                           const fly::CMString& db_path) -> CMSharedPtr<Database> {
        return self.get_database(db_path);
    })
    FLY_EXPORT_DEF("request_remote_data", [](fly::WorkerAgent& self, const fly::CMString& object_name) -> fly_export::tuple {
        auto [refreshed, can_still_produce] = self.request_remote_data(object_name);
        return fly_export::make_tuple(refreshed, can_still_produce);
    })
    FLY_EXPORT_METHOD("request_db_path", [](fly::WorkerAgent& self,
                                               const fly::CMString& db_path) -> bool {
        return self.request_db_path(db_path);
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
    })

    // ── 业务 RPC（PeerChannelGroup 底层）──────────────────────────
    // payload 参数全程用 nanobind bytes（零拷贝），消除 Python 侧 latin-1 编解码 hack。
    FLY_EXPORT_METHOD("start_peer_rpc_listen", [](fly::WorkerAgent& self,
                                                      const fly::CMString& host,
                                                      int port) {
        return self.start_peer_rpc_listen(host, port);
    })
    FLY_EXPORT_METHOD("peer_rpc_connect", [](fly::WorkerAgent& self,
                                                const fly::CMString& host,
                                                int port,
                                                int retries,
                                                int retry_interval_ms) {
        return self.peer_rpc_connect(host, port, retries, retry_interval_ms);
    })
    FLY_EXPORT_METHOD("peer_rpc_call", [](fly::WorkerAgent& self,
                                             uint64_t conn_id,
                                             fly_export::bytes payload,
                                             int timeout_ms) {
        fly::CMString payload_str(payload.c_str(), payload.size());
        auto result = peer_call_gil_released(self, conn_id, payload_str,
                                             timeout_ms);
        return fly_export::make_tuple(
            result.first,
            fly_export::bytes(result.second.data(), result.second.size()));
    })
    FLY_EXPORT_METHOD("peer_rpc_respond", [](fly::WorkerAgent& self,
                                                uint64_t conn_id,
                                                uint64_t rpc_id,
                                                fly_export::bytes payload) {
        fly::CMString payload_str(payload.c_str(), payload.size());
        return self.peer_rpc_respond(conn_id, rpc_id, payload_str);
    })
    // ── 流式大 payload（流插件化 2026-08-31）── writer 构造的 lambda 参数
    // 含逗号，按仓库惯例抽独立函数（见 peer_call_gil_released 注释）。
    FLY_EXPORT_METHOD("peer_stream_writer", [](fly::WorkerAgent& self,
                                               uint64_t conn_id,
                                               std::string compression,
                                               int level) {
        return peer_stream_writer_export(self, conn_id, compression, level);
    })
    FLY_EXPORT_METHOD("peer_stream_respond_writer", [](fly::WorkerAgent& self,
                                                       uint64_t conn_id,
                                                       uint64_t rpc_id,
                                                       std::string compression,
                                                       int level) {
        return peer_stream_respond_writer_export(self, conn_id, rpc_id,
                                                 compression, level);
    })
    FLY_EXPORT_METHOD("peer_stream_call_wait", [](fly::WorkerAgent& self,
                                                  uint64_t rpc_id,
                                                  int timeout_ms) {
        return peer_stream_call_wait_export(self, rpc_id, timeout_ms);
    })
    FLY_EXPORT_METHOD("peer_rpc_respond_failure", [](fly::WorkerAgent& self,
                                                        uint64_t conn_id,
                                                        uint64_t rpc_id,
                                                        fly_export::bytes reason) {
        fly::CMString reason_str(reason.c_str(), reason.size());
        return self.peer_rpc_respond_failure(conn_id, rpc_id, reason_str);
    })
    FLY_EXPORT_METHOD("peer_rpc_respond_not_ready", [](fly::WorkerAgent& self,
                                                         uint64_t conn_id,
                                                         uint64_t rpc_id,
                                                         fly_export::bytes reason) {
        fly::CMString reason_str(reason.c_str(), reason.size());
        return self.peer_rpc_respond_not_ready(conn_id, rpc_id, reason_str);
    })
    FLY_EXPORT_METHOD("peer_rpc_recv_request", [](fly::WorkerAgent& self,
                                                     int timeout_ms) {
        // timeout_ms=0（无限等待）时本调用会长时间阻塞在 cv 上：必须释放
        // GIL，否则同进程其它 Python 线程（如 task 主线程）全部冻结——
        // service 常驻线程架构下实测把 Thread.start() 的调用者冻死。
        fly::WorkerAgent::PeerRpcRequest req;
        {
            fly_export::gil_scoped_release release;
            req = self.peer_rpc_recv_request(timeout_ms);
        }
        return fly_export::make_tuple(
            req.conn_id_, req.rpc_id_, req.src_worker_id_,
            fly_export::bytes(req.payload_.data(), req.payload_.size()));
    })
    FLY_EXPORT_METHOD("peer_rpc_notify_failure", [](fly::WorkerAgent& self,
                                                        uint64_t conn_id,
                                                        fly_export::bytes reason) {
        fly::CMString reason_str(reason.c_str(), reason.size());
        return self.peer_rpc_notify_failure(conn_id, reason_str);
    })
    FLY_EXPORT_METHOD("peer_rpc_close", [](fly::WorkerAgent& self,
                                              uint64_t conn_id) {
        self.peer_rpc_close(conn_id);
    })
    FLY_EXPORT_METHOD("stop_peer_rpc", [](fly::WorkerAgent& self) {
        self.stop_peer_rpc();
    })
    FLY_EXPORT_METHOD("peer_rpc_port", [](fly::WorkerAgent& self) {
        return self.peer_rpc_port();
    });

}