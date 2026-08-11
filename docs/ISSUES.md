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
- **Status**: PENDING
- **File**: `src/storage/cpp/database.cpp:152-177`
- **Risk**: Execute and complete lambdas have void return types and no error handling.
- **Note**: Deferred. Design preference: use error codes and in-place handling rather than try/catch. Requires broader audit of write-back error paths.

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

### P3-17: [PENDING] No concurrency stress tests
- **Status**: PENDING
- **Risk**: Zero tests verify concurrent access to shared data structures. Data races will only manifest in production under load.
- **Note**: Deferred — requires dedicated test infrastructure for concurrent scenarios.

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

### P3-19: [PENDING] MetadataClient success path untested
- **Status**: PENDING
- **File**: `src/network/tests/metadata_client_test.cpp`
- **Risk**: Only failure cases and message encoding are tested. No end-to-end query against a running server.
- **Note**: Deferred — requires mock server infrastructure.

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
| X-7 | `get_int` throws on unknown key, `get_str` returns empty | `config.cpp` | Medium | PENDING — 需全局约定无效值 |
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
| 007 | task 生命周期并发竞态审计（6 个残留问题） | [docs/issues/007](issues/007-task-lifecycle-concurrency-audit.md) | High | OPEN — H1/H2 高危竞态待修，详见 issue 文档 |

---

## Summary

| Category | Total | Fixed | Pending | Open |
|----------|-------|-------|---------|------|
| P0 — Critical | 4 | 4 | 0 | 0 |
| P1 — High | 6 | 5 | 1 | 0 |
| P2 — Medium | 6 | 5 | 0 | 1 (closed: not a bug) |
| P3 — Low | 4 | 2 | 2 | 0 |
| X — Unprioritized | 18 | 8 | 1 | 9 |
| **Total** | **38** | **24** | **4** | **10** |
