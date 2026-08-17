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
| WorkerAgentContext | `common/cpp/worker_context.h`（位于 common 模块，agent 与 storage 共用以避免循环依赖） | 写入跟踪上下文 |

---

## MasterAgent

### 核心职责

Master 节点管理，负责 Worker 注册、任务调度、数据就绪通知、故障恢复和优雅关机。

### 核心数据结构

```
conn_to_worker_:  conn_id → worker_id      // 连接双向映射
worker_to_conn_:  worker_id → conn_id
db_instances_:    db_path → shared_ptr<Database>   # 以 db_path 为键（ADR 0002，无 db_id）
frozen_dbs_:            set<db_path>               # 已确认冻结（task 完成后）
pending_frozen_dbs_:    map<db_path, task_id>      # 非 stream 模式待确认（task 内声明，task 完成提交/失败回滚）
```

> **task 提交字段单一来源**：task 的完整不变字段（name/module/args/inputs/outputs/caps/timeout/priority/write_context_hash/vars）统一存储在 `TaskMetadata.submission_`（`TaskSubmissionSpec`，见 `task/cpp/task_manager.h`）。master 不再单独维护 module/args/vars 的并行 map —— 所有读取经 `metadata_->get_task(id)->submission_`（shared_ptr 快照，线程安全）。

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
current_writes_:     当前写入记录 CMVector<WriteRecord>（full_name + 压缩字节数，单一容器保证同生命周期）
prefetched_locations_: 预取的依赖数据位置
```

### 启动流程

```
WorkerAgent.start()
  1. 创建 Transport + Data Server
  2. 连接 Master
  3. 创建 Reactor，注册 message handlers
  4. 启动 reactor_thread_、heartbeat_thread_ 和 register_watchdog_thread_
  5. 发送 RegisterMessage
```

### 注册守望（register_watchdog_loop，P3-23 兜底）

- **职责分层**：连接级丢失（RegisterAck 丢失的真实主因）由
  `on_disconnect → reconnect_loop` 事件驱动恢复（毫秒级，无超时参与）；
  守望只覆盖「master 活着但 REGISTER/RegisterAck 被应用层吞掉」——
  EOF 不会到来的窄场景。
- **事件驱动**：cv 等 RegisterAck（`on_register_ack` 持锁 notify，
  注册成功即刻退出，零空转；`initiate_shutdown` 同步唤醒）。
- **退避重发**：超时指数退避（`worker_register_ack_retry_initial_ms`
  默认 500ms，×2 上限 30s）；master 对同 conn 重发走正常注册路径，
  幂等安全；`reconnecting_` 期间让位 reconnect_loop。
- **join 顺序**：do_cleanup 按 reconnect → watchdog → heartbeat →
  reactor 依赖序回收。

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
Master.stop() — 三阶段流程
  Phase 1: 等待所有 running tasks 完成 (workers 仍然活跃)
    → drain_cv_ 等待 running_count == 0 (最多 30s)
    → on_task_complete / on_task_failed 通知 drain_cv_

  Phase 2: 发送 shutdown 给所有 workers
    → 广播 ShutdownMessage

  Phase 3: 等待 workers 断开连接
    → workers_drained_cv_ 等待 worker_to_conn_ 为空 (最多 10s)
    → on_disconnect 通知 workers_drained_cv_

  → persist_pending_tasks()
  → 停止所有线程和组件

draining 模式下 on_disconnect:
  → 标记 running tasks 为 FAILED
  → 通知 drain_cv_ 和 workers_drained_cv_

Worker.on_shutdown()
  → initiate_shutdown()
  → 停止 Data Server 和 Reactor
  → do_cleanup()

自动 stop():
  → 脚本模式: 用户脚本执行完毕后自动调用 stop()
  → 交互模式: 用户退出时通过 atexit 调用 stop()
```

### Freeze 流程

```
Worker 任务执行中调用 db.freeze():
  → Database::freeze() (本地落盘 + 标记)
  → WorkerAgentContext::notify_freeze(db_id)
  → request_database_freeze: 同步等 ack（发送 DatabaseFreezeNotification 带 task_id）

Master 收到 (on_database_freeze_request):
  冲突检查：db 已 frozen/pending → 回 ack DB_ALREADY_FROZEN（fail-fast）
  stream 模式（默认）：即时 frozen_dbs_.insert + 本地 freeze + 广播 → 回 ack success
  非 stream 模式：pending_frozen_dbs_[db_id] = task_id（不广播、不本地 freeze）→ 回 ack success

  task 完成（on_task_complete）→ commit_pending_frozen(task_id):
    pending 中 task_id 匹配的项 → frozen_dbs_ + 本地 freeze + 广播
  task 失败/崩溃（on_task_failed / on_disconnect）→ rollback_pending_frozen(task_id):
    按 task_id 清除 pending（防永久死锁）

Worker 收到广播 (on_database_freeze_notification):
  → databases_[db_id]->freeze() (本地冻结，幂等去重)
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
