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
| PeerRpcServer | `cpp/peer_rpc_server.h/cpp` | worker 间业务 RPC（独立业务端口 + 独立线程，与 reactor/DataServer 隔离）：单帧请求-响应 + 流式大 payload 管线（PeerStreamWriter/PeerStreamReader，压缩块流经 DATA_CHUNK 帧承载，见 [rpc-stream-pipeline.md](../rpc-stream-pipeline.md)） |
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

## RunMetricsCollector（run_metrics.h/cpp，master 专属）

运行时指标采集与 RunSummary 汇总（设计与裁定记录：`docs/run-summary-metrics-design.md`）。

**采集模型（骨架 + 最近邻合成，全部推迟到退出时合成）**：
- tick 线程每 `metrics_tick_seconds`（默认 10s）采 master 自身 RSS 成骨架
  {steady rel_ms（渲染时间轴）, epoch_ms（对齐域）, rss}；start 立即首 tick。
- worker 侧 RSS/负载采样已迁出心跳：worker 的 monitor 线程（`monitor_thread_`/
  `monitor_report_loop`，与心跳完全解耦）按 `monitor_sample_interval_ms` 周期
  采样（task 执行窗口内加密至 `monitor_exec_sample_interval_ms`；assign/执行
  起止/断连等事件点经节流后入缓冲），每 `monitor_report_interval_ms` 成组
  `MONITOR_SAMPLE` 消息上报（样本含 **真实采样时刻** = unix epoch 毫秒、RSS、
  proc/host CPU、host 内存/loadavg、网络累计字节）。发送失败/断连窗口的样本
  在 worker 侧缓冲不丢（`pending_samples_`），下次成组补发；master 侧
  `on_monitor_sample`（master_agent.h）喂 RunMetricsCollector 并经 MetricsDb
  落 worker_samples 表（主键 (worker_id, epoch_ms)，补发幂等）。心跳仅保活
  （HeartbeatMessage 无 RSS 字段）。
- 退出时合成：每骨架 tick 取各 worker ≤ 该 epoch 时刻的最后样本
  （`std::upper_bound` 最近邻；首样本前=未上线不计；`on_worker_dead` 记
  判死 epoch，死后无新样本不计，复活样本自然重新生效）。

**db 生命周期钩子**：`record_db_created`（register_database +
get_or_create_database 双入口，幂等首见）/ `record_db_frozen`
（frozen_dbs_.insert 成功三处 + 锁外 du -sk 统计磁盘终值）/ 
`record_db_paths_changed`（merge set_paths 作废统计值，退出补测）。

**输出（用户裁定形态）**：stop_impl 尾部直写 `{log_dir}/runtime.summary`
（时长 + 10 等份分阶段集群内存 total_avg/total_peak；样本 <10 退化单阶段）
与 `{log_dir}/db.summary`（disk/创建时长/冻结状态/窗口内 mem avg/peak）——
**独立 ofstream 直写不经 Logger**（退出期 Logger INFO 有吞行前科）；用户
日志只打一行 `FLY::0002`（总耗时 + 文件地址）。

**机器信息定时日志**：main.cpp `resource_monitor_loop` 每
`machine_info_interval_seconds`（默认 10，0=关）打 INFO 级 MachineInfo
（proc_rss+peak / host free/available/total / cpu% / loadavg），master 与
worker 进程共用。数值 API 在 `core/cpp/system_info.h`。

---

## WorkerAgent

### 核心职责

Worker 节点执行，负责任务接收、执行、写入跟踪和远程数据读取。

### 核心数据结构

```
master_conn_:        到 Master 的连接 ID
task_queue_:         queue<PendingTask> (Reactor→Main 传递)
databases_:          db_path → shared_ptr<Database>   # 以 db_path 为键（无 db_id）
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
  4. 启动 reactor_thread_、heartbeat_thread_、monitor_thread_（负载采样
     上报，与心跳解耦）、register_watchdog_thread_ 和 probe_thread_
     （带宽探测，flag 控制）
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

### 任务执行流程（执行上提，5016527）

```
ReactorThread (lane):
  → on_task_assign(TaskAssignMessage)
    → 存储预取的依赖位置
    → task_queue_.push(task)

Python 主线程 (Worker.poll_loop, agent.py):
  → task = agent.take_task(timeout_ms)
      // C++ 侧 GIL 释放状态下等待/出队——空等不压制同进程 Python 线程；
      // internal task（merge/backup，纯 C++）就地消化后继续等下一个；
      // 普通 task 出队时做 begin 钩子（begin_task/vars 暂存/资源跟踪开始）
  → executor(task_id, name, module, args)
      // create_executor 产物，在本线程直接调用（executor 异常兜底为
      // status=1 result，保证 finish 必被调用）
  → agent.finish_task(task, result)
      // 纯 C++ 收尾：资源跟踪终点 / end_task / IO 明细上报 / 写段提交或
      // 回滚 / TaskComplete·TaskFailed 上报 / outstanding 减计
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

### 关机流程（双通道语义，2026-08-18 用户裁定重构）

关闭分两条语义相反的通道，统一入口 `stop_impl(fast, reason)`：

```
Master.stop() — 正常收尾（脚本执行完毕自动调用）
  Phase 1: 等待所有 RUNNING task 完成（workers 仍活跃）
    → drain_cv_ 等待 running_count == 0（30s 硬超时已废除：长 task 等不到
      只是延迟处死且超时后 RUNNING 无留痕；兜底=心跳判死链+断连宽限超时+
      fast_exit 打断+drain_timeout_seconds=600 长上限——覆盖「worker 活着但
      complete 丢失」的僵死路径，超时转 fast 路径 fail 善后留痕，0=无限）
    → on_task_complete / on_task_failed / on_disconnect 通知 drain_cv_
      （持 drain_mutex_ notify——无锁 notify 有 lost wakeup 窗口，945e213）
  Phase 1.5: message summary 屏障（诊断输出，30s 容错）
  Phase 2: 广播 ShutdownMessage（worker 优雅退：flush coverage + WBQ drain）
  Phase 3: 等 worker 断连（10s 上界）
  → persist_pending_tasks() → 停止所有线程和组件

Master.fast_exit(reason) — 快速退出（SIGTERM / graceful_exit 致命错误）
  Phase 0: 全部 RUNNING task 立即 fail 善后
    → fail_task + graph remove + failed record 持久化（磁盘坏时 persist 失败
      仅 WARN 不阻塞退出）
  Phase 2: 广播 StopNowMessage（新消息 STOP_NOW=58）
    → worker 收到即 kill(getpid(), SIGKILL)——进程级自杀，不依赖 master 知
      pid/句柄（bsub/ssh 跨机 worker 同样生效）；coverage/WBQ flush 丢失是该
      通道接受的代价（master 侧已留痕 fail record）
  Phase 3: 等 worker 断连（2s 短宽限；SIGKILL→OS 关 fd 亚秒，2s 兜僵死）
  → persist_pending_tasks() → 停止所有线程和组件

并发协调：fast_exit 到达时 stop() 已在 drain → 置 fast_exit_requested_ +
持锁 notify 打断等待 → drain 转快速路径。幂等由 draining_ 首位置位保证。
触发链：SIGTERM → 心跳线程（≤5s）→ trigger_graceful_shutdown → 独立线程
fast_exit；write-back 落盘失败 → graceful_exit callback → fast_exit。

draining 模式下 on_disconnect:
  → 标记 running tasks 为 FAILED
  → 通知 drain_cv_ 和 workers_drained_cv_

Worker.on_shutdown()（收到 ShutdownMessage，优雅路径）
  → initiate_shutdown()：主动 close master 连接（master_conn_.exchange(0) +
    reactor_->close_connection——Reactor::stop 只停循环不关 fd，不主动关会让
    master 断连等待拖满 deadline）
  → 停止 Data Server 和 Reactor → do_cleanup()（~Database drain WBQ）

Worker.on_stop_now()（收到 StopNowMessage，快速路径）
  → kill(getpid(), SIGKILL)（testonly 编译可被 stop_now_hook_for_testing_
    拦截——库对象单测观察语义不杀测试进程；hook 分支也主动 close 连接让
    master 断连等待立即通过）

自动 stop():
  → 脚本模式: 用户脚本执行完毕后自动调用 stop()
  → 交互模式: 用户退出时通过 atexit 调用 stop()
```

#### Worker 启动时序（半初始化竞争防护，2026-08-19 用户裁定）

```
WorkerAgent::start() 的关键顺序约束：
  1. transport listen → connect master → reactor 构造 + 全部 handler 注册
  2. reactor 线程 spawn
  3. heartbeat / register_watchdog / bandwidth_probe 三线程 spawn（flag 置 true）
  4. shutdown_state_mutex_ 锁内：秒拒检查 + running_ = true 提交
  5. send_register_message()   ← 注册消息必须在全部初始化完成后才发

原因：dup ack 在注册后毫秒级可达（master 秒拒 + lane 处理），若注册早于
初始化完成，ack 作用于半初始化的 worker（线程未 spawn、running_ 未置位），
initiate_shutdown 的标志写被 start 尾段覆盖 → is_running 恒真 + 幂等闸门
锁死 join（4 实例压测实测 300s 卡死）。shutdown_state_mutex_ 使 start 尾段
与 initiate_shutdown 的生命周期标志写互斥（消除 TOCTOU 残窗）。

poll_task 约束：executor 未注入且队首为普通 task 时不弹出（弹出后无执行
器只会静默丢弃，master 侧 RUNNING 永不归零）；internal task（merge/backup）
不依赖 executor，storage_only worker 也执行。
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
Phase 1: 读取 _DB_META（JSON，storage/py/db_meta.py：db_path + workers[]
         写者登记 worker_id/writer_id/hostname/ip_address）
Phase 2: Master 自身恢复 (创建 Database 实例)
Phase 3: 按 hostname 分配 Worker (复用或新建)
Phase 4: 下发 idx 加载命令给所有 Worker (db_path + writer_ids)
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
