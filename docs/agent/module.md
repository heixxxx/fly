# Agent 模块 — Agent 层

## 模块概述

**位置**: `src/agent/`

Agent 层是框架的最高 C++ 层，封装 Master 和 Worker 的完整业务逻辑，包括消息处理、任务生命周期管理、数据传输协调和 Python 任务执行。

---

## 核心组件

| 组件 | 文件 | 职责 |
|------|------|------|
| MasterAgent | `cpp/master_agent.h/cpp` | Master 节点管理 |
| WorkerAgent | `cpp/worker_agent.h/cpp` | Worker 节点执行 |
| TaskExecutor | `cpp/task_executor.h/cpp` | 任务执行器 |
| WorkerAgentContext | `cpp/worker_context.h` | 写入跟踪上下文 |

---

## MasterAgent

### 核心职责

Master 节点管理，负责 Worker 注册、任务调度、数据就绪通知、故障恢复和优雅关机。

### 核心数据结构

```
conn_to_worker_:  conn_id → worker_id      // 连接双向映射
worker_to_conn_:  worker_id → conn_id
task_modules_:    task_id → module_name
task_args_:       task_id → args[]
db_registry_:     db_id → {base_path → data_path}
db_instances_:    db_id → shared_ptr<Database>
frozen_dbs_:      set<db_id>
```

### 启动流程

```
MasterAgent.start()
  1. 创建 Transport + Reactor
  2. 注册所有 message handlers
  3. 启动 reactor_thread_ 和 heartbeat_check_thread_
```

### 任务调度流程

```
schedule_tasks()
  → if draining_: return
  → ready_tasks = graph_->get_ready_tasks()
  → idle_workers = worker_manager_->get_idle_workers()
  → for each ready_task:
      → 检查 required_capabilities
      → 无匹配 Worker → persist_failed_task → FAILED
      → 有匹配 Worker → assign_task_to_worker
  → schedule_all_available()
```

### 任务分配（含依赖位置预取）

```
assign_task_to_worker(task_id, worker_id)
  → 构建 TaskAssignMessage
  → 从缓存 + 直接查询填充依赖数据位置
  → 发送给 Worker
```

### 消息处理

**on_task_complete**:
```
→ Worker → IDLE
→ for written_object: mark_data_ready + update_remote_idx
→ for frozen_db: broadcast freeze notification
→ remove_task + update_status(COMPLETED)
→ schedule_tasks()
```

**on_task_failed**:
```
→ Worker → IDLE
→ update_status(FAILED)
→ if fatal error type: set fatal_error_ flag
→ schedule_tasks()
```

**on_write_register**:
```
→ mark_data_ready (触发下游任务就绪)
→ update_remote_idx (更新数据位置)
→ update_dependency_location_cache (更新依赖位置缓存)
→ schedule_tasks()
```

**on_disconnect**:
```
→ 移除 Worker 连接映射
→ 恢复该 Worker 的 RUNNING 任务 → PENDING
→ schedule_tasks()
```

---

## WorkerAgent

### 核心职责

Worker 节点执行，负责任务接收、执行、写入跟踪和远程数据读取。

### 核心数据结构

```
master_conn_:        到 Master 的连接 ID
task_queue_:         queue<PendingTask> (Reactor→Main 传递)
databases_:          db_id → shared_ptr<Database>
current_task_id_:    当前任务 ID
current_writes_:     当前写入记录
prefetched_locations_: 预取的依赖数据位置
```

### 启动流程

```
WorkerAgent.start()
  1. 创建 Transport + Data Server
  2. 连接 Master
  3. 创建 Reactor，注册 message handlers
  4. 启动 reactor_thread_ 和 heartbeat_thread_
  5. 发送 RegisterMessage
```

### 任务执行流程

```
ReactorThread:
  → on_task_assign(TaskAssignMessage)
    → 存储预取的依赖位置
    → task_queue_.push(task)

MainThread (poll_task 循环):
  → poll_task()
    → begin_task(task_id)  // 设置回调
    → executor_->execute(task_id, name, module, args)
    → end_task(task_id)
    → 发送 TaskCompleteMessage 或 TaskFailedMessage
```

### 远程数据读取

```
request_remote_data(object_name)
  → 检查 prefetched_locations_ (预取命中)
    → 命中 → 直接从目标 Worker 读取
  → 未命中 → 查询 Master 获取位置
    → 从目标 Worker 读取
  → 缓存 remote_idx
```

---

## WorkerAgentContext

### 核心职责

写入跟踪上下文，通过 `std::function` 回调实现 C++ 层与 Python 层的解耦。

### 回调机制

```
任务开始:
  WorkerAgent.begin_task(task_id)
    → set_record_write_func(lambda)
    → set_register_func(lambda)
    → set_freeze_func(lambda)
    → set_notify_removed_func(lambda)

写入触发:
  Database.write_object()
    → WorkerAgentContext::record_write()
      → lambda → WorkerAgent::record_write()

任务结束:
  WorkerAgent.end_task(task_id)
    → WorkerAgentContext::clear()
    → return current_writes_
```

---

## 核心流程

### 跨 Worker 数据读取

```
Worker A: db.read_object("key")

Layer 1: DataService.try_read_local("key")
  → 找到且 COMPLETE → 返回
  → 未找到 → Layer 2

Layer 2: DataService.lookup_remote_idx("key")
  → 有缓存 → DataClient 直连 Worker B
  → 失败 → Layer 3

Layer 3: request_remote_data("key")
  → 检查预取位置 → 查询 Master → DataClient 读取
  → 缓存 remote_idx
```

### 优雅关机流程

```
Master.stop()
  1. draining_ = true (阻止新调度)
  2. 广播 ShutdownMessage 给所有 Worker
  3. 等待 running tasks 清空 (最多 10s)
  4. persist_pending_tasks() (持久化未完成任务)
  5. 停止所有线程和组件

Worker.on_shutdown()
  → initiate_shutdown()
  → 停止 Data Server 和 Reactor
  → do_cleanup()
```

### Freeze 流程

```
Worker 任务执行中调用 db.freeze():
  → Database::freeze() (本地)
  → WorkerAgentContext::notify_freeze(db_id)
  → 发送 DatabaseFreezeNotification 给 Master

Master 收到:
  → frozen_dbs_.insert(db_id)
  → broadcast 给所有 Worker

Worker 收到广播:
  → databases_[db_id]->freeze() (本地冻结)
```

### Worker 断连恢复

```
Master.on_disconnect(conn_id):
  → 移除 Worker 连接映射
  → 恢复该 Worker 的 RUNNING 任务:
    → graph_->remove_task → graph_->add_task (重新入队)
    → update_task_status(PENDING)
  → schedule_tasks()
```

### load_db 恢复流程

```
Phase 1: 读取 _DB_META (db_id, workers)
Phase 2: Master 自身恢复 (创建 Database 实例)
Phase 3: 按 hostname 分配 Worker (复用或新建)
Phase 4: 下发 idx 加载命令给所有 Worker
Phase 5: 重建 remote_idx
```

---

## 设计决策

| 决策 | 原因 |
|------|------|
| Master + Worker 共用 Reactor 模式 | 统一事件驱动，handler 无锁 |
| std::function + lambda 回调 | WorkerAgentContext 不依赖 Agent 头文件，保持模块独立 |
| workers_mutex_ 保护连接映射 | reactor 线程和主线程并发访问 |
| stop() 幂等 + drain 语义 | 允许重复调用；drain 期间仍接受 task 但不调度 |
| DataClient 独立 TCP | Worker 读数据不走主 Reactor，避免多线程读冲突 |
| 依赖位置预取 | 消除远程读路径的 Master 查询开销 |
