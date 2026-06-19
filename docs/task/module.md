# Task 模块 — 任务系统层

## 模块概述

**位置**: `src/task/`

任务系统层负责任务的依赖管理、调度决策、Worker 状态管理、任务元数据和心跳监控。是连接网络层和 Agent 层的核心调度引擎。

---

## 核心文件

| 文件 | 说明 |
|------|------|
| `cpp/dependency_graph.h/cpp` | 任务依赖管理，反向索引优化 mark_data_ready |
| `cpp/worker_manager.h/cpp` | Worker 注册/状态管理 |
| `cpp/task_scheduler.h/cpp` | FIFO 调度，依赖就绪检测 |
| `cpp/task_manager.h/cpp` | 任务元数据管理（按状态分桶，O(1) 查询） |
| `cpp/heartbeat_monitor.h/cpp` | 心跳监控，超时检测 |

---

## 类详细说明

### DependencyGraph（依赖图）

```cpp
class DependencyGraph {
public:
    void add_task(uint64_t task_id, const CMVector<CMString>& inputs,
                  const CMVector<CMString>& required_capabilities = {});
    void mark_data_ready(const CMString& data_path);
    void mark_data_removed(const CMString& data_path);
    bool is_data_ready(const CMString& data_path) const;
    CMVector<uint64_t> get_ready_tasks() const;
    CMVector<uint64_t> get_pending_tasks() const;
    bool is_task_ready(uint64_t task_id) const;
    CMVector<CMString> get_task_requirements(uint64_t task_id) const;
    CMVector<CMString> get_task_dependencies(uint64_t task_id) const;
    void remove_task(uint64_t task_id);

private:
    bool check_and_move_to_ready(uint64_t task_id);

    CMUnorderedMap<uint64_t, CMVector<CMString>> task_dependencies_;
    CMUnorderedMap<CMString, bool> data_ready_status_;
    CMUnorderedMap<uint64_t, CMVector<CMString>> task_requirements_;
    CMUnorderedSet<uint64_t> ready_tasks_;
    CMUnorderedSet<uint64_t> pending_tasks_;
    CMUnorderedSet<uint64_t> completed_tasks_;

    // 反向索引：data_path → 依赖该数据的 pending task_ids
    // 避免 mark_data_ready 遍历所有 pending tasks
    CMUnorderedMap<CMString, CMUnorderedSet<uint64_t>> data_to_pending_tasks_;
};
```

**依赖解析算法（反向索引优化）**:

```
add_task(task_id, inputs)
  → task_dependencies_[task_id] = inputs
  → for each input in inputs:
      if !data_ready_status_[input]:
        data_to_pending_tasks_[input].insert(task_id)  // 构建反向索引
  → if all inputs ready:
      ready_tasks_.insert(task_id)
  else:
      pending_tasks_.insert(task_id)

mark_data_ready(data_path)
  → data_ready_status_[data_path] = true
  → affected_tasks = data_to_pending_tasks_[data_path]  // O(1) 查找
  → for each task_id in affected_tasks:                  // O(T) 遍历
      if check_and_move_to_ready(task_id):
        移除反向索引条目
```

**复杂度**: 从 O(P×D) 降到 O(T×D)，P = pending 任务总数，T = 依赖该数据的任务数（通常 T << P）

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
    uint64_t task_id_ = 0;
    CMString name_;
    TaskStatus status_ = TaskStatus::PENDING;
    CMVector<CMString> inputs_;
    CMVector<CMString> outputs_;
    CMString config_;
    CMVector<CMString> required_capabilities_;
    uint64_t created_at_ = 0;
    uint64_t started_at_ = 0;
    uint64_t completed_at_ = 0;
    CMString error_message_;
    uint64_t assigned_worker_id_ = 0;
    CMString write_context_hash_;
};

using TaskMetadataPtr = CMSharedPtr<TaskMetadata>;

class TaskManager {
public:
    // 原子复合操作（单次锁获取）
    void fail_task(uint64_t task_id, const CMString& error);
    void assign_task(uint64_t task_id, uint64_t worker_id);
    void unassign_task(uint64_t task_id);

    // O(1) 状态查询
    bool has_tasks_with_status(TaskStatus status) const;
    int count_tasks_by_status(TaskStatus status) const;

    // ID-only 查询（避免拷贝完整 TaskMetadata）
    CMVector<uint64_t> get_task_ids_by_status(TaskStatus status) const;
    CMVector<uint64_t> get_task_ids_by_worker(uint64_t worker_id) const;

    // 返回 shared_ptr（0.2ns 拷贝，无竞态）
    TaskMetadataPtr get_task(uint64_t task_id) const;

private:
    void move_task(uint64_t task_id, TaskStatus from, TaskStatus to);
    void maybe_cleanup_completed();

    // 按状态分桶 — get_tasks_by_status 只遍历目标桶
    CMUnorderedMap<uint64_t, TaskMetadataPtr> buckets_[5];
    CMUnorderedMap<uint64_t, TaskStatus> task_status_;
};
```

**存储结构**: 按状态分桶

```
buckets_[PENDING]  → {task_id → TaskMetadataPtr}
buckets_[RUNNING]  → {task_id → TaskMetadataPtr}
buckets_[COMPLETED] → {task_id → TaskMetadataPtr}
buckets_[FAILED]   → {task_id → TaskMetadataPtr}
buckets_[CANCELLED] → {task_id → TaskMetadataPtr}
```

**优势**:
- `get_tasks_by_status()` 从 O(n) 降到 O(k)，k = 该状态的任务数
- `has_tasks_with_status()` 从 O(n) 降到 O(1)
- `get_task()` 返回 shared_ptr，0.2ns 拷贝，无数据竞态
- 自动清理：completed+failed 超过 kMaxCompletedTasks 时淘汰最老任务

**原子复合操作**:
- `fail_task(task_id, error)` = `update_task_status(FAILED)` + `set_error()`
- `assign_task(task_id, worker_id)` = `update_task_status(RUNNING)` + `set_assigned_worker()`
- `unassign_task(task_id)` = `update_task_status(PENDING)` + `set_assigned_worker(0)`

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
};
```

---

## 任务调度失败检测

当 `fail_unscheduleable_tasks=1` 时，`schedule_tasks()` 执行两项检查：

**Capability 检查**:
```
ready_tasks = graph_->get_ready_tasks()
for each task in ready_tasks:
    if task.required_capabilities is not empty:
        if !worker_manager->has_worker_with_all_capabilities(requirements):
            fail_task(task_id, error)
            persist_failed_task(record)
```

**Dependency 检查**:
```
pending_tasks = graph_->get_pending_tasks()
if ready_tasks empty AND no running tasks:
    → 所有 pending tasks 的依赖无法满足
    → fail_task(task_id, error)
```

---

## 核心流程

### 任务提交到调度

```
TaskSubmitMessage 到达 Master
  → metadata_->create_task(task_id, name, inputs, outputs, config)
  → graph_->add_task(task_id, inputs)  // 注册依赖 + 构建反向索引
  → 预取依赖位置（submit_time prefetch）
      → for each input in inputs:
          loc = DataService->lookup_remote_idx(input)
          if found: task_dependency_locations_[task_id][input] = loc
  → schedule_tasks()
```

### 数据就绪触发下游

```
WriteRegister 到达 Master
  → graph_->mark_data_ready(data_path)  // O(T×D) 反向索引查询
  → DataService.update_remote_idx(...)
  → update_dependency_location_cache(...)  // 更新 pending tasks 的依赖位置
  → schedule_tasks()
```

### 任务分配（含依赖位置预取）

```
assign_task_to_worker(task_id, worker_id)
  → 构建 TaskAssignMessage
  → 从 task_dependency_locations_ 获取缓存的依赖位置
  → 直接查询 DataService 补充未缓存的依赖位置
  → 发送 TaskAssignMessage + dependency_locations_
```

---

## 设计决策

| 决策 | 原因 |
|------|------|
| DependencyGraph 反向索引 | mark_data_ready 从 O(P×D) 降到 O(T×D) |
| TaskManager 按状态分桶 | get_tasks_by_status 从 O(n) 降到 O(k) |
| TaskMetadataPtr shared_ptr | get_task 返回 0.2ns 拷贝，消除数据竞态 |
| 原子复合操作 | fail_task/assign_task/unassign_task 单次锁获取 |
| 自动清理 completed tasks | 防止内存无限增长，保留最近 100 个 |
| 依赖位置预取 | 消除 read_nb 路径的 master 查询开销 |
