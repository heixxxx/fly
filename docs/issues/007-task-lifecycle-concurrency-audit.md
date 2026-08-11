# Issue 007: task 生命周期并发竞态审计 — 残留问题

## 概述

**日期**: 2026-08-11
**触发原因**: 修复 `submit_task`（Python 调用线程）与 `on_task_complete`（reactor 线程）跨线程竞态导致的 `COMPLETED-MISMATCH` 永久卡死（commit `62b7355`）。修复后对全仓库做同类模式审计，发现多个结构相似的残留问题。
**审计范围**: `master_agent.cpp` 所有跨 `graph_`/`metadata_`/`worker_manager_` 复合操作、各 manager 的隐式覆盖/静默 return 语义、后台线程与 reactor 线程的状态竞争。

**本次已修复**（commit `62b7355` + 防御性 assert/WARN）:
- ✅ `TaskManager::create_task` 重复 id → assert（不再隐式覆盖）
- ✅ `DependencyGraph::add_task` 重复 id → assert（与 create_task 对称）
- ✅ 4 处 task 生命周期复合操作加 `schedule_mutex_` 保护（submit_task / on_task_complete / on_task_failed / on_disconnect）
- ✅ `get_or_create_database`/`register_database` 重复 db_path → WARN
- ✅ `WorkerManager::register_worker` 重注册时旧状态 BUSY → WARN
- ✅ `WorkerManager::register_worker` 3 参数重载递归锁 bug → 删除（死代码）

**以下为未处理的残留问题**，按严重度排序。

---

## 问题汇总

| # | 严重度 | 类型 | 标题 |
|---|--------|------|------|
| 1 | 高 | 并发竞态 | `assign_task_to_worker` 发送/赋值乱序 + scheduler 预占 → worker 永久卡 BUSY |
| 2 | 高 | 并发竞态 | `on_disconnect` snapshot 在 schedule_mutex_ 外 → task 永久孤儿 |
| 3 | 中 | 逻辑顺序 | `DataService::on_write_started` 在重复检测前覆盖 COMPLETE 条目 |
| 4 | 中 | 并发竞态 | backup/merge task 的 `worker_manager_->assign_task` 在 schedule_mutex_ 外 |
| 5 | 低 | 静默覆盖 | `pending_delete_acks_` / `pending_merge_cleanups_` 重复触发重置计数器 |
| 6 | 低 | 静默覆盖 | `TcpConnectionManager` 同 fd 重复注册 → conn_to_fd_ 孤儿条目 |

---

## 问题 1（高）：`assign_task_to_worker` 发送/赋值乱序 + scheduler 预占

### 现象

worker 永久卡 BUSY，`current_task_id_` 指向已完成的 task，`get_idle_workers` 永不返回该 worker → 集群容量慢性耗尽。现有的 `[COMPLETED-MISMATCH]` WARN **检测不到**此 bug（它只比对 graph vs metadata，而两者一致）。

### 定位

- `src/agent/cpp/master_agent.cpp:760-763` — `assign_task_to_worker`:
  ```cpp
  reactor_->send(conn_id, msg);                    // 760: TaskAssign 先发
  metadata_->assign_task(task_id, worker_id);      // 762: 状态后设
  worker_manager_->assign_task(worker_id, task_id); // 763: 无条件覆盖 BUSY
  ```
- `src/agent/cpp/master_agent.cpp:993` — `on_task_complete` 的 `worker_manager_->complete_task(worker_id)` 在 `schedule_mutex_` **之外**。
- `src/task/cpp/task_scheduler.cpp:68` — `schedule_next` 已在 schedule_mutex_ 内调 `manager_->assign_task(W,T)`（设 BUSY）。

### 竞态时序

```
t0 调度线程(持 schedule_mutex_): schedule_next 选 W → assign_task(W,T) [W BUSY]
t1 调度线程: reactor_->send(TaskAssign) — 字节进 socket buffer
t2 worker 极快完成 T → 回 TaskComplete
t3 reactor 线程: on_task_complete → complete_task(W) [W IDLE]  ← schedule_mutex_ 外
t4 reactor 线程: 抢 schedule_mutex_ — 阻塞
t5 调度线程: metadata_->assign_task(T,W) [T RUNNING]
t6 调度线程: worker_manager_->assign_task(W,T) — 覆盖 complete_task 结果 [W BUSY]
t7 调度线程: 释放 schedule_mutex_
t8 reactor 线程继续: graph_->remove_task(T) + update_task_status(T, COMPLETED)
```

**终态分叉**: metadata T=COMPLETED，worker_manager W=BUSY(current_task_id=T)，graph T 在 completed_tasks_。W 永久不可用。

### 修复方向

- 把 `on_task_complete`/`on_task_failed` 的 `worker_manager_->complete_task` 纳入 `schedule_mutex_` 保护段；或
- 去掉 `assign_task_to_worker:763` 的冗余 `worker_manager_->assign_task`（scheduler.cpp:68 已赋值过），并把 `reactor_->send` 移到 metadata/worker_manager 赋值**之后**。

---

## 问题 2（高）：`on_disconnect` snapshot 在 schedule_mutex_ 外 → task 永久孤儿

### 现象

worker 断连瞬间被 scheduler assign 的 task 永久丢失——不在 pending（graph 已 remove）、不在 RUNNING@活 worker（worker 已 DEAD）、不被 `on_disconnect` 恢复（snapshot 漏了）、不被 `persist_pending_tasks` 捞到（它不是 PENDING）。

### 定位

- `src/agent/cpp/master_agent.cpp:1147` — `metadata_->get_task_ids_by_worker(W)` 取 snapshot，在 `schedule_mutex_` **之外**。
- `src/agent/cpp/master_agent.cpp:1156-1191` — 恢复循环在 `schedule_mutex_` 内，但用的是 1147 的旧 snapshot。

### 竞态时序

```
t0 scheduler(持 schedule_mutex_): worker_to_conn_ 检查 W 仍在 → 取 conn_id
t1 on_disconnect: workers_mutex_ 内擦除 W
t2 on_disconnect: snapshot = get_task_ids_by_worker(W) → 不含 T（T 尚未 assign）
t3 scheduler: metadata_->assign_task(T, W) → T 进 RUNNING@W 桶
t4 scheduler: worker_manager_->assign_task(W, T)
t5 on_disconnect 进 schedule_mutex_ 块：snapshot 为空，恢复循环不处理 T
```

**终态**: T 标 RUNNING@W（W 已 DEAD），graph 不含 T，无人恢复。

### 修复方向

把 `get_task_ids_by_worker` snapshot 移入 `schedule_mutex_` 块，或在持锁块内重新核对每个 RUNNING task 的 assigned_worker。

---

## 问题 3（中）：`DataService::on_write_started` 在重复检测前覆盖 COMPLETE 条目

### 现象

重复写入路径上，对象已有的 COMPLETE local_idx 条目被 INCOMPLETE 覆盖后又被 `on_write_failed` 擦除 → 等待该对象的读取者掉落到 TIER2/远程。

### 定位

- `src/storage/cpp/database.cpp:153` — `on_write_started` **无条件**调用，在 `register_write`（:155 重复检测）**之前**:
  ```cpp
  DataService::instance()->on_write_started(db_path_, full);  // 153: 先覆盖
  auto [reg_error, ...] = ...register_write(...);             // 155: 后检测
  if (reg_error_type == WRITE_DUPLICATE_SKIPPED) {
      DataService::instance()->on_write_failed(...);          // 159: 再擦除
  ```
- `src/storage/cpp/data_service.cpp:307` — `on_write_started`: `local_idx_[db_path].objects_[short_name] = info;` 用新 INCOMPLETE 覆盖任何已存在的 COMPLETE 条目（丢弃 entries_ 向量）。

### 修复方向

把 `on_write_started` 移到 `register_write` 成功之后；或让 `on_write_started` 拒绝覆盖已存在的 COMPLETE 条目。

---

## 问题 4（中）：backup/merge task 的 `worker_manager_->assign_task` 在 schedule_mutex_ 外

### 定位

- `src/agent/cpp/master_agent.cpp:2201` — `on_backup_request`: `worker_manager_->assign_task(backup_W, T_backup)` 无 `schedule_mutex_`。
- `src/agent/cpp/master_agent.cpp:2296` — `send_merge_task`: `worker_manager_->assign_task(target_W, T_merge)` 无 `schedule_mutex_`。

### 风险

scheduler（attr-check/watchdog 线程）刚把 W 设 BUSY with T_real，backup/merge 的 assign_task 覆盖 current_task_id_。由于 backup/merge task 不进 metadata_/graph_（is_internal_=true），分叉只在 worker_manager_ 内。`on_task_complete` 的 complete_task(W) 会清理（不分 internal/external），多数自愈。但 worker 收到两个 TaskAssign 时行为未定义，可能造成 merge 屏障 `wait_merge_tasks_complete` 超时。

### 修复方向

backup/merge 的 assign_task 纳入 `schedule_mutex_`，或复用 `assign_task_to_worker`（它已在锁内）。

---

## 问题 5（低）：`pending_delete_acks_` / `pending_merge_cleanups_` 重复触发重置计数器

### 定位

- `src/agent/cpp/master_agent.cpp:2406` — `pending_delete_acks_[ack_key] = PendingDeleteData{}`: 若同一 ack_key (`db_path:source_worker_id`) 二次触发且首次未完成，首次的 received/expected 计数被重置 → ack 丢失。
- `src/agent/cpp/master_agent.cpp:2520` — `pending_merge_cleanups_[db_path] = PendingMergeCleanup{...}`: 同模式。

### 修复方向

插入前检查是否已存在 pending 条目，存在则 WARN 或等待首次完成。

---

## 问题 6（低）：`TcpConnectionManager` 同 fd 重复注册 → conn_to_fd_ 孤儿

### 定位

- `src/network/cpp/tcp_connection_manager.cpp:302-303` — `register_connection`:
  ```cpp
  conn_to_fd_[conn_id] = fd;   // conn_id 单调递增，安全
  fd_to_conn_[fd] = conn_id;   // 同 fd 二次注册则覆盖，旧 conn_to_fd_[old] 变孤儿
  ```

### 风险

双重 accept / fd 重用时，旧 conn_id 的 conn_to_fd_ 条目泄露，永不被清理。严重度低（accept 循环通常正确）。

### 修复方向

注册 fd 前检查 fd_to_conn_ 是否已有该 fd，有则 WARN + 先 unregister 旧条目。

---

## 审计中已确认安全（无需处理）的模式

以下经审查为合理语义或已有防御，**记录但不处理**：

| 模式 | 位置 | 理由 |
|------|------|------|
| TaskManager 各方法 "not found 静默 return" | task_manager.cpp:93/119/129 等 | task 在合法清理（maybe_cleanup_completed）中会消失，return 正确 |
| WorkerManager 各方法 "not found 静默 return" | worker_manager.cpp:47/56/64 等 | worker 死亡后延迟消息到达，return 正确 |
| `write_provenance_` 覆盖 | master_agent.cpp:1418 | 已有 do_write_register 的 WRITE_PROVENANCE_MISMATCH 防御 |
| `recorded_workers_.insert` | master_agent.cpp:963 | 已有查找后插入去重 |
| `ObjectCache::put_*` 覆盖 | object_cache.h | 缓存更新语义，合理 |
| `Database::var_store_` 覆盖 | database.cpp | var 设计为可变，合理 |
| `merge_task_states_` 覆盖 | master_agent.cpp:2280 | key 来自单调递增 remote_task_counter_，不会重复 |
| `pending_frozen_dbs_` 覆盖 | master_agent.cpp:2052 | 已有 is_db_frozen 上游防御 |
| heartbeat_check_loop 的 update_worker_status(DEAD) 锁外 | master_agent.cpp:770 | 下轮 heartbeat 重设 DEAD，靠 on_disconnect 收尾，自愈 |
| on_heartbeat DEAD→IDLE 复活 | master_agent.cpp:923 | worker 发心跳说明活着，瞬时自愈 |
| on_data_query_dispatch / TIER3 的 graph+metadata 只读 | master_agent.cpp:1347 | 瞬时不一致，自愈 |

---

## 相关 commit

- `62b7355` — 根治 submit_task 与 on_task_complete 跨线程竞态（本次修复的主体）
- `0423f67` — runqa 卡死诊断工具链 + write_buffers 健壮性修复（本次审计的基线）
