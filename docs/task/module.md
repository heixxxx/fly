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
| `cpp/metadata_manager.h/cpp` | 任务元数据（仅 task lifecycle） |
| `cpp/heartbeat_monitor.h/cpp` | 心跳监控，超时检测 |

---

## 类详细说明

### DependencyGraph（依赖图）

```cpp
class DependencyGraph {
public:
    // 添加任务及其输入依赖
    void add_task(uint64_t task_id, const CMVector<CMString>& inputs);

    // 标记数据就绪，返回因此变为 ready 的任务列表
    CMVector<uint64_t> mark_data_ready(const CMString& data_path);

    // 查询
    bool is_task_ready(uint64_t task_id) const;
    bool has_task(uint64_t task_id) const;
    CMVector<uint64_t> get_ready_tasks() const;

    // 移除已完成任务
    void remove_task(uint64_t task_id);

private:
    // 任务依赖映射
    CMMap<uint64_t, CMVector<CMString>> task_dependencies_;

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
class WorkerManager {
public:
    // 注册
    void register_worker(uint64_t worker_id, const CMString& role,
                         const CMVector<CMString>& attributes);

    // 状态更新
    void complete_task(uint64_t worker_id);
    void set_heartbeat(uint64_t worker_id, double timestamp);

    // 查询
    WorkerInfo get_worker(uint64_t worker_id) const;
    CMVector<WorkerInfo> get_available_workers() const;
    bool has_worker(uint64_t worker_id) const;
    CMVector<uint64_t> get_all_worker_ids() const;

    // 属性管理
    void update_attributes(uint64_t worker_id,
                           const CMVector<CMString>& add,
                           const CMVector<CMString>& remove);

private:
    CMMap<uint64_t, WorkerInfo> workers_;
};

struct WorkerInfo {
    uint64_t worker_id;
    CMString role;                         // "hybrid" | "storage_only"
    CMVector<CMString> attributes;
    bool is_busy = false;
    uint64_t current_task_id = 0;
    double last_heartbeat = 0.0;
};
```

**Worker 生命周期**:

```
注册 → register_worker(id, role, attrs) → is_busy=false
分配任务 → mark_worker_busy(id, task_id) → is_busy=true
任务完成 → complete_task(id) → is_busy=false
```

---

### TaskScheduler（任务调度器）

```cpp
class TaskScheduler {
public:
    explicit TaskScheduler(std::shared_ptr<DependencyGraph> graph,
                           std::shared_ptr<WorkerManager> workers);

    // 提交任务
    void submit_task(uint64_t task_id, const CMString& name,
                     const CMString& module, const CMVector<CMString>& args,
                     const CMVector<CMString>& inputs,
                     const CMVector<CMString>& required_attributes);

    // 调度
    void schedule_all_available();

    // 查询
    int get_pending_count() const;
    int get_ready_count() const;
    CMVector<uint64_t> get_pending_tasks() const;

private:
    std::shared_ptr<DependencyGraph> graph_;
    std::shared_ptr<WorkerManager> workers_;

    // 任务参数存储
    CMMap<uint64_t, CMString> task_names_;
    CMMap<uint64_t, CMString> task_modules_;
    CMMap<uint64_t, CMVector<CMString>> task_args_;
    CMMap<uint64_t, CMVector<CMString>> task_required_attrs_;
};
```

**FIFO 调度算法**:

```
schedule_all_available()
  → ready_tasks = graph_->get_ready_tasks()
  → available_workers = workers_->get_available_workers()
  → for each (task, worker) pair (FIFO 匹配):
      → assign_task(task_id, worker_id)
      → 发送 TaskAssignMessage
```

**核心约束**: Worker 同一时刻最多执行一个任务。Master 仅向 `is_busy=false` 的 Worker 派发。

---

### MetadataManager（元数据管理）

```cpp
struct TaskMetadata {
    uint64_t task_id;
    CMString task_name;
    CMString module;
    CMVector<CMString> inputs;
    int status;  // PENDING=0, READY=1, RUNNING=2, COMPLETED=3, FAILED=4
};

class MetadataManager {
public:
    void create_task(uint64_t task_id, const CMString& name,
                     const CMVector<CMString>& inputs, ...);
    void update_task_status(uint64_t task_id, int status);
    TaskMetadata get_task(uint64_t task_id) const;
    CMVector<TaskMetadata> get_tasks_by_status(int status) const;

private:
    CMMap<uint64_t, TaskMetadata> tasks_;
};
```

**职责范围**: 仅管理任务生命周期元数据（状态、名称、模块、输入），**不管理数据位置**（数据位置已迁移至 DataService）。

---

### HeartbeatMonitor（心跳监控）

```cpp
class HeartbeatMonitor {
public:
    explicit HeartbeatMonitor(std::shared_ptr<WorkerManager> workers);

    // 检查所有 Worker 心跳
    void check_all_workers(double current_timestamp);

    // 获取超时 Worker 列表
    CMVector<uint64_t> get_dead_workers() const;

    void set_timeout(double seconds);

private:
    std::shared_ptr<WorkerManager> workers_;
    double timeout_seconds_ = 120.0;
    CMVector<uint64_t> dead_workers_;
};
```

**心跳检测流程**:

```
Master.heartbeat_check_thread_ (每 5s)
  → heartbeat_monitor_->check_all_workers(now)
  → for each worker:
      if (now - last_heartbeat) > timeout_seconds:
        dead_workers_.push_back(worker_id)
```

---

## 核心流程

### 任务提交到调度

```
TaskSubmitMessage 到达 Master
  → metadata_->create_task(task_id, name, inputs, ...)
  → graph_->add_task(task_id, inputs)  // 注册依赖
  → schedule_all_available()
      → ready_tasks × idle_workers → FIFO 匹配
      → for each match:
          → reactor_->send(worker_conn, TaskAssignMessage{...})
```

### 数据就绪触发下游

```
TaskCompleteMessage 到达 Master
  → for written_object in written_objects:
      → graph_->mark_data_ready(data_path)
      → 返回 newly_ready 任务列表
      → DataService.update_remote_idx(...)
  → schedule_all_available()  // 调度新就绪的任务
```

---

## 设计决策

| 决策 | 原因 |
|------|------|
| pending_count 归零即 ready | O(1) 判断就绪，无需遍历所有依赖 |
| FIFO 调度（默认） | 简单可靠，后续可扩展 Locality 策略 |
| Worker 单任务约束 | 简化并发模型，Worker 无需任务队列 |
| MetadataManager 不管数据位置 | 任务生命周期与数据位置是不同关注点，解耦独立演进 |
| HeartbeatMonitor 独立线程 | 不阻塞 Reactor，超时检测准确 |
| graph_->mark_data_ready 返回新就绪任务 | 一次调用获取调度候选，减少轮询 |
