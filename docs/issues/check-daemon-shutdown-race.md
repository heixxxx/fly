# BUG: check daemon 写 sol 后 worker 被 master SHUTDOWN 中断，数据未完整落盘

> 发现日期：2026-08-05
> 严重度：P0（数据完整性）
> 影响范围：ras_graph_daemon v2（常驻 daemon 模式），OpenMP 启用时必现，非 OpenMP 偶现
> 状态：**未修复——需架构层面解决**

## 一、现象

`solve_ras_graph_v2` 收敛后，`_wait_solution` 读 `__rasg__converged` 时报 `EOFError: Ran out of input`（pickle 反序列化失败，数据不完整）。

n=1000 非 OpenMP 下偶现（~50% 概率），OpenMP（2 线程）下必现。

## 二、精确时序（n=1000 OpenMP 实测日志）

```
check worker1（task_id=9 = check_daemon_task）：
  16.351  WriteRegister sent: __rasg__converged（最后一个 write_object）
  16.360  [CHECK] converged=True at step=7（while break）
  16.362  [CHECK] exited at step=7（task 函数返回）
  16.403  TaskComplete sent: task_id=9, outputs=4
  16.414  Worker shutdown initiated: master shutdown message（收到 SHUTDOWN）

master（Python 主线程）：
  16.40x  _wait_solution 轮询到 __rasg__sol → 立即返回
  16.413  solve_ras_graph_v2 返回 → 调用方 master.stop()
  16.413  MasterAgent stop() → drain → SHUTDOWN 发给所有 worker
```

## 三、根因（架构级）

**master Python 主线程**的调用栈：

```python
# 测试脚本（master 主线程）
result = solve_ras_graph_v2(db, ...)  # 阻塞调用
#   ↓ 内部：
#   1. launch_local_workers
#   2. _coord_prebuild_pipeline
#   3. check_daemon_task + compute_daemon_task（提交 task，非阻塞）
#   4. _wait_solution → 轮询 ds.has_local_object(sol) → 读到后立即返回 dict
#   ↓ 返回 result
master.stop()  # 立即调用
#   ↓ MasterAgent::stop() → drain → SHUTDOWN 所有 worker
```

**check daemon task 的写入时序**：

```python
# check_daemon_task 内（worker Python 主线程）
db.write_object("__rasg__sol", x_global)          # ← save_to_db=True，走 commit_write
db.write_object("__rasg__iters", step + 1)        # ← save_to_db=True
db.write_object("__rasg__converged", all_converged)  # ← save_to_db=True
# ↓ while break → task 函数返回
# ↓ executor 后处理 → TaskComplete 发给 master
```

`write_object(save_to_db=True)` 的内部路径（`database.cpp:commit_write`）：

1. **ObjectCache put_low**（立即可见，同步）
2. **register_write**（同步 RPC 等 master ACK，master 标记 data ready）
3. **enqueue WriteBackQueue**（异步落盘）

**问题**：`_wait_solution` 轮询到 `sol` 在 ObjectCache 可见后立即返回。但 `iters` / `converged` 可能还在 `register_write` 的同步 RPC 中（或还没开始写）。

`_wait_solution` 的返回值 dict 中读 `converged` 时，`converged` 可能：
- 尚未被 check 写入（register_write 未完成）
- 已注册 master 但 worker 侧 ObjectCache 还没有（TIER1 miss → TIER2 读到不完整数据）
- 被后续 master.stop() → SHUTDOWN → worker cleanup 中断

**关键竞态窗口**：从 `_wait_solution` 读到 sol（ObjectCache）到 `master.stop()` 发 SHUTDOWN，只有 ~10ms。在这 10ms 内，check 的 `iters` / `converged` 的 WriteBackQueue 可能还没完成落盘。

## 四、为什么 master.stop() 会被过早调用

`_wait_solution` 的语义是"等 sol 就绪"。它在 ObjectCache 看到 sol 后立即返回。但 sol 只是 3 个输出对象的第 1 个——iters 和 converged 还没写完。

**核心设计缺陷**：`_wait_solution` 的"完成"判定与 check daemon 的"输出完成"之间没有同步原语。check 写 sol 后没有"全部输出完成"的信号。

## 五、为什么不应该是 worker 自行退出

按 fly 架构设计，worker 的生命周期完全由 master 控制：
- master 发 SHUTDOWN → worker 收到 → cleanup → 退出
- worker 不应该主动退出（除非进程崩溃）

**当前实际行为**：check daemon task 的 while 循环 break 后，task 函数返回 → executor 发 TaskComplete → master 收到 → 此时 master 主线程的 `_wait_solution` 读到 sol 并返回 → 调用方 `master.stop()` → SHUTDOWN。

**worker 没有"主动退出"**——它确实是在收到 master 的 SHUTDOWN 后才退出的。问题是 **master 的 SHUTDOWN 发得太早**（sol 写了但 iters/converged 还没写完）。

## 六、复现条件

1. 使用 `solve_ras_graph_v2`（常驻 daemon 模式）
2. check daemon 收敛后写 `__rasg__sol` / `__rasg__iters` / `__rasg__converged`（3 个 `save_to_db=True` 的 write_object）
3. `_wait_solution` 轮询到 `sol` 后立即返回
4. 调用方 `master.stop()` 在 `iters` / `converged` 落盘前发 SHUTDOWN

**必现条件**：OpenMP 启用（`solver_openmp_threads > 0`），因为 OpenMP 让 LDLT 分解更快，整体时序更紧凑，竞态窗口更大。

**偶现条件**：非 OpenMP 模式，取决于 CPU 调度（WriteBackQueue 是否在 SHUTDOWN 前完成）。

## 七、可能的修复方向（架构层面）

### 方向 A：check daemon 写完所有输出后发"完成"信号
check 写完 sol/iters/converged 后，**不退出 while 循环**，而是阻塞等待 master 的"确认收到"信号。`_wait_solution` 读到所有 3 个对象后才返回。

### 方向 B：_wait_solution 等所有输出对象
`_wait_solution` 轮询 `sol` + `iters` + `converged` 三个对象全部就绪后才返回。这是当前 `_wait_solution` 加重试的 hack 的正确版本——但仍有竞态（master.stop 可能在 WriteBackQueue 落盘前发出）。

### 方向 C：master.stop() 前 drain 所有 worker 的 WriteBackQueue
`master.stop()` → drain phase 中，不仅等 worker 断开，还确保所有 worker 的 WriteBackQueue 已 drain。当前 `do_cleanup` 中 `databases_.clear()` → `~Database()` → `drain_write_back()` 应该能等到——但如果 WriteBackQueue 被 `stop_write_back()` 提前停止了（如 `DataService::reset()`），就等不到。

### 方向 D（最根本）：solve_ras_graph_v2 不应在主线程同步等待
`solve_ras_graph_v2` 改为异步 API（返回 future / callback），不阻塞 master 主线程。master 主线程不被阻塞，可以正常处理 reactor 消息（包括 on_task_complete 的 WriteBackQueue drain）。但这改变了 API 语义。

## 八、相关代码

- `src/solver/py/ras_graph_daemon.py`：`_wait_solution`（轮询读 sol）、`check_daemon_task`（写 sol/iters/converged）
- `src/agent/cpp/master_agent.cpp`：`MasterAgent::stop()`（drain → SHUTDOWN）
- `src/agent/cpp/worker_agent.cpp`：`do_cleanup()`（databases_.clear → ~Database → drain_write_back）
- `src/storage/cpp/database.cpp`：`commit_write`（ObjectCache put + register_write + enqueue WriteBackQueue）
- `src/storage/cpp/write_back_queue.cpp`：异步落盘

## 九、当前 workaround

`_wait_solution` 加 5 次重试（每次间隔 0.5s），读 `converged` 失败时重试。但这只是掩盖问题，不保证数据完整性（WriteBackQueue 可能在重试期间被 SHUTDOWN 中断）。

真正的修复应在架构层面确保：**master 在确认所有 worker 的异步写回完成之前，不发 SHUTDOWN**。
