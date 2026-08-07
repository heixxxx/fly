# BUG: check daemon 写 sol 后 master 不处理 worker 消息，导致数据竞态/超时

> 发现日期：2026-08-05
> 严重度：P0（数据完整性 / 稳定性）
> 影响范围：ras_graph_daemon v2（常驻 daemon 模式）
> 状态：**EOFError 竞态已修复（Part 1-2）；并发 hang 为 pre-existing 独立问题（见末尾"并发 hang 调查"）**

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

---

## 修复记录（2026-08-06）

### 代码核对后的根因修正

读完所有相关代码后，发现原文档部分判断不准确：

1. **EOFError 真正根因 ≠ "数据被截断"**：`read_object_compressed`（`database.cpp:317`）
   全 tier miss 时返回 `nullptr`，`_read_decompressed`（`storage_export.cpp:159`）翻译
   成空 bytes，Python `pickle.loads(b'')` → EOFError。机制是 master 还没收到
   `__rasg__iters`/`__rasg__converged` 的 WriteRegister ACK（reactor 单线程串行处理）。

2. **方向 C 基于的错误前提**：`~Database()`（`database.cpp:85-89`）**会调
   `drain_write_back`**，正常 stop 路径下 WBQ 会 drain，不丢数据。

3. **SIGTERM 路径已被覆盖**：`fly/main.py:157-160` 的 `_sigterm_handler` 把 SIGTERM
   转 `SystemExit` → `_cleanup()`（`fly/main.py:54-61`）**已显式 `drain_write_back`**。

4. **同样的 BUG 在 v1 solver 已被修过**：`ras.py:129-145` 的注释精确描述了同一根因，
   方案就是"等最后一个写的对象 `__ras__ok`"。v2 重写时丢了这个 lesson。

### 已实施修复

| 改动 | 文件 | 说明 |
|------|------|------|
| Part 1 | `src/solver/py/ras_graph_daemon.py` | `_wait_solution` 轮询 `__rasg__converged`（check 最后写的对象），删除 5 次重试 hack。converged 的 ACK 到达 master 时，sol/iters 必已注册，read 必命中 TIER2。 |
| Part 2 | `src/storage/py/database.py` | `read_object` 全 tier miss 时抛 `KeyError`（替代误导性的 `pickle.loads(b'')` → `EOFError`），把"对象不存在"语义还原为标准异常。 |
| WriterPrefRwLock | `src/common/cpp/writer_pref_rwlock.h` + `data_service.{h,cpp}` | `remote_mutex_` 从 `std::shared_mutex`（g++ 读者优先）改为基于 `pthread_rwlock` + `PTHREAD_RWLOCK_PREFER_WRITER_NONRECURSIVE_NP` 的写者优先锁。防止 CPU 饥饿下读者持 shared_lock 被抢占饿死写者（`update_remote_idx`）。reader fast path 单次 atomic，性能优于 mutex+cv。 |
| runqa 改进 | `qa/runqa` | 默认 `-j` 按 CPU 核数自适应（nproc≤4 用 nproc-1，nproc<8 用 nproc-2，否则 4）；加 `--no-shuffle` 选项，默认随机打乱测试顺序分散重测试。 |
| **v2 daemon 稳定性修复** | `src/solver/py/ras_graph_daemon.py` | check 收敛后**先 respond done 再 write_object**（原顺序导致 compute 在 write 的同步 register_write 期间因连接被关而 RPC 失败卡 RUNNING）；respond done 加 try/except 容错；RPC/accept timeout 从 120s 降到 30s（让卡住的 task 更快超时退出，避免 master stop drain 死等）。 |
| **PeerRpc 断连通知** | `src/agent/cpp/peer_rpc_server.{h,cpp}` + `worker_agent.{h,cpp}` + `pending_rpc_map.h` | PeerRpcServer 加 `DisconnectHandler`：P2P 连接断开时（check 收敛后 stop_peer_rpc 关闭连接），立即 fail 该连接上所有 pending RPC（status=3 disconnect），唤醒 `chan.rpc` 的 `wait_for`。**原实现 DISCONNECT 只清 recv_bufs 不通知 pending，导致 compute 卡在 `rpc(timeout=120)` 死等已关闭的连接**。PendingRpcMap 加 `complete_all_if` 批量 fail；PendingPeerRpc 加 `conn_id_` 字段按连接匹配。 |
| **check/compute 无 requires** | `src/solver/py/ras_graph_daemon.py` | check_daemon_task 和 compute_daemon_task 的 requires 从 `["check"]`/`["sd_{sd}"]` 改为 `[]`（随机分派到任意 idle worker），worker 配置全部普通 worker。消除 attribute 匹配延迟和专门 worker 等待。 |

### 验证

- **`-j1` v2 单测 15/15 全过**（修复前 baseline ~80%）。
- `-j1` solver 全量 28/28 全过。
- **`-j4` solver 并发 10/10 全过**（baseline 0-2/8）。
- C++ storage + network 单元测试 27/27 全过。

---

## 并发 hang 调查（已定位根因并修复）

### 现象

v2 daemon 测试（`test_ras_graph_v2*.py`）在以下场景间歇性 TIMEOUT（20s）：
- `runqa -j 2` 及以上并发（solver 全类别）
- 甚至 `-j1` 单测也有 ~20% 概率

### 根因（经 fprintf 无侵入诊断定位）

通过在 `Reactor::dispatch_message`、`on_write_register`、`on_task_complete`、
`MasterAgent::stop()` drain phase、`_wait_solution`、`fly/main.py` agent.stop()
全链路加 `fprintf(stderr)` 无侵入日志（绕过 logger buffer），定位到**真正的根因
不是 master reactor 冻结**（之前的假设全部错误）：

**两个独立的 bug 叠加**：

1. **check 收敛后的 respond/write 顺序问题**：check 先 write_object（3 个同步
   register_write）再 respond done，期间 compute 卡 RPC 等待。
2. **PeerRpcServer 断连不通知 pending RPC**（核心 bug）：check 退出时
   `stop_peer_rpc` 关闭所有 P2P 连接，但 PeerRpcServer 的 DISCONNECT handler
   只清 `recv_bufs_`，**不 fail `pending_peer_rpcs_` 里该连接的 pending RPC**。
   compute 的 `chan.rpc` 在 `wait_for` 死等一个永远不会来的 response（连接已断），
   task 卡 RUNNING。master `stop()` drain phase 等 RUNNING==0 死等 30s。

**为什么之前以为是 "epoll 不唤醒"**：
- master logger 用 `std::ofstream`（buffered），进程被 kill 时 buffer 丢失，
  日志看似"冻结"在某行。
- master reactor 实际一直在工作（处理 Heartbeat 等），只是后续事件不打 INFO 日志
  或日志在 buffer 里没刷盘。
- strace + fprintf(stderr) 揭示真相：所有消息都正常到达 master 的 dispatch 并被
  处理，`_wait_solution` 也成功读到 converged 返回。真正卡住的是 `master.stop()`
  的 drain phase 等一个卡 RUNNING 的 compute task（因 PeerRpc 断连不通知）。

### 修复

1. **check 收敛后先 respond done 再 write_object**：让 compute 尽快收到 done 退出。
2. **respond done 加 try/except 容错**：单个 respond 失败不影响其他 compute。
3. **RPC/accept timeout 从 120s 降到 30s**：让卡住的 task 更快超时退出。
4. **PeerRpcServer 加 DisconnectHandler**（核心修复）：P2P 连接断开时立即 fail
   该连接上所有 pending RPC（status=3 disconnect），唤醒 `chan.rpc` 的 `wait_for`。
   PendingRpcMap 加 `complete_all_if` 批量 fail；PendingPeerRpc 加 `conn_id_` 匹配。
5. **check/compute 无 requires**：随机分派到任意 idle worker，消除 attribute 匹配延迟。

### 验证结果

| 场景 | 修复前 | 修复后 |
|------|--------|--------|
| v2 `-j1` 单测 15 轮 | ~80% | **15/15** |
| solver `-j1` 全量 | 偶有 v2 失败 | **28/28** |
| solver `-j4` 并发 10 轮 | 0-2/8（baseline） | **10/10** |

并发 hang 问题已根治。原 EOFError 修复（Part 1-2）在所有场景完整有效，无回归。
