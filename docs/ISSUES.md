# Fly — Known Issues & Risks

> Auto-generated from comprehensive code review (2026-05-30).
> Status: `OPEN` = needs fix, `FIXED` = resolved, `PENDING` = deferred.
> Last updated: 2026-05-31.

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

### P3-19: -j16 高压下偶发 worker 启动信息未写入本地 debug log（未定位）
- **Status**: OPEN — 已捕获 1 次，30 轮定向复现失败（15 常规 + 15 饱和 -j8），待专项
- **Evidence**（2026-08-15 高压稳定性测试第 5/10 轮，`test_startup_info`）：
  worker1.log 完整缺少 `Fly Startup Info (worker)` 段（connect/DataServer/Register
  等其余日志齐全）。emit_system_message 直写 Logger（message_dispatch.cpp:42），
  打印路径未被改动。怀疑方向：Logger::resolve_log_dir 轮转目录在多进程高并发下的
  竞争（写入轮转目录而测试读主目录）。
- **Probability**: ~1/3000 case（两轮 -j16 完整压力共 20 轮出现 1 次；随后完整
  10/10 轮全过）。
- **Next**: 专项查 resolve_log_dir 并发轮转竞争（.latest symlink 与 .N 目录的
  多进程互斥）。与 2026-08-15 G 系列（断连重连）无因果路径。

### P3-18: 退出期偶发 pure virtual method called（静态析构竞态，未定位）
- **Status**: OPEN — 已捕获 1 次，45 轮定向复现失败，待专项
- **Evidence**（2026-08-15 高压稳定性测试第 10/10 轮，`test_solver_ras_n4_sd2_ov1_noconv`）：
  master drain 正常完成后进程退出阶段 `pure virtual method called → std::terminate`，
  栈落在静态析构链的 `shared_ptr` release（strip 后符号近似为 Logger，但 Logger 类
  无虚函数——真实源头不明）。嵌入式 Python 进程退出的静态析构序竞态经典形态。
- **Probability**: ~1/3120 case（两轮完整 -j16 高压共 20 轮 3120 case 出现 1 次；
  常压 20 轮 + CPU 饱和 25 轮定向复现均未触发）。
- **Next**: 专项排查 atexit 静态析构链与 Python finalization 的交错（core dump +
  符号化完整栈 / 逐静态审计）。与 2026-08-14 批次改动无因果路径（未触碰
  Logger/静态析构/退出路径），非本批次引入的判定依据已记录于该批次 commit。

### P3-17: Concurrency stress test infrastructure
- **Status**: PARTIAL — 基础设施已建立，覆盖面仍需扩展
- **Risk**: Zero tests verify concurrent access to shared data structures. Data races will only manifest in production under load.
- **Fix Applied**: 已建立确定性并发测试基础设施 —— `src/storage/tests/data_service_concurrency_bench.cpp`（DataService 并发锁争用 micro-benchmark）、`src/agent/tests/master_agent_test.cpp`（用 `std::latch` 协调线程交错的竞态测试，经 `FLY_ENABLE_TEST_HOOKS` 隔离）、`src/task/tests/scheduling_hotloop_bench.cpp`（调度热循环 bench）。覆盖面仍限于 DataService / master_agent / scheduler，全模块并发覆盖待后续扩展。

### P3-18: Dead code cleanup
- **Status**: FIXED ✅
- **Files**: Multiple
- **Details**:
  - `DependencyGraph::pending_count_` — written but never read
  - `TaskScheduler::locality_enabled_` — stored but never consulted
  - `TaskStatus::CANCELLED` — exported to Python via `FLY_EXPORT_ENUM_VALUE` (kept for API compat)
  - `TaskExecStatus::TIMEOUT` — enum value never produced
  - `WorkerManager::record_heartbeat` — dead method
- **Fix Applied**: Removed unused fields, methods, and enum values. `CANCELLED` retained in Python exports for API compatibility.

### P3-19: MetadataClient success path untested
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

> Last updated: 2026-08-12

| Category | Total | Fixed | Pending | Open |
|----------|-------|-------|---------|------|
| P0 — Critical | 4 | 4 | 0 | 0 |
| P1 — High | 6 | 6 | 0 | 0 |
| P2 — Medium | 6 | 5 | 0 | 1 (closed: not a bug) |
| P3 — Low | 4 | 3 | 1 (P3-17 partial) | 0 |
| X — Unprioritized | 18 | 9 | 0 | 9 |
| **Total** | **38** | **27** | **1** | **10** |
