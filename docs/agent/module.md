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
    MasterAgent(const CMString& host, int port);
    ~MasterAgent();

    void start();
    void stop();

    // 任务提交
    void submit_task_with_deps(uint64_t task_id, const CMString& name,
                               const CMString& module,
                               const CMVector<CMString>& args,
                               const CMVector<CMString>& inputs,
                               const CMVector<CMString>& outputs);

    // 数据库管理
    std::shared_ptr<Database> get_or_create_database(const CMString& base_path,
                                                      const CMString& data_path,
                                                      int writer_id);

    // 查询
    int get_port() const;
    int get_pending_tasks() const;
    int get_running_tasks() const;
    int get_completed_tasks() const;
    bool is_running() const;

private:
    // 核心组件
    std::unique_ptr<Reactor> reactor_;
    std::shared_ptr<TaskScheduler> scheduler_;
    std::shared_ptr<DependencyGraph> graph_;
    std::shared_ptr<WorkerManager> worker_manager_;
    std::shared_ptr<MetadataManager> metadata_;
    std::shared_ptr<HeartbeatMonitor> heartbeat_monitor_;

    // 连接映射
    CMMap<uint64_t, uint64_t> conn_to_worker_;   // conn_id → worker_id
    CMMap<uint64_t, uint64_t> worker_to_conn_;   // worker_id → conn_id

    // 任务参数存储
    CMMap<uint64_t, CMVector<CMString>> task_args_;

    // 数据库管理
    CMMap<CMString, std::shared_ptr<Database>> db_instances_;
    CMMap<CMString, std::pair<CMString, CMString>> db_registry_;  // db_id → {base, data}
    CMSet<CMString> frozen_dbs_;

    // 线程
    std::thread reactor_thread_;
    std::thread heartbeat_check_thread_;

    // Message handlers
    void on_worker_register(uint64_t conn_id, const RegisterMessage& msg);
    void on_heartbeat(uint64_t conn_id, const HeartbeatMessage& msg);
    void on_task_complete(uint64_t conn_id, const TaskCompleteMessage& msg);
    void on_task_failed(uint64_t conn_id, const TaskFailedMessage& msg);
    void on_data_query(uint64_t conn_id, const DataQueryMessage& msg);
    void on_data_request(uint64_t conn_id, const DataRequestMessage& msg);
    void on_db_path_request(uint64_t conn_id, const DbPathRequestMessage& msg);
    void on_write_register(uint64_t conn_id, const WriteRegisterMessage& msg);
    void on_disconnect(uint64_t conn_id);

    void schedule_tasks();
};
```

**Master 核心映射**:

```
conn_to_worker_:  conn_id → worker_id      // 连接双向映射
worker_to_conn_:  worker_id → conn_id
task_args_:       task_id → args[]          // 任务参数
db_registry_:     db_id → {base_path, data_path}
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

**Master 消息处理**:

```
on_task_complete(TaskCompleteMessage)
  → worker_manager_->complete_task(worker_id)   // Worker 空闲
  → for written_object:
      → graph_->mark_data_ready(data_path)       // 触发下游
      → DataService.update_remote_idx(...)        // 更新远程索引
  → for frozen_db:
      → db_instances_[db_id]->freeze()            // Master 侧 C++ freeze
  → graph_->remove_task(task_id)
  → metadata_->update_task_status(task_id, COMPLETED)
  → schedule_tasks()                              // 调度新任务

on_write_register(WriteRegisterMessage)
  → 检查 is_db_frozen(db_id)
  → 未冻结 → ACK(success=true)
  → 已冻结 → ACK(success=false, error_type=WRITE_TO_FROZEN_DB)

on_task_failed(TaskFailedMessage)
  → 检查 error_type 是否为 fatal (WRITE_TO_FROZEN_DB 等)
  → fatal → 广播 ShutdownMessage
  → 非fatal → 记录失败，后续可重试
```

---

### WorkerAgent（Worker 节点）

```cpp
class WorkerAgent {
public:
    WorkerAgent(uint64_t worker_id, const CMString& master_host, int master_port);
    ~WorkerAgent();

    void start();
    void stop();

    // 任务执行
    void poll_task();

    // 远程数据读取
    DataResponse request_data_from_worker(const CMString& host, int32_t port,
                                           const CMString& object_name);
    DataResponse request_remote_data(const CMString& object_name);

    // DB 路径查询
    bool request_db_path(const CMString& db_id);

    // 任务提交（递归）
    void submit_task(const CMString& name, const CMString& module,
                     const CMVector<CMString>& args,
                     const CMVector<CMString>& inputs);

    // 数据库管理
    void register_database(const CMString& db_id, std::shared_ptr<Database> db);

    // 查询
    bool is_running() const;

private:
    uint64_t worker_id_;
    uint64_t master_conn_;
    int32_t data_server_port_;

    // Reactor (Master conn + Data Server 共用)
    std::unique_ptr<Reactor> reactor_;
    std::unique_ptr<TaskExecutor> executor_;

    // 任务队列 (Reactor → Main Thread)
    ThreadSafeQueue<PendingTask> task_queue_;

    // 数据库管理
    CMMap<CMString, std::shared_ptr<Database>> databases_;

    // 远程数据请求状态
    CMMap<CMString, PendingRemoteData> pending_data_;
    CMMap<CMString, PendingDbPath> pending_db_paths_;

    // 写入跟踪
    uint64_t current_task_id_;
    CMVector<CMString> current_writes_;

    // 线程
    std::thread reactor_thread_;
    std::thread heartbeat_thread_;

    // Message handlers
    void on_register_ack(uint64_t conn_id, const RegisterAckMessage& msg);
    void on_task_assign(uint64_t conn_id, const TaskAssignMessage& msg);
    void on_data_location(uint64_t conn_id, const DataLocationMessage& msg);
    void on_data_request(uint64_t conn_id, const DataRequestMessage& msg);
    void on_db_path_response(uint64_t conn_id, const DbPathResponseMessage& msg);
    void on_write_register_ack(uint64_t conn_id, const WriteRegisterAckMessage& msg);
    void on_shutdown(uint64_t conn_id, const ShutdownMessage& msg);
};
```

**Worker 核心映射**:

```
master_conn_:      uint64_t                 // 到 Master 的连接 ID
data_server_port_: int32_t                  // Data Server 监听端口
task_queue_:      queue<PendingTask>        // Reactor→Main 传递
databases_:       db_id → shared_ptr<Database>
pending_data_:    object_name → PendingRemoteData
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

```cpp
class WorkerAgentContext {
public:
    static void set(WorkerAgent* agent);
    static WorkerAgent* current();
    static void clear();

private:
    static thread_local WorkerAgent* current_agent_;
};

class WriteRegistrationError : public std::runtime_error {
public:
    TaskErrorType error_type;
    WriteRegistrationError(TaskErrorType type, const CMString& msg);
};
```

**写入回调链**:

```
Database._write_typed(name, data, py_name)
  → WorkerAgentContext::current() → WorkerAgent*
  → agent->record_write(db_id_, name)
  → current_writes_.push_back(db_id_ + ":" + name)
```

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
        FLY_EXPORT_METHOD("is_running", &MasterAgent::is_running);

    FLY_EXPORT_CLASS(WorkerAgent, "EXAgentWorker")
        FLY_EXPORT_INIT(uint64_t, CMString, int)
        FLY_EXPORT_METHOD("start", &WorkerAgent::start)
        FLY_EXPORT_METHOD("stop", &WorkerAgent::stop)
        FLY_EXPORT_METHOD("poll_task", &WorkerAgent::poll_task)
        FLY_EXPORT_METHOD("submit_task", ...)
        FLY_EXPORT_METHOD("request_remote_data", ...)
        FLY_EXPORT_METHOD("request_data_from_worker", ...)
        FLY_EXPORT_METHOD("request_db_path", ...)
        FLY_EXPORT_METHOD("is_running", &WorkerAgent::is_running);

    FLY_EXPORT_CLASS(TaskExecutor, "EXTaskExecutor") ...;
    FLY_EXPORT_ENUM(EXTaskExecStatus, "EXTaskExecStatus") ...;
}
```

---

## 设计决策

| 决策 | 原因 |
|------|------|
| Master + Worker 共用 Reactor 模式 | 统一事件驱动，handler 无锁 |
| Worker task_slot 而非 task_queue | Master 不向忙碌 Worker 派发，最多 1 个任务 |
| Thread-local WorkerAgentContext | C++ exception 跨 nanobind 丢失类型信息，thread-local 保存 |
| Master fatal error 不调 stop() | 避免 detached thread 调 stop() 崩溃，只设 flag + 广播 |
| DataClient 独立 TCP | Worker A 读数据不走主 Reactor，避免多线程读冲突 |
| 递归任务提交 | Worker 内 task 调用 task → submit_task → Master 调度 |
