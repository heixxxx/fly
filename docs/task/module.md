# Task 模块 — 任务系统层

## 模块概述

**位置**: `src/task/`

任务系统层负责任务的依赖管理、调度决策、Worker 状态管理、任务元数据和心跳监控。是连接网络层和 Agent 层的核心调度引擎。

---

## 核心文件

| 文件 | 说明 |
|------|------|
| `cpp/dependency_graph.h/cpp` | 任务依赖管理，mark_data_ready 触发就绪 |
| `cpp/worker_manager.h/cpp` | Worker 注册/状态管理 |
| `cpp/task_scheduler.h/cpp` | FIFO 调度，依赖就绪检测 |
| `cpp/task_manager.h/cpp` | 任务元数据管理（状态、时间戳、Worker 分配） |
| `cpp/heartbeat_monitor.h/cpp` | 心跳监控，超时检测 |

---

## 类详细说明

### DependencyGraph（依赖图）

```cpp
class DependencyGraph {
public:
    // 添加任务及其输入依赖
    void add_task(uint64_t task_id, const CMVector<CMString>& inputs,
                  const CMVector<CMString>& required_capabilities = {});

    // 标记数据就绪，返回因此变为 ready 的任务列表
    CMVector<uint64_t> mark_data_ready(const CMString& data_path);

    // 查询
    bool is_data_ready(const CMString& data_path) const;
    bool is_task_ready(uint64_t task_id) const;
    bool has_task(uint64_t task_id) const;
    CMVector<uint64_t> get_ready_tasks() const;
    CMVector<uint64_t> get_pending_tasks() const;
    CMVector<CMString> get_task_dependencies(uint64_t task_id) const;

    // 移除已完成任务
    void remove_task(uint64_t task_id);

private:
    // 任务依赖映射
    CMMap<uint64_t, CMVector<CMString>> task_dependencies_;
    CMMap<uint64_t, CMVector<CMString>> task_capabilities_;

    // 数据就绪状态
    CMUnorderedMap<CMString, bool> data_ready_status_;

    // 未满足依赖计数（归零则 ready）
    CMMap<uint64_t, int> pending_count_;

    // 就绪任务队列
    CMVector<uint64_t> ready_tasks_;
};
```

**依赖解析算法**:

```
add_task(task_id, inputs)
  → task_dependencies_[task_id] = inputs
  → pending_count_[task_id] = 0
  → for each input in inputs:
      if !data_ready_status_[input]:
        pending_count_[task_id]++
  → if pending_count_[task_id] == 0:
      ready_tasks_.push_back(task_id)

mark_data_ready(data_path)
  → data_ready_status_[data_path] = true
  → newly_ready = []
  → for each (task_id, count) in pending_count_:
      if task_id depends on data_path:
        count--
        if count == 0:
          newly_ready.push_back(task_id)
  → return newly_ready
```

---

### WorkerManager（Worker 管理）

```cpp
enum class WorkerStatus : uint8_t {
    IDLE = 0,
    BUSY = 1,
    DEAD = 2,
};

struct WorkerInfo {
    uint64_t worker_id;
    CMString address;
    uint16_t port;
    WorkerStatus status;
    CMVector<CMString> capabilities;  // 动态可更新
    uint64_t last_heartbeat;
    uint64_t current_task_id;
};

class WorkerManager {
public:
    void register_worker(uint64_t worker_id, const CMString& address, uint16_t port,
                         const CMVector<CMString>& capabilities = {});
    void register_worker(uint64_t worker_id, const CMString& address,
                         const CMVector<CMString>& capabilities);
    void unregister_worker(uint64_t worker_id);
    void update_worker_status(uint64_t worker_id, WorkerStatus status);
    void record_heartbeat(uint64_t worker_id);
    void set_heartbeat(uint64_t worker_id, uint64_t timestamp);
    void assign_task(uint64_t worker_id, uint64_t task_id);
    void complete_task(uint64_t worker_id);

    WorkerInfo* get_worker(uint64_t worker_id);
    CMVector<uint64_t> get_idle_workers();
    CMVector<uint64_t> get_workers_with_capability(const CMString& capability);
    CMVector<WorkerInfo> get_all_workers();
    size_t get_worker_count();
    size_t get_idle_worker_count();

    // 动态能力管理
    void update_capabilities(uint64_t worker_id, const CMVector<CMString>& added,
                             const CMVector<CMString>& removed);
    bool has_worker_with_all_capabilities(const CMVector<CMString>& requirements) const;

private:
    CMMap<uint64_t, WorkerInfo> workers_;
};
```

**Worker 生命周期**:

```
注册 → register_worker(id, address, port, capabilities) → status=IDLE
分配任务 → assign_task(id, task_id) → status=BUSY
任务完成 → complete_task(id) → status=IDLE
心跳超时 → heartbeat_monitor → status=DEAD
```

> **注意**: `RegisterMessage` 协议中存在 `role` 字段（"hybrid" | "storage_only"），这是预留的设计字段。当前 Worker 注册时**未填充**该字段，Master 也**未读取**该字段进行角色区分。WorkerManager 使用 `capabilities`（标签能力）而非 `role`（角色类型）进行 Worker 分组。

---

### TaskScheduler（任务调度器）

```cpp
struct ScheduleResult {
    uint64_t task_id;
    uint64_t worker_id;
    bool scheduled;
};

class TaskScheduler {
public:
    TaskScheduler(DependencyGraph* graph, WorkerManager* manager);

    ScheduleResult schedule_next();
    CMVector<ScheduleResult> schedule_all_available();
    void set_locality_preference(bool enabled);

private:
    uint64_t select_best_worker(uint64_t task_id);

    DependencyGraph* graph_;
    WorkerManager* manager_;
    bool locality_enabled_;
};
```

**调度算法**:

```
schedule_next()
  → ready_tasks = graph_->get_ready_tasks()
  → idle_workers = manager_->get_idle_workers()
  → if locality_enabled_:
      → select_best_worker(task_id) // 基于数据局部性
  → else:
      → FIFO 匹配 idle_workers[0]
  → return ScheduleResult{task_id, worker_id, scheduled}

schedule_all_available()
  → 循环调用 schedule_next() 直到无新调度
  → return CMVector<ScheduleResult>
```

**核心约束**: Worker 同一时刻最多执行一个任务。Master 仅向 `status=IDLE` 的 Worker 派发。

---

### TaskManager（任务元数据管理）

```cpp
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

class TaskManager {
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

private:
    CMMap<uint64_t, TaskMetadata> tasks_;
};
```

**职责范围**: 仅管理任务生命周期元数据（状态、名称、输入输出、时间戳），**不管理数据位置**（数据位置已迁移至 DataService）。

---

### HeartbeatMonitor（心跳监控）

```cpp
class HeartbeatMonitor {
public:
    HeartbeatMonitor(WorkerManager* manager, uint64_t timeout_seconds = 30);

    void check_all_workers(uint64_t current_time);
    uint64_t get_timeout() const;
    void set_timeout(uint64_t seconds);
    CMVector<uint64_t> get_dead_workers() const;

private:
    WorkerManager* manager_;
    uint64_t timeout_seconds_;
    CMVector<uint64_t> dead_workers_;
};
```

**心跳检测流程**:

```
Master.heartbeat_check_thread_ (每 heartbeat_interval 秒)
  → heartbeat_monitor_->check_all_workers(now)
  → for each worker:
      if (now - last_heartbeat) > timeout_seconds:
        manager_->update_worker_status(worker_id, WorkerStatus::DEAD)
        dead_workers_.push_back(worker_id)
```

> **注意**: 默认超时 30 秒（代码实现），配置文件 `heartbeat_timeout` 默认 120 秒。HeartbeatMonitor 构造时使用配置值覆盖默认值。

---

## 任务调度失败检测

当 `fail_unscheduleable_tasks=1` 时，`schedule_tasks()` 执行两项检查：

**Capability 检查**:
```
ready_tasks = graph_->get_ready_tasks()
for each task in ready_tasks:
    if task.required_capabilities is not empty:
        requirements = task.required_capabilities
        matching_workers = worker_manager->has_worker_with_all_capabilities(requirements)
        if not matching_workers:
            graph_->mark_task_failed(task.task_id)
            persist_failed_task(record)
```

**Dependency 检查**:
```
pending_tasks = graph_->get_pending_tasks()
if only pending_tasks exist (no ready, no running):
    for each pending in pending_tasks:
        if graph_->is_data_ready_all_dependencies(pending.task_id):
            mark_data_ready for all inputs
        else:
            mark_task_failed(pending.task_id)
            persist_failed_task(record)
```

---

## 核心流程

### 任务提交到调度

```
TaskSubmitMessage 到达 Master
  → metadata_->create_task(task_id, name, inputs, outputs, config)
  → graph_->add_task(task_id, inputs)  // 注册依赖
  → schedule_all_available()
      → 循环 schedule_next()
      → ready_tasks × idle_workers → 匹配
      → for each match:
          → manager_->assign_task(task_id, worker_id)
          → reactor_->send(worker_conn, TaskAssignMessage{...})
```

### 数据就绪触发下游

```
TaskCompleteMessage 到达 Master
  → for written_object in written_objects:
      → graph_->mark_data_ready(data_path)
      → DataService.update_remote_idx(...)
  → schedule_all_available()  // 调度新就绪的任务
```

---

## 设计决策

| 决策 | 原因 |
|------|------|
| pending_count 归零即 ready | O(1) 判断就绪，无需遍历所有依赖 |
| FIFO 调度 + 可选 Locality | 默认简单可靠，`set_locality_preference(true)` 启用数据局部性 |
| Worker 单任务约束 | 简化并发模型，Worker 无需任务队列 |
| WorkerStatus 枚举替代 bool is_busy | 支持 DEAD 状态，扩展性更强 |
| TaskManager 不管数据位置 | 任务生命周期与数据位置是不同关注点，解耦独立演进 |
| HeartbeatMonitor 独立线程 | 不阻塞 Reactor，超时检测准确 |
| role 字段预留未实现 | RegisterMessage.role 已定义，但 Worker 未填充、Master 未使用 |
