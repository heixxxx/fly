# Task 模块 — 任务系统层

## 模块概述

**位置**: `src/task/`

任务系统层负责任务的依赖管理、调度决策、Worker 状态管理、任务元数据和心跳监控。是连接网络层和 Agent 层的核心调度引擎。

---

## 核心组件

| 组件 | 文件 | 职责 |
|------|------|------|
| DependencyGraph | `cpp/dependency_graph.h/cpp` | 任务依赖管理，数据就绪触发任务就绪 |
| WorkerManager | `cpp/worker_manager.h/cpp` | Worker 注册/状态/能力管理 |
| TaskScheduler | `cpp/task_scheduler.h/cpp` | FIFO 调度，依赖就绪检测 |
| TaskManager | `cpp/task_manager.h/cpp` | 任务元数据生命周期管理 |
| HeartbeatMonitor | `cpp/heartbeat_monitor.h/cpp` | 心跳监控，超时检测 |

---

## DependencyGraph

### 核心职责

管理任务之间的数据依赖关系。当某个数据就绪时，自动检测并更新依赖该数据的任务状态。

### 数据结构

- **task_dependencies_**: task_id → 依赖的数据列表
- **data_ready_status_**: data_path → 是否就绪
- **ready_tasks_**: 已就绪（所有依赖满足）的任务集合
- **pending_tasks_**: 未就绪（存在未满足依赖）的任务集合
- **data_to_pending_tasks_**: 反向索引，data_path → 依赖该数据的 pending task 集合

### 依赖解析算法

**添加任务**:
1. 记录任务的依赖列表
2. 检查每个依赖是否已就绪
3. 如果所有依赖已就绪 → 加入 ready_tasks
4. 否则 → 加入 pending_tasks，并构建反向索引

**标记数据就绪**:
1. 标记 data_path 为就绪
2. 通过反向索引找到依赖该数据的所有 pending tasks（O(1) 查找）
3. 对每个相关任务检查是否所有依赖都已满足
4. 如果满足 → 从 pending 移到 ready，清理反向索引

**复杂度**: O(T×D)，T = 依赖该数据的任务数，D = 平均依赖数。相比遍历所有 pending tasks 的 O(P×D)，当 T << P 时有显著提升。

---

## TaskManager

### 核心职责

管理任务的生命周期元数据：状态、名称、输入输出、时间戳、错误信息、分配的 Worker。

### 数据结构

采用**按状态分桶**的存储结构，每个状态维护独立的哈希表。任务元数据使用 `shared_ptr` 管理，实现零拷贝共享和线程安全。

```
buckets_[PENDING]   → {task_id → TaskMetadataPtr}
buckets_[RUNNING]   → {task_id → TaskMetadataPtr}
buckets_[COMPLETED] → {task_id → TaskMetadataPtr}
buckets_[FAILED]    → {task_id → TaskMetadataPtr}
buckets_[CANCELLED] → {task_id → TaskMetadataPtr}
task_status_        → {task_id → 当前状态}
```

### 查询复杂度

| 接口 | 复杂度 | 说明 |
|------|--------|------|
| get_task | O(1) | 返回 shared_ptr，0.2ns 拷贝 |
| has_tasks_with_status | O(1) | 检查桶是否为空 |
| count_tasks_by_status | O(1) | 桶的大小 |
| get_tasks_by_status | O(k) | 只遍历目标桶 |
| get_task_ids_by_status | O(k) | 只返回 ID |
| get_task_ids_by_worker | O(r) | 按 worker 过滤 RUNNING 任务 |

### 原子复合操作

将常见的多步操作合并为单次锁获取：

- **fail_task**: 更新状态为 FAILED + 设置错误信息 + 记录完成时间
- **assign_task**: 更新状态为 RUNNING + 设置分配的 Worker + 记录开始时间
- **unassign_task**: 更新状态为 PENDING + 清除 Worker 分配

### 自动清理

当 COMPLETED + FAILED 任务数超过阈值（默认 100）时，按完成时间排序，淘汰最老的任务。防止长时间运行时内存无限增长。

---

## TaskScheduler

### 核心职责

将就绪任务匹配到空闲 Worker。

### 调度算法

1. 获取就绪任务列表
2. 获取空闲 Worker 列表
3. 对每个就绪任务，选择一个符合条件的 Worker（能力匹配）
4. 返回匹配结果

**约束**: Worker 同一时刻最多执行一个任务。

---

## 任务调度失败检测

当 `fail_unscheduleable_tasks=1` 时，`schedule_tasks()` 执行两项检查：

**Capability 检查**: 遍历就绪任务，检查是否有 Worker 具备所需能力。如果没有，任务标记为 FAILED。

**Dependency 检查**: 如果没有就绪任务且没有运行中的任务，说明剩余 pending 任务的依赖永远无法满足，标记为 FAILED。

---

## 核心流程

### 任务提交

```
TaskSubmitMessage 到达 Master
  → TaskManager.create_task(...)
  → DependencyGraph.add_task(...)     // 注册依赖 + 构建反向索引
  → 预取依赖数据位置（submit_time prefetch）
  → schedule_tasks()
```

### 数据就绪触发调度

```
WriteRegister 到达 Master
  → DependencyGraph.mark_data_ready(...)  // O(T×D) 反向索引查询
  → DataService.update_remote_idx(...)
  → 更新依赖位置缓存
  → schedule_tasks()
```

### 任务分配（含依赖位置预取）

```
assign_task_to_worker(task_id, worker_id)
  → 构建 TaskAssignMessage
  → 填充依赖数据位置（从缓存 + 直接查询）
  → 发送给 Worker
```

Worker 收到 TaskAssignMessage 后，将依赖位置存入本地缓存。后续读取依赖数据时优先使用缓存位置，避免查询 Master。

---

## 设计决策

| 决策 | 原因 |
|------|------|
| DependencyGraph 反向索引 | mark_data_ready 从 O(P×D) 降到 O(T×D) |
| TaskManager 按状态分桶 | 按状态查询从 O(n) 降到 O(k) |
| shared_ptr 存储元数据 | 零拷贝共享，消除数据竞态 |
| 原子复合操作 | 减少锁获取次数，降低竞争 |
| 自动清理 completed tasks | 防止内存无限增长 |
| 依赖位置预取 | 消除远程读路径的 Master 查询开销 |
