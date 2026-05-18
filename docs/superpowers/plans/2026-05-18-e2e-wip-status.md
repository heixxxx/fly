# E2E Test Work In Progress — 2026-05-18

## Summary

Continuing from session `ses_1cb199c8cffeGI4M0JNL6ldNIA` (OOM killed).
Resumed session added Master-side remote read capability but E2E test still fails.

## Completed Changes

### 1. Build Infrastructure
- **`src/test/cpp/BUILD`**: Added `strip_include_prefix = "/src"` to fix `_fly_test.so` compilation
- **`src/test/`**: Full test module (TestObject C++ class, export, Python tasks) — carried over from previous session
- **`src/main/cpp/main.cpp`**: Added `src/test/export` to Worker `sys.path`
- **`src/main/cpp/BUILD`**: Added `_fly_test.so` as data dependency
- **`src/fly/BUILD`**: Added `_fly_test.so` dep + imports

### 2. Master-side Remote Read (NEW)
- **`src/agent/cpp/master_agent.h`**: Added `request_remote_data()` and `request_data_from_worker()` declarations
- **`src/agent/cpp/master_agent.cpp`**: Implemented both methods using DataService `lookup_remote_idx` + `DataClient::request_data`
- **`src/agent/cpp/BUILD`**: Added `fly_network_data_client` dependency to `fly_agent_master`
- **`src/agent/export/agent_export.cpp`**: Exported both methods on `EXAgentMaster`
- **`src/fly/database.py`**: Reverted to agent-agnostic `_read_remote()` (Master and Worker now both have `request_remote_data`)

### 3. Master Python API
- **`src/fly/agent.py`**: Added `wait_for_all_tasks()` method to `Master` class

### 4. E2E Test Scripts (carried from previous session)
- **`src/e2e_tasks.py`**: 5 task functions (write_data, freeze_db, read_data, write_after_freeze, fanout_write)
- **`src/e2e_user_script.py`**: Full E2E test suite with 5 test cases

## Current Blocking Issue

### Symptom
`remote_idx` is empty after Worker completes a write task. `read_object()` fails with "No remote location found".

### Root Cause Analysis (in progress)

The data notification chain:
```
Worker write_object_typed()
  → WorkerAgentContext::register_write() [trampoline]
  → WorkerAgent::register_write_with_master() [sends WriteRegisterMessage, blocks for ACK]
  → [actual write to disk]
  → WorkerAgentContext::record_write() [trampoline]
  → WorkerAgent::record_write()
    → sends DataReadyMessage (streaming mode)
    → adds to current_writes_
  → end_task() returns written_objects
  → TaskCompleteMessage with written_objects
```

Master side:
```
on_data_ready() → ds.update_remote_idx(obj_name, worker_id, host, port)
on_task_complete() → in streaming mode, skips update_remote_idx
```

**Key finding**: `WorkerAgentContext` uses `thread_local` storage for trampolines.
`begin_task()` sets trampolines on the calling thread. In process Worker mode,
the Python main loop calls `poll_task()` → `begin_task()` → Python callback
→ `db.write_object()` → C++ `write_object_typed()`. The thread_local context
should be on the same thread, but this needs verification.

**Hypothesis 1**: `thread_local` context is not visible when `Database::write_object_typed`
is called from Python through nanobind (different thread?).

**Hypothesis 2**: `DataReadyMessage` is sent but Master's `on_data_ready` receives
empty worker address (`get_worker_address` returns empty host/port).

### Debug Steps Needed
1. Add logging to `WorkerAgent::record_write()` to verify it's called
2. Add logging to `MasterAgent::on_data_ready()` to verify it receives the message
3. Verify `DataService::get_worker_address()` returns valid data for the worker
4. Check if `register_write_with_master` ACK flow works (it blocks — if ACK never comes, write would fail with timeout, but task shows "success")
5. Consider whether nanobind Python→C++ calls preserve thread identity for `thread_local`

## Test Status

- **35 Bazel unit tests**: ALL PASS
- **2 QA tests** (smoke + storage): ALL PASS
- **E2E tests**: FAILING — `remote_idx` not populated after Worker writes

## Files Changed (this session)

```
src/agent/cpp/BUILD              — data_client dep for master
src/agent/cpp/master_agent.h     — request_remote_data, request_data_from_worker
src/agent/cpp/master_agent.cpp   — implementations + data_client include
src/agent/export/agent_export.cpp — export new Master methods
src/fly/agent.py                 — wait_for_all_tasks()
src/fly/database.py              — agent-agnostic _read_remote
src/fly/BUILD                    — _fly_test.so dep + imports
src/main/cpp/main.cpp            — test/export sys.path
src/main/cpp/BUILD               — _fly_test.so data dep
src/test/cpp/BUILD               — strip_include_prefix fix
```
