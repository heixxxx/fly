# Fly — Known Issues & Risks

> Auto-generated from comprehensive code review (2026-05-30).
> Status: `OPEN` = needs fix, `FIXED` = resolved, `PENDING` = deferred.
> Last updated: 2026-08-16.

---

## P0 — Critical (Production Safety)

### P0-1: No mutex in core data structures
- **Status**: FIXED ✅
- **Files**: `src/task/cpp/dependency_graph.h/cpp`, `src/task/cpp/worker_manager.h/cpp`, `src/task/cpp/task_manager.h/cpp`
- **Risk**: DependencyGraph, WorkerManager, TaskManager have ZERO mutex protection. 6 thread classes (reactor, heartbeat, scheduler, Python API, write-back, IO pool) concurrently access shared state — undefined behavior.
- **Fix Applied**: Added `mutable std::mutex` to each class; guard all public methods.

### P0-2: on_task_failed leaks DependencyGraph entries
- **Status**: FIXED ✅
- **File**: `src/agent/cpp/master_agent.cpp`
- **Risk**: `on_task_complete` calls `graph_->remove_task()` but `on_task_failed` does NOT. Failed tasks remain in the graph forever, potentially re-scheduled infinitely.
- **Fix Applied**: Added `graph_->remove_task(msg.task_id)` in `on_task_failed`.

### P0-3: fly.sh check hides test failures
- **Status**: FIXED ✅
- **File**: `fly.sh`
- **Risk**: Pipeline exit code may hide test failures.
- **Fix Applied**: Added `set -euo pipefail` at top of fly.sh.

### P0-4: Database::is_frozen_ non-atomic bool across threads
- **Status**: FIXED ✅
- **File**: `src/storage/cpp/database.h:89`
- **Risk**: `is_frozen_` is a plain `bool` read/written from different threads — undefined behavior.
- **Fix Applied**: Changed to `std::atomic<bool> is_frozen_{false}`.

---

## P1 — High

### P1-5: restart_failed_tasks deletes file before re-submission
- **Status**: FIXED ✅
- **File**: `src/agent/cpp/master_agent.cpp`
- **Risk**: File deleted before tasks re-submitted. Crash between these lines permanently loses failed task records.
- **Fix Applied**: Moved file deletion after re-submission loop.

### P1-6: DataService::reset() modifies state without mutex
- **Status**: FIXED ✅
- **File**: `src/storage/cpp/data_service.cpp`
- **Risk**: `reset()` clears all maps without acquiring `mutex_`.
- **Fix Applied**: Added `std::lock_guard<std::mutex> lock(mutex_)` at top of `reset()`.

### P1-7: LocalIndex::find_entry returns dangling pointer after mutex unlock
- **Status**: FIXED ✅
- **Files**: `src/storage/cpp/local_index.h/cpp`, `src/storage/tests/local_index_test.cpp`
- **Risk**: `find_entry()` returns raw pointer to internal map element. Lock released at function end, pointer may dangle.
- **Fix Applied**: Changed return type to `std::optional<IndexEntry>` (copy). Updated all callers including tests.

### P1-8: Write-back lambdas have no error handling
- **Status**: FIXED ✅
- **Files**: `src/storage/cpp/write_back_queue.cpp`, `src/storage/cpp/database.cpp`, `src/storage/cpp/data_writer.cpp`, `src/storage/cpp/local_index.cpp`
- **Risk**: Execute and complete lambdas have void return types and no error handling. Worker thread crash on exception → `pending_` stuck → `drain()` deadlock.
- **Fix Applied**: worker_loop 加 try-catch 防止 worker 线程崩溃 + drain 死锁；WriteRequest 新增 error 回调通道；DataWriter/local_index 落盘方法返回 bool 错误标志并检查流状态；落盘失败时打 ERR log 并按错误类型重试（瞬时 IO 错误），确定性失败（磁盘满/权限）则 ERR + 退出进程避免静默数据丢失。

### P1-9: TCPTransport::send() blocks reactor thread up to 5 seconds
- **Status**: FIXED ✅
- **Files**: `src/network/cpp/tcp_transport.h/cpp`
- **Risk**: `send()` blocks reactor thread with `poll(POLLOUT, timeout=5000ms)` under backpressure.
- **Fix Applied**: Added `set_nonblocking()` helper using `fcntl(fd, F_SETFL, flags | O_NONBLOCK)`. Send now uses `MSG_NOSIGNAL` flag.

### P1-10: Reactor::send() ignores send failure
- **Status**: FIXED ✅
- **File**: `src/network/cpp/reactor.h:82-88`
- **Risk**: `transport_->send()` return value silently discarded. Failed messages lost.
- **Fix Applied**: Added return value check with `WARN("Reactor::send failed for conn_id={}", conn_id)` on failure.

---

## P2 — Medium

### P2-11: Busy-wait polling instead of condition variables
- **Status**: FIXED ✅
- **Files**: `src/agent/cpp/worker_agent.h/cpp`
- **Risk**: 100-iteration × 50ms spin loops waste CPU. Inconsistent with CV-based `request_object_remove`.
- **Fix Applied**: Replaced busy-wait loops with `std::condition_variable::wait_for` pattern.

### P2-12: Code duplication across modules
- **Status**: FIXED ✅
- **Files**: Multiple
- **Details**:
  - Backup write logic duplicated 3× in `database.cpp:171-269`
  - Decompression loop duplicated 5× across `storage_export.cpp`, `data_service.cpp`
  - `recv_exact()`/`send_all()` duplicated in `data_client.cpp`, `metadata_client.cpp`
  - DataService `try_read_local/raw/raw_or_wait/or_wait` 4 methods with similar logic
- **Fix Applied**: 原先提取为 `src/network/cpp/net_utils.h/cpp`（recv_exact, send_all）。网络层抽象重构后所有客户端改用 `Transport`/`ConnectionManager`，`net_utils` 已作为死代码删除（见 2026-06-14 网络层重构）。Decompression loop 仍在 `src/storage/cpp/decompress_helper.h/cpp`。

### P2-13: std::endl flushes every log line
- **Status**: FIXED ✅
- **File**: `src/log/cpp/logger.cpp`
- **Risk**: `std::endl` forces a `write()` + `flush()` syscall per log line. Degrades throughput 10-100×.
- **Fix Applied**: Replaced `std::endl` with `"\n"`. Added conditional flush on WARN/ERROR level messages.

### P2-14: Python public API missing docstrings
- **Status**: FIXED ✅
- **Files**: `src/fly/__init__.py`, `src/fly/runtime.py`, `src/task/py/task.py`
- **Risk**: None of the public functions have docstrings. Users cannot use `help()`.
- **Fix Applied**: Added docstrings to all public functions in the Python API.

### P2-15: 28 QA tests not registered in Bazel BUILD
- **Status**: FIXED ✅
- **File**: `qa/BUILD`
- **Risk**: `bazel test //...` skipped 28 of 37 QA test files.
- **Fix Applied**: Added all 28 missing QA test targets. Now 40 `py_test` entries in BUILD.

### P2-16: text1b limits strings to 255 bytes
- **Status**: CLOSED — NOT A BUG
- **File**: `src/serialization/cpp/serialization_macros.h:229`
- **Note**: The code review incorrectly diagnosed `text1b` as having a 1-byte fixed length prefix. In bitsery, `text<N>` means N bytes per **character** (char width), not the length prefix. The length prefix uses bitsery's variable-length `writeSize()` encoding (1-4 bytes), supporting strings up to ~1GB. `text1b` is the correct choice for `CMString` (= `std::string`, char). Added `FLY_STR_U16` / `FLY_STR_U32` macros for future u16string/u32string support.

---

## P3 — Low / Deferred

### P3-26: lane 并行下重连注册先于旧 conn 断连处理 → deferred 注册孤儿化，worker 挂死
- **Status**: FIXED ✅（2026-08-18）— probe 发送失败当场接受重连
- **Files**: `src/agent/cpp/master_agent.cpp`（on_worker_register 疑似重复注册分支）、`src/network/cpp/reactor.h`（send 返回 bool + is_connected 直通）
- **Root Cause**: 50 轮稳定性第 16 轮实测（前 15 轮通过，复现率 ~1/43）。handler lane 并行分发（8a7e8b8，同连接保序/跨连接并行）破坏了 deferred 注册协议的单线程时序假设：worker 闪断重连时 REGISTER(新 conn) 与旧 conn 的 DISCONNECT 在**两条 lane 上并行**——若 REGISTER 的 dup 检查读到 `worker_to_conn_` 仍指向旧 conn（断连清理未完成）而 transport 已 reap 旧 conn（EOF 已检测、fd 已关），probe 发送失败仅 WARN，注册被挂起（deferred）；并行交错的另一侧 `on_disconnect` 的 `replay_deferred_register` take 先于挂起插入执行（no-op）→ 挂起条目孤儿化，无人重放，worker 挂死到 15s probe deadline 被保守误拒（duplicate）。
- **Fix**: ① probe 发送失败 = 旧 conn 确定已死 → 当场清残留映射、走正常注册路径接受重连（不等 DISCONNECT 触发 replay，迟到 DISCONNECT 因映射已清而 no-op）；② 挂起插入后复核旧 conn 存活性（`Reactor::is_connected`），已死则就地自重放——封死「插入晚于 take」的残余交错窗口（重放路径再进 on_worker_register 时 probe 必失败，走①，无递归风险）。`Reactor::send` 改返回 bool（调用方多忽略返回值，兼容）。
- **测试**: 新增 `ReconnectRegisterBeforeDisconnectProcessed`（确定性）：`on_disconnect_entry_hook_for_testing_` 阻塞旧 conn 断连处理（lane 隔离保证 conn1/conn2 异 lane 并行），重连注册必然先于断连处理——修复前确定性转红（挂起孤儿化），修复后必绿。原间歇用例 `WriteRegisterPendingBlocksUntilReconnected` 同时修复其失败路径的 terminate 放大器（ASSERT 提前返回致 writer 线程未 join → std::terminate 吞掉 logger 缓冲现场）。
- **验证**: TDD 红→绿；三重连用例并行 ×10 + 串行 ×10 全过；全量单测 60/60；50 轮稳定性重跑中。取证日志（WARN 级 handler 入口状态）已按规范移除。
- **架构收口（2026-08-18 同日补强）**: 消息顺序敏感分析确认身份域三消息（REGISTER/WorkerProbeAck/断连事件）是唯一真跨连接顺序依赖族，其余 handler 均为收敛型。Reactorn 新增 `set_serialized_domain`（保留串行 lane，跨连接 FIFO；域外消息并行不受影响），master 将身份域三成员入域——该族交错类缺陷从机制上根除。机制单测 `reactor_serialized_domain_test`（FIFO/并行保持/生命周期事件三用例）+ `ReconnectRegisterBeforeDisconnectProcessed` 改造为串行域语义（REGISTER 不得越过被阻塞的断连处理）。后续新增顺序敏感消息一律加入此域（约定见 architecture.md §4.2 与 master_agent.cpp 注册点）。

### P3-25: PeerRpcServer BYE 优雅关闭在 ACK/DISCONNECT 竞态窗口误触发 disconnect_handler
- **Status**: FIXED ✅（2026-08-18）— ACK 到达处同线程标记 bye_closed
- **Files**: `src/agent/cpp/peer_rpc_server.cpp`（handle_bye / send_bye）、`src/agent/tests/peer_rpc_server_test.cpp`
- **Root Cause**: 50 轮稳定性测试（unit --no-cache + QA）第 9 轮实测复现（前 8 轮通过）：`SendByeGracefulCloseWithoutDisconnectCallback` 失败。机制：服务端收到 BYE 后回 BYE_ACK 并立即 close；客户端 server_loop 处理 DATA(BYE_ACK) 时 `handle_bye` 只插入 `bye_ack_conns_` 并 notify cv，`bye_closed_conns_` 的标记留给 send_bye **调用方线程**在 cv 唤醒后执行——跨线程 TOCTOU：server_loop 紧接着处理 DISCONNECT 事件（EOF，与 ACK 几乎同时到达）时标记尚未落位 → `is_bye=false` → 优雅关闭误触发 disconnect_handler。日志证据：DISCONNECT（fd=6）先于 "BYE_ACK received" 被处理。CPU 负载下调用方线程唤醒慢一步即输掉竞争，低负载时通常侥幸通过。
- **Fix**: `handle_bye` 客户端分支在插入 `bye_ack_conns_` 的同一锁内同时插入 `bye_closed_conns_`——标记与后续 DISCONNECT 事件的处理同线程（server_loop）天然有序（transport 保证数据+FIN 同时到达时先 DATA 后 DISCONNECT，`tcp_connection_manager.cpp` drain_socket）。send_bye 唤醒后的标记保留（幂等；覆盖 ACK 丢失的 force-close 路径）。
- **测试**: 新增 `ByeAckDisconnectRaceDoesNotFireDisconnectHandler`——`bye_wake_hook_for_testing_` 在 cv 唤醒后 park 调用方 200ms（不持锁，持锁会阻塞 DISCONNECT 处理反而掩盖竞态），DISCONNECT 必然先被 server_loop 处理：修复前确定性转红、修复后必绿。peer_rpc_server_test 切 test_hooks 库变体。
- **验证**: 单测红灯→修复→绿灯（TDD）；peer_rpc_server_test `--runs_per_test=100` 全过；全量单测 60/60；50 轮稳定性重跑。

### P3-24: test_golden_n500_sd4_coarse 在 coverage 全量（-j6）下稳定 npz EOFError
- **Status**: FIXED ✅（2026-08-16）— 原子写根治
- **Files**: `src/solver/py/ras_graph.py`（generate_poisson_matrix / compute_exact_solution）
- **Root Cause**: `compute_exact_solution` 的后台线程在 splu 后**原地 `np.savez(path)` 重写共享矩阵文件**（truncate → 重写窗口）。golden_solver 的 fallback 路径（无 prebuilt 矩阵时）并行起 exact 线程与 solve task——高负载下 worker task 的 `_load_matrix` 首读被拖进重写窗口，读到截断视图 → zipfile EOFError（vals 在 npz 尾段最易被截）。低负载时 worker 首读（毫秒级）先于 exact 线程重写（splu 1.4s+）完成，故单跑/串行不复现。
- **Fix**: 写协议统一为**原子替换**——savez 到文件对象（tmp 路径）+ `os.replace`（generate 首写/exact 重写/compute_exact_solution 三处）。读方要么见旧版完整文件、要么见新版，永无中间态。注意 `np.savez(路径)` 会给非 `.npz` 后缀自动追加后缀，故写入用文件对象。
- **验证**: 新增单测 ras_graph_io_test（testzip 完整性/无 tmp 残留/幂等重写）；coverage 单跑 ×2 + coverage 全量 -j2 147/147（原失败场景）全过。

### P3-23: agent_network_test DuplicateWorkerRegisterRejectedAfterProbe 高并行负载下偶发超限
- **Status**: FIXED ✅（2026-08-17）— 注册 ack 丢失重发兜底
- **Files**: `src/agent/cpp/worker_agent.cpp`（heartbeat_loop 未注册分支）、`src/core/cpp/config.cpp`（新键）
- **Root Cause**（确定性证据链，按用户要求不采信无证据的"资源饥饿"归因）：
  1. 失败现场特征：second 实例 60s 不退出、`is_registered=0`、master 侧流程消息 `AGENT::0001 ×2`（两次正常注册）。
  2. 事件 trace 实证健康路径亚秒完成（非"高负载必然慢"）。
  3. 代码链穷举定位结构洞：`replay_deferred_register`（master 侧，first 连接断开时触发）把挂起的 second 转正常注册并重发 RegisterAck——**ack 送达无任何保障**（连接若也在断开中，`reactor_->send` 失败仅 WARN 静默）；而 **worker 侧注册协议无 ack 重发**（"首注册不假设时限"被实现成了无限静默等待）。deferred 条目在 replay 时已被 take 清空 → **15s deadline 兜底对该 worker 失效** → ack 一旦丢失即永久挂死。这是"应用层丢消息导致挂死"的确定机制，负载只是放大了 first 连接抖动与 ack 丢失的概率。
- **Fix**: 注册守望线程（`register_watchdog_loop`）——事件驱动的 ack 等待 + 超时退避重发。**职责分层**：连接级丢失（ack 丢失的真实主因）由 `on_disconnect → reconnect_loop` 的既有事件驱动路径恢复（毫秒级，无超时参与）；守望只覆盖「master 活着但注册/ack 被应用层吞掉」——cv 等 ack（`on_register_ack` 持锁 notify，注册成功即刻退出零空转），超时则指数退避重发（`worker_register_ack_retry_initial_ms` 默认 500ms，×2 上限 30s；`reconnecting_` 期间让位给 reconnect_loop）。master 对同 conn 重发走正常注册路径（`worker_to_conn_` 同 conn 跳过 probe 分支）幂等安全；不违反"首注册不假设时限"语义（重发是幂等重试，非超时失败判定）。
- **测试**: 新增 `RegisterAckLossRecoveredByResend`（master `drop_next_register_for_testing_` hook 吞掉首条 REGISTER 确定性构造丢失，1s interval 重发后注册成功；修复前该场景永久挂死）。agent_network_test 切换 test_hooks 库变体。
- **残留**: first 连接在失败场景中为何断开（触发 replay 的上游）未获直接现场（唯一失败现场被清理命令误删，~600 runs 复现 2 次）——重发兜底已使该上游无论为何，worker 不再挂死；取证装置（scene dump + 每秒事件 trace）保留在测试内，若复发可直接定位。

### P3-19: DEBUG/INFO 日志 flush 延迟导致运行中读取漏行（原：-j16 下偶发缺失）
- **Status**: FIXED ✅（2026-08-15）— Logger 自动 flush（累计 64KB 或距上次 flush 1s，
  config 可调 log_flush_threshold_bytes / log_flush_interval_ms；WARN/ERROR 立即不变）。
  根因确认（worker role QA 调试中实证）：INFO 缓冲仅退出时 flush，测试运行中读日志
  必漏最新行——非 resolve_log_dir 竞争（原怀疑方向排除）。QA 断言读日志仍建议在
  stop 后（终态一致），但延迟上限从"进程生命周期"缩到 ≤1s。
- **Evidence**（2026-08-15 高压稳定性测试第 5/10 轮，`test_startup_info`）：
  worker1.log 完整缺少 `Fly Startup Info (worker)` 段（connect/DataServer/Register
  等其余日志齐全）。原怀疑方向 resolve_log_dir 轮转竞争**已排除**——根因为
  INFO 缓冲仅退出时 flush（运行中读取必漏最新行）。
- **Probability**: ~1/3000 case（两轮 -j16 完整压力共 20 轮出现 1 次；随后完整
  10/10 轮全过）。
- **Next**: 无遗留——已修复（见 Status）。

### P3-18: 退出期偶发 pure virtual method called（静态析构竞态）
- **Status**: FIXED ✅（2026-08-15，5058f01）— Logger 单例 leak-on-exit 根治
- **Root Cause**: 高压 QA 第三次捕获（qa/solver/test_golden_n50_sd4_r30）拿到完整
  现场栈：resource_monitor_loop 后台线程退出期 log_write 的局部 shared_ptr
  last-use release 与函数局部 static shared_ptr 控制块的静态析构竞争（Python
  atexit/coverage 收尾可触发与 main join 顺序无关的析构路径）→ 已析构控制块上
  虚调用 → terminate。修复：instance() 改 new 裸对象 + noop deleter（每次独立
  控制块，永不触及静态析构），文件 flush 仍由显式 shutdown() 保证。
- **Evidence**（2026-08-15 高压稳定性测试第 10/10 轮，`test_solver_ras_n4_sd2_ov1_noconv`）：
  master drain 正常完成后进程退出阶段 `pure virtual method called → std::terminate`，
  栈落在静态析构链的 `shared_ptr` release（strip 后符号近似为 Logger，但 Logger 类
  无虚函数——真实源头不明）。嵌入式 Python 进程退出的静态析构序竞态经典形态。
- **Probability**: ~1/3120 case（两轮完整 -j16 高压共 20 轮 3120 case 出现 1 次；
  常压 20 轮 + CPU 饱和 25 轮定向复现均未触发）。
- **Next**: 无遗留——已修复（见 Status；5058f01）。

### P3-17: Concurrency stress test infrastructure
- **Status**: PARTIAL — 基础设施已建立，覆盖面仍需扩展
- **Risk**: Zero tests verify concurrent access to shared data structures. Data races will only manifest in production under load.
- **Fix Applied**: 已建立确定性并发测试基础设施 —— `src/storage/tests/data_service_concurrency_bench.cpp`（DataService 并发锁争用 micro-benchmark）、`src/agent/tests/master_agent_test.cpp`（用 `std::latch` 协调线程交错的竞态测试，经 `FLY_ENABLE_TEST_HOOKS` 隔离）、`src/task/tests/scheduling_hotloop_bench.cpp`（调度热循环 bench）。覆盖面仍限于 DataService / master_agent / scheduler，全模块并发覆盖待后续扩展。

### P3-21: Dead code cleanup（原编号 P3-18，2026-08-16 去重——与新 P3-18 编号冲突）
- **Status**: FIXED ✅
- **Files**: Multiple
- **Details**:
  - `DependencyGraph::pending_count_` — written but never read
  - `TaskScheduler::locality_enabled_` — stored but never consulted
  - `TaskStatus::CANCELLED` — exported to Python via `FLY_EXPORT_ENUM_VALUE` (kept for API compat)
  - `TaskExecStatus::TIMEOUT` — enum value never produced
  - `WorkerManager::record_heartbeat` — dead method
- **Fix Applied**: Removed unused fields, methods, and enum values. `CANCELLED` retained in Python exports for API compatibility.

### P3-22: MetadataClient success path untested（原编号 P3-19，2026-08-16 去重——与新 P3-19 编号冲突）
- **Status**: FIXED ✅
- **File**: `src/network/tests/metadata_client_test.cpp`
- **Risk**: Only failure cases and message encoding were tested. No end-to-end query against a running server.
- **Fix Applied**: 补充轻量 mock master server e2e 测试：多副本成功路径（`found_=true` + `all_locations_` 填充 + 便捷字段镜像）、`can_still_produce_` 透传、server 主动回 `success_=false`（对象不存在）路径、往返 object_name 一致性校验。

### P3-20: DataService violates SRP
- **Status**: FIXED ✅
- **Files**: `src/storage/cpp/data_service.h/cpp`
- **Risk**: DataService manages local indexes, remote indexes, transfer server, write-back queue, and event callbacks — 40+ public methods.
- **Fix Applied**: Internal reorganization with section comments and extracted helper methods (e.g., `do_read_local_entries`). Public API preserved unchanged. Full class split deferred to avoid breaking downstream consumers.

---

## Additional Issues (Not Prioritized)

| ID | Issue | File | Severity | Status |
|----|-------|------|----------|--------|
| X-1 | `StorageManager` maps have no synchronization | `storage_manager.cpp` | Medium | FIXED ✅ — 新增 `ConcurrentMap`，StorageManager 已改用；其余模块(DependencyGraph/WorkerManager/TaskManager/DataService)已有 mutex 保护，Reactor 为单线程事件循环，均无需替换 |
| X-2 | Raw pointers in project code (excluding third-party libs) | Multiple | High | FIXED ✅ — ①get_worker()/get_task()→optional<reference_wrapper<T>>消除29+裸指针 ②WorkerAgentContext回调改std::function消除7处trampoline static_cast ③DataService→CMSharedPtr singleton+enable_shared_from_this，agent层CMWeakPtr<DataService>观察者 |
| X-3 | `dispatch_message` head-of-line blocking on decode failure | `reactor.cpp` | Medium | FIXED ✅ — ERR 日志 + 清空 buffer |
| X-4 | `register_handler` copies entire receive buffer per message | `reactor.h:69-78` | Medium | FIXED — 去掉冗余拷贝，decode 已原地修改 buffer |
| X-5 | `wait_until_running()` deadlock if stopped before run | `reactor.cpp` | Medium | FIXED — 增加 stop_requested_ 检测 + ERR 日志 + assert
| X-6 | 4 MessageType enums with no struct definitions | `message_types.h:15-18` | Low | RECORD ONLY — 记录不处理 |
| X-7 | `get_int` throws on unknown key, `get_str` returns empty | `config.cpp` | Medium | FIXED ✅ — `get_int` 未知 key 返回 `INVALID_INT`（INT64_MIN）+ ERR log，不再 throw；`get_str` 返回空串 |
| X-8 | Logger silently drops output before init() | `logger.cpp` | Medium | FIXED ✅ — pre-init 输出至 stdout |
| X-9 | `common_types.h` global namespace pollution | `common_types.h:91-110` | Medium | RECORD ONLY — 期望行为 |
| X-10 | `pickle.loads` arbitrary code execution risk | `task.py:168` | High | RECORD ONLY — 业务层已确认安全性，仅记录 |
| X-11 | `main.py` bare except catches programming errors | `main.py:123` | Medium | FIXED ✅ — `traceback.print_exc` |
| X-12 | `main.py` accesses `agent._agent` private attribute | `main.py:78` | Low | FIXED ✅ — Worker 新增公共方法 |
| X-13 | `task_queue_` unbounded growth in WorkerAgent | `worker_agent.cpp` | Medium | RECORD ONLY — 业务层保证 |
| X-14 | `TaskExecutor::cancel()` is a no-op | `task_executor.cpp` | Medium | FIXED — 移除 cancel 声明+实现+测试+export |
| X-15 | Task timestamps not auto-set | `task_manager.cpp` | Medium | FIXED — create_task 设 created_at, update_status 设 started_at/completed_at |
| X-16 | `worker_attributes` parsing silent on malformed input | `runtime.py:52` | Low | FIXED — get_int 返回 INT64_MIN 表示无效值，调用方需检查 |
| X-17 | Triple `gc.collect()` in cleanup | `main.py:48-50` | Low | FIXED — 改为单次 gc.collect() |
| X-18 | Python API `__getattr__` hides properties from static analysis | `__init__.py` | Low | RECORD ONLY — 记录不修复 |
| 007 | task 生命周期并发竞态审计（6 个残留问题） | [docs/issues/007](issues/007-task-lifecycle-concurrency-audit.md) | High | FIXED ✅ — 6 个残留问题全部修复（commit 62b7355 + 312e535 + 95a9fc3） |

---

## Summary

> Last updated: 2026-08-16

| Category | Total | Fixed | Pending | Open |
|----------|-------|-------|---------|------|
| P0 — Critical | 4 | 4 | 0 | 0 |
| P1 — High | 6 | 6 | 0 | 0 |
| P2 — Medium | 6 | 5 | 0 | 1 (closed: not a bug) |
| P3 — Low | 10 | 9 | 1 (P3-17 partial) | 0 |
| X — Unprioritized | 18 | 9 | 0 | 9 |
| **Total** | **44** | **33** | **1** | **10** |

> 2026-08-16 去重说明：原 P3-18（Dead code cleanup）→ **P3-21**、原 P3-19（MetadataClient）→ **P3-22**，为新 P3-18（退出期 pure virtual）/P3-19（日志 flush）腾出编号（后者已被 commit 5058f01/c119b1b 与 DOC_CHANGELOG 引用，保持不变）。
