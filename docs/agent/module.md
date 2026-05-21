# Agent 模块 — Agent 层

## 模块概述

**位置**: `src/agent/`

Agent 层是框架的最高 C++ 层，封装 Master 和 Worker 的完整业务逻辑，包括消息处理、任务生命周期管理、数据传输协调和 Python 任务执行。

---

## 核心文件

| 文件 | 说明 |
|------|------|
| `cpp/master_agent.h/cpp` | Master 节点管理 |
| `cpp/worker_agent.h/cpp` | Worker 节点执行 |
| `cpp/task_executor.h/cpp` | 任务执行器 |
| `cpp/worker_context.h` | WorkerAgentContext + WriteRegistrationError |
| `export/agent_export.cpp` | nanobind Python 导出 |

---

## 类详细说明

### MasterAgent（Master 节点）

```cpp
class MasterAgent {
public:
    MasterAgent(const CMString& host, uint16_t port);
    ~MasterAgent();

    void start();
    void stop();
    bool is_running() const;

    void set_data_service(DataService* ds);

    // 任务提交
    void submit_task(uint64_t task_id, const CMString& name,
                     const CMString& module,
                     const CMVector<CMString>& args,
                     const CMVector<CMString>& inputs = {},
                     const CMVector<CMString>& outputs = {});

    // 查询
    CMVector<uint64_t> get_connected_workers() const;
    CMVector<uint64_t> get_pending_tasks() const;
    CMVector<uint64_t> get_running_tasks() const;
    CMVector<uint64_t> get_completed_tasks() const;
    CMVector<uint64_t> get_failed_tasks() const;
    CMString get_task_error(uint64_t task_id) const;
    CMVector<uint64_t> get_idle_workers() const;
    uint16_t get_port() const;

    // 数据库管理
    void register_database(const CMString& db_id, const CMString& base_path,
                           const CMString& data_path = "");
    bool is_db_frozen(const CMString& db_id) const;
    CMSharedPtr<Database> get_or_create_database(const CMString& base_path,
                                                  const CMString& data_path = "",
                                                  uint64_t writer_id = 0);

    // 远程数据读取
    ReadResult request_remote_data(const CMString& object_name);
    ReadResult request_data_from_worker(const CMString& host, int32_t port,
                                         const CMString& object_name);

private:
    CMString host_;
    uint16_t port_;
    int32_t data_server_port_ = 0;
    std::atomic<bool> running_{false};

    CMUniquePtr<Reactor> reactor_;
    std::unique_ptr<DependencyGraph> graph_;
    CMUniquePtr<WorkerManager> worker_manager_;
    CMUniquePtr<TaskScheduler> scheduler_;
    CMUniquePtr<TaskManager> metadata_;
    CMUniquePtr<HeartbeatMonitor> heartbeat_monitor_;

    CMMap<uint64_t, uint64_t> conn_to_worker_;
    CMMap<uint64_t, uint64_t> worker_to_conn_;
    CMMap<uint64_t, CMString> task_modules_;
    CMMap<uint64_t, CMVector<CMString>> task_args_;

    CMMap<CMString, CMMap<CMString, CMString>> db_registry_;
    CMMap<CMString, CMSharedPtr<Database>> db_instances_;
    CMSet<CMString> frozen_dbs_;

    std::thread reactor_thread_;
    std::thread heartbeat_check_thread_;

    void schedule_tasks();
    void assign_task_to_worker(uint64_t task_id, uint64_t worker_id);

    // Message handlers
    void on_worker_register(uint64_t conn_id, const RegisterMessage& msg);
    void on_heartbeat(uint64_t conn_id, const HeartbeatMessage& msg);
    void on_data_ready(uint64_t conn_id, const DataReadyMessage& msg);
    void on_task_complete(uint64_t conn_id, const TaskCompleteMessage& msg);
    void on_task_failed(uint64_t conn_id, const TaskFailedMessage& msg);
    void on_data_request(uint64_t conn_id, const DataRequestMessage& msg);
    void on_write_register(uint64_t conn_id, const WriteRegisterMessage& msg);
    void on_worker_property_update(uint64_t conn_id, const WorkerPropertyUpdateMessage& msg);
    void on_disconnect(uint64_t conn_id);
    void on_error(uint64_t conn_id, int error_code);

    // Failed task persistence
    void restart_failed_tasks(const CMString& file_path);
    void persist_failed_task(const FailedTaskRecord& record);
    void remove_persisted_task(uint64_t task_id);
    CMString get_failed_tasks_file_path() const;
};
```

**Master 核心映射**:

```
conn_to_worker_:  conn_id → worker_id      // 连接双向映射
worker_to_conn_:  worker_id → conn_id
task_modules_:    task_id → module_name
task_args_:       task_id → args[]
db_registry_:     db_id → {base_path → data_path}
db_instances_:    db_id → shared_ptr<Database>
frozen_dbs_:      set<db_id>                // 已冻结 DB 集合
```

**Master 启动流程**:

```
MasterAgent.start()
  1. create_transport("tcp")
  2. transport->listen(host, port)
  3. reactor_ = new Reactor(transport)
  4. 注册所有 message handlers
  5. reactor_thread_ = thread { reactor_->run() }
  6. heartbeat_check_thread_ = thread { check_loop() }
```

**schedule_tasks**:
```
schedule_tasks()
  → ready_tasks = graph_->get_ready_tasks()
  → idle_workers = worker_manager_->get_idle_workers()
  → for each ready_task:
      → 检查 required_capabilities (如果有)
      → 检查依赖 (graph_->is_data_ready)
      → 无匹配 Worker 或 依赖不可解 → persist_failed_task(task_id) → FAILED
      → 有匹配 Worker → assign_task_to_worker(task_id, worker_id)
  → schedule_all_available()
```

**Master 消息处理**:

```
on_task_complete(TaskCompleteMessage)
  → worker_manager_->complete_task(worker_id)   // Worker → IDLE
  → for written_object:
      → graph_->mark_data_ready(data_path)       // 触发下游
      → DataService.update_remote_idx(...)        // 更新远程索引
  → for frozen_db:
      → db_instances_[db_id]->freeze()            // Master 侧 C++ freeze
  → graph_->remove_task(task_id)
  → metadata_->update_task_status(task_id, COMPLETED)
  → remove_persisted_task(task_id)               // 清除持久化记录
  → schedule_tasks()                              // 调度新任务
```

---

### WorkerAgent（Worker 节点）

```cpp
class WorkerAgent {
public:
    WorkerAgent(uint64_t worker_id, const CMString& master_host, uint16_t master_port);
    ~WorkerAgent();

    void start();
    void stop();
    bool is_running() const;
    uint64_t get_worker_id() const;
    bool is_registered() const;

    // 任务执行
    void set_executor(CMSharedPtr<TaskExecutor> executor);
    bool has_pending_task() const;
    bool poll_task();

    void set_data_service(DataService* ds);

    // 写入跟踪
    void begin_task(uint64_t task_id);
    void record_write(const CMString& db_id, const CMString& object_name);
    CMVector<CMString> end_task(uint64_t task_id);
    void register_write_with_master(const CMString& db_id, const CMString& object_name);

    // 远程数据读取
    ReadResult request_data_from_worker(const CMString& host, int32_t port,
                                           const CMString& object_name);
    ReadResult request_remote_data(const CMString& object_name);

    // DB 路径查询
    bool request_db_path(const CMString& db_id);

    // Worker 属性管理
    void set_worker_property(const CMVector<CMString>& props);
    void remove_worker_property(const CMVector<CMString>& props);
    CMVector<CMString> get_worker_properties() const;

    // 任务提交（递归）
    void submit_task(const CMString& name, const CMString& module,
                     const CMVector<CMString>& args,
                     const CMVector<CMString>& inputs);

    // 数据库管理
    void register_database(const CMString& db_id, CMSharedPtr<Database> db);
    CMSharedPtr<Database> get_database(const CMString& db_id) const;

private:
    uint64_t worker_id_;
    CMString master_host_;
    uint16_t master_port_;
    std::atomic<bool> running_{false};
    std::atomic<bool> registered_{false};

    CMUniquePtr<Reactor> reactor_;
    uint64_t master_conn_;
    CMString data_server_host_;
    int32_t data_server_port_ = 0;

    CMSharedPtr<TaskExecutor> executor_;
    static void record_write_trampoline(void* ctx, const CMString& db_id, const CMString& name);
    static void register_write_trampoline(void* ctx, const CMString& db_id, const CMString& name);

    uint64_t current_task_id_ = 0;
    CMVector<CMString> current_writes_;

    std::queue<PendingTask> task_queue_;

    CMMap<CMString, CMSharedPtr<Database>> databases_;
    CMMap<CMString, CMSharedPtr<PendingDbPath>> pending_db_paths_;
    CMMap<CMString, CMSharedPtr<PendingWriteRegister>> pending_write_regs_;

    std::thread reactor_thread_;
    std::thread heartbeat_thread_;

    // Worker 属性管理
    mutable std::mutex attributes_mutex_;
    CMSet<CMString> attributes_;

    DataService* data_service_ = nullptr;

    // Message handlers
    void on_register_ack(const RegisterAckMessage& msg);
    void on_task_assign(const TaskAssignMessage& msg);
    void on_shutdown(const ShutdownMessage& msg);
    void on_db_path_response(const DbPathResponseMessage& msg);
    void on_data_request(uint64_t conn_id, const DataRequestMessage& msg);
    void on_write_register_ack(uint64_t conn_id, const WriteRegisterAckMessage& msg);
    void on_worker_property_update(uint64_t conn_id, const WorkerPropertyUpdateMessage& msg);
    void on_disconnect(uint64_t conn_id);

    void heartbeat_loop();
    void initiate_shutdown(const CMString& reason);
};
```

**Worker 核心映射**:

```
master_conn_:      uint64_t                 // 到 Master 的连接 ID
data_server_port_: int32_t                  // Data Server 监听端口
task_queue_:      queue<PendingTask>        // Reactor→Main 传递
databases_:       db_id → shared_ptr<Database>
pending_db_paths_: db_id → PendingDbPath    // DB 路径查询状态
pending_write_regs_: object → PendingWriteRegister  // 写入注册状态
current_task_id_: uint64_t                  // 当前任务
current_writes_:  vector<string>            // 当前写入记录
```

**Worker 启动流程**:

```
WorkerAgent.start()
  1. create_transport("tcp")
  2. transport->listen("0.0.0.0", 0)        // Data Server
  3. data_server_port_ = transport->get_bound_port()
  4. master_conn_ = transport->connect(master_host, master_port)
  5. reactor_ = new Reactor(transport)       // 共用一个 Reactor
  6. 注册所有 message handlers
  7. reactor_thread_ = thread { reactor_->run() }
  8. heartbeat_thread_ = thread { heartbeat_loop() }
  9. reactor_->send(master_conn_, RegisterMessage{...})
```

**任务执行流程**:

```
Worker.ReactorThread
  → on_task_assign(TaskAssignMessage)
    → task_queue_.push({task_id, name, module, args})  // 入队

Worker.MainThread (poll_task 循环)
  → poll_task()
    → task = task_queue_.pop()
    → begin_task(task_id)               // 设置 current_task_id_, 清空 writes
    │     └── WorkerAgentContext::set(trampoline, this)
    → executor_->execute(task_id, ...)
    │     → import module → pickle.loads(args) → 执行原始函数
    │     → 函数内 write_object → WorkerAgentContext 触发 record_write
    → tracked_writes = end_task(task_id)
    → [成功] reactor_->send(TaskCompleteMessage{written_objects, frozen_dbs})
    → [失败] reactor_->send(TaskFailedMessage{error_message, error_type})

  → on_worker_property_update(WorkerPropertyUpdateMessage)
    → set_worker_property(props)        // 更新 attributes_
    → reactor_->send(..., msg)           // 通知 Master
```

---

### TaskExecutor（任务执行器）

```cpp
struct EXTaskExecResult {
    uint64_t task_id;
    int status;                    // 0=SUCCESS, 1=FAILED
    CMString output;
    CMString error;
    CMVector<CMString> outputs;
    CMVector<CMString> frozen_dbs;
};

class TaskExecutor {
public:
    using ExecFunc = std::function<EXTaskExecResult(uint64_t, const CMString&,
                                                     const CMString&,
                                                     const CMVector<CMString>&)>;

    void set_exec_func(ExecFunc func);
    EXTaskExecResult execute(uint64_t task_id, const CMString& name,
                             const CMString& module,
                             const CMVector<CMString>& args);

private:
    ExecFunc exec_func_;
};
```

**执行模式**: exec_func 由 Python 层注入（通过 `create_executor(worker)`），内部完成 import module → deserialize args → 执行原始函数。

---

### WorkerAgentContext（写入跟踪上下文）

> **文件位置**: `src/common/cpp/worker_context.h`（非 `src/agent/cpp/`）

**回调类型定义**:

```cpp
using RecordWriteFunc = void(*)(void* ctx, const CMString& db_id, const CMString& name);
using RegisterWriteFunc = void(*)(void* ctx, const CMString& db_id, const CMString& name);
```

**WorkerAgentContext 类**:

```cpp
class WorkerAgentContext {
public:
    // 设置记录写入回调（任务开始时调用）
    static void set(RecordWriteFunc func, void* ctx);
    static void clear();

    // 触发记录写入
    static void record_write(const CMString& db_id, const CMString& object_name);

    // 设置写入注册回调（写入冻结DB时触发）
    static void set_register_func(RegisterWriteFunc func);
    static void register_write(const CMString& db_id, const CMString& object_name);

    // 状态查询
    static bool is_active();
    static void set_last_error_type(TaskErrorType type);
    static TaskErrorType get_last_error_type();

private:
    static inline thread_local RecordWriteFunc func_ = nullptr;
    static inline thread_local void* ctx_ = nullptr;
    static inline thread_local RegisterWriteFunc register_func_ = nullptr;
    static inline thread_local TaskErrorType last_error_type_ = TaskErrorType::UNKNOWN;
};
```

**WriteRegistrationError 异常类**:

```cpp
class WriteRegistrationError : public std::runtime_error {
public:
    WriteRegistrationError(const CMString& what, TaskErrorType type);
    TaskErrorType error_type() const;
};
```

**回调模式（C 函数指针 + trampoline）**:

WorkerAgentContext 不存储 `WorkerAgent*` 指针，而是通过 **C 函数指针** 回调，实现 C++ 层与 Python 层的解耦：

```
任务开始:
  WorkerAgent.begin_task(task_id)
    → WorkerAgentContext::set(record_write_trampoline, this)
    → WorkerAgentContext::set_register_func(register_write_trampoline)

写入触发 (Python → C++ → 回调):
  Database._write_typed(name, data, py_name)
    → WorkerAgentContext::record_write(db_id, name)
      → func_(ctx_, db_id, name)           // C 函数指针调用
      → WorkerAgent::record_write_trampoline(void* ctx, db_id, name)
        → static_cast<WorkerAgent*>(ctx)->record_write(db_id, name)
        → current_writes_.push_back(db_id + ":" + name)

写入冻结 DB 触发:
  Database._write_typed() 检测到 db 已冻结
    → WorkerAgentContext::register_write(db_id, name)
      → register_func_(ctx_, db_id, name)   // C 函数指针调用
      → WorkerAgent::register_write_trampoline(void* ctx, db_id, name)
        → static_cast<WorkerAgent*>(ctx)->register_write_with_master(db_id, name)
        → 向 Master 发送 WriteRegisterMessage
        → Master 检查 → WriteRegisterAckMessage
        → Worker 收到 ACK: success=false → 抛 WriteRegistrationError

任务结束:
  WorkerAgent.end_task(task_id)
    → WorkerAgentContext::clear()
    → return current_writes_
```

**设计意图**: 使用 C 函数指针 + `void*` 而非 C++ 模板/虚函数，避免 `worker_context.h` 对 `WorkerAgent` 类的头文件依赖，保持 common 模块的独立性。

---

### FailedTaskRecord / FailedTaskFile

```cpp
struct FailedTaskRecord {
    uint64_t task_id;
    CMString name;
    CMString module;
    CMVector<CMString> args;
    CMVector<CMString> inputs;       // 使用 db.get_obj_name() 生成 full name
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
```

**持久化机制**:
- Task 失败时（capability 不匹配或依赖无法解析），Master 将 `FailedTaskRecord` 序列化到 `log_dir/failed_tasks.bin`
- 失败时追加记录，成功完成时自动删除对应记录
- 失败日志打印 bin 文件路径和 restart API 用法

**Unresolvable Dependency 检测**:
- 当 `fail_unscheduleable_tasks=1` 时，`schedule_tasks()` 执行两项检查：
  1. **Capability 检查**: ready_tasks 中无匹配 Worker 的 task → FAILED
  2. **依赖检查**: 仅 pending_tasks 残留（无 ready、无 running）→ 依赖永远无法满足 → FAILED

---

## 核心流程

### 跨 Worker 数据读取

```
Worker A: db.read_object("key") (Python 三层降级)

Layer 1: DataService.try_read_local("key")
  → 找到 → DataReader → 返回
  → 未找到 → Layer 2

Layer 2: DataService.lookup_remote_idx("key")
  → 有缓存 → DataClient.request_data(host, port, "key")
  │     → 独立 TCP socket 直连 Worker B Data Server
  │     → Worker B: submit_transfer → IOThreadPool → reactor_->send(DataResponse)
  → 失败 → Layer 3

Layer 3: request_remote_data("key") (最多 3 次重试)
  → reactor_->send(master_conn_, DataQueryMessage)
  → Master: DataService.has_remote_location → DataLocationMessage
  → DataClient 直连目标 Worker
  → 成功 → update_remote_idx 缓存
```

### DB 路径查询

```
WorkerAgent.request_db_path(db_id)
  → 查本地 databases_[db_id] → 已有 → return true
  → reactor_->send(master_conn_, DbPathRequestMessage{db_id})
  → Master: 查 db_registry_ → DbPathResponseMessage
  → Worker: 创建 Database(base_path, data_path, worker_id)
  → 存入 databases_[db_id] → return true
```

### 关机流程

```
Master.stop()
  → 广播 ShutdownMessage 给所有 worker_to_conn_
  → heartbeat_check_running_ = false; cv_.notify_all()
  → heartbeat_check_thread_.join()
  → reactor_->stop()  → running_ = false
  → reactor_thread_.join()

Worker.on_shutdown()
  → registered_ = false
```

---

## Python 导出

```cpp
FLY_EXPORT_MODULE(_fly_agent) {
    FLY_EXPORT_CLASS(MasterAgent, "EXAgentMaster")
        FLY_EXPORT_INIT(CMString, int)
        FLY_EXPORT_METHOD("start", &MasterAgent::start)
        FLY_EXPORT_METHOD("stop", &MasterAgent::stop)
        FLY_EXPORT_METHOD("submit_task_with_deps", ...)
        FLY_EXPORT_METHOD("get_or_create_database", ...)
        FLY_EXPORT_METHOD("get_port", &MasterAgent::get_port)
        FLY_EXPORT_METHOD("get_pending_tasks", ...)
        FLY_EXPORT_METHOD("is_running", &MasterAgent::is_running)
        FLY_EXPORT_METHOD("restart_failed_tasks", &MasterAgent::restart_failed_tasks);

    FLY_EXPORT_CLASS(WorkerAgent, "EXAgentWorker")
        FLY_EXPORT_INIT(uint64_t, CMString, int)
        FLY_EXPORT_METHOD("start", &WorkerAgent::start)
        FLY_EXPORT_METHOD("stop", &WorkerAgent::stop)
        FLY_EXPORT_METHOD("poll_task", &WorkerAgent::poll_task)
        FLY_EXPORT_METHOD("submit_task", ...)
        FLY_EXPORT_METHOD("request_remote_data", ...)
        FLY_EXPORT_METHOD("request_data_from_worker", ...)
        FLY_EXPORT_METHOD("request_db_path", ...)
        FLY_EXPORT_METHOD("is_running", &WorkerAgent::is_running)
        FLY_EXPORT_METHOD("set_worker_property", &WorkerAgent::set_worker_property)
        FLY_EXPORT_METHOD("remove_worker_property", &WorkerAgent::remove_worker_property)
        FLY_EXPORT_METHOD("get_worker_properties", &WorkerAgent::get_worker_properties);

    FLY_EXPORT_CLASS(TaskExecutor, "EXTaskExecutor") ...;
    FLY_EXPORT_ENUM(EXTaskExecStatus, "EXTaskExecStatus") ...;
}
```

---

## 设计决策

| 决策 | 原因 |
|------|------|
| Master + Worker 共用 Reactor 模式 | 统一事件驱动，handler 无锁 |
| Worker 单任务约束（IDLE/BUSY/DEAD） | WorkerStatus 枚举支持 DEAD 状态，比 bool is_busy 更强 |
| C 函数指针 + trampoline 回调 | WorkerAgentContext 不依赖 WorkerAgent 头文件，保持 common 模块独立 |
| Master fatal error 设 flag 而不调 stop() | 避免 detached thread 调 stop() 崩溃 |
| DataClient 独立 TCP | Worker A 读数据不走主 Reactor，避免多线程读冲突 |
| 递归任务提交 | Worker 内 task 调用 task → submit_task → Master 调度 |
| Master liveness tracking | Worker 跟踪 last_master_contact，检测 Master 断连 |
| register_database 显式注册 | Master 和 Worker 都可注册 DB，支持分布式 Database 实例查找 |
