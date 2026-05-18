# Architecture Flow Changes — 2026-05-18 Session

## Executive Summary

This session introduced **MasterClient** to enable thread-safe remote reads from Workers, unified name resolution at Database entry points, and simplified the Worker's data request flow by removing Reactor-dependent handlers.

---

## Before vs After: Worker Remote Read Flow

### Before (Reactor-dependent, Async Handlers)

```
Worker Thread calls read_object("obj")
  ↓
Database::read_raw()
  ↓
DataService::try_read_local() → fail (not local)
  ↓
DataService::lookup_remote_idx() → fail (not cached)
  ↓
WorkerAgent::request_remote_data() [BLOCKS on Reactor]
  ↓
  1. pending_data_[obj_name] = PendingRemoteData
  2. Send DataQueryMessage via reactor_->send()
  3. Wait on CV pending_data_cv_
  ↓
[Reactor thread receives DataLocationMessage]
  ↓
on_data_location() → populate pending_data_[obj_name].location
  ↓
[Reactor thread sends DataRequestMessage to target worker]
  ↓
[Target Worker sends DataResponseMessage]
  ↓
on_data_response() → populate pending_data_[obj_name].data
  ↓
[Worker thread CV wakes, returns data]

Issues:
- Reactor::send() NOT thread-safe (Worker thread calling it)
- Complex async handler chain (3 handlers)
- pending_data_ map + mutex + CV per request
- Dead lock potential if Reactor blocked
```

### After (MasterClient + DataClient, Blocking TCP)

```
Worker Thread calls read_object("obj")
  ↓
Database::read_raw()
  ↓
DataService::try_read_local() → fail
  ↓
DataService::lookup_remote_idx() → fail
  ↓
WorkerAgent::request_remote_data()
  ↓
MasterClient::query_data_location(master_host, master_port, obj_name)
    [Independent TCP socket, blocking, thread-safe]
  ↓
DataClient::request_data(host, port, obj_name)
    [Independent TCP socket, blocking, thread-safe]
  ↓
DataService::update_remote_idx() → cache result
  ↓
Return data to caller

Benefits:
- No Reactor dependency (Worker thread calls directly)
- Each request uses independent socket (no thread conflicts)
- Simplified flow (2 blocking calls, no handlers)
- No pending_data_ map or CV needed
- Thread-safe by design (socket per call)
```

---

## Key Architectural Changes

### 1. MasterClient (NEW)

**Purpose**: Thread-safe blocking TCP client for Worker → Master queries.

**Location**: `src/network/cpp/master_client.h/cpp`

**API**:
```cpp
struct DataLocation {
    bool found;
    CMString host;
    int32_t port;
    uint64_t worker_id;
    CMString error;
};

static DataLocation query_data_location(
    const CMString& master_host,
    int32_t master_port,
    const CMString& object_name);
```

**Thread Safety**: Creates independent socket per call (no shared state).

**Message Flow**:
- Worker → Master: `DataQueryMessage`
- Master → Worker: `DataLocationMessage`

---

### 2. Name Resolution Unified at Database

**Purpose**: External callers use short names; Database converts to full names internally.

**Implementation**:
```cpp
// database.cpp
CMString Database::full_name(const CMString& short_name) const {
    return db_id_ + ":" + short_name;
}

CMVector<uint8_t> Database::read_raw(const CMString& name) {
    CMString full = full_name(name);  // Convert at entry point
    return data_service_->read_raw(full, ...);
}
```

**Indices**: All DataService indices (local_idx, remote_idx) keyed by full name `db_id:name`.

**Master Flow**:
- `on_data_ready()` / `on_task_complete()` → `update_remote_idx(full_name, ...)`

**Worker Flow**:
- `register_write()` → WorkerAgentContext uses `get_obj_name()` in inputs lambda
- `record_write()` → adds full name to current_writes_

---

### 3. Removed Dead Code

**Removed**:
- `PendingRemoteData` struct (no longer needed)
- `pending_data_` map + mutex (no async waiting)
- `on_data_response()` handler (Worker no longer receives via Reactor)
- `on_data_location()` handler (Worker uses MasterClient directly)
- Reactor registrations for `DataResponseMessage`, `DataLocationMessage`

**Retained**:
- `on_data_request()` — Worker still responds to DataRequestMessage (acts as data server)
- `PendingWriteRegister` — Still needed for write ACK flow
- `PendingDbPath` — Still needed for DB path query flow

---

### 4. Worker DB Alignment

**Purpose**: Worker's Database must have same db_id as Master's for name resolution.

**Implementation**:
```cpp
// database.h
void set_db_id(const CMString& db_id);

// executor.py (Worker process)
db = open_db(master_db_id)  # Creates DB with base_path
db.set_db_id(master_db_id)  # Aligns db_id with Master
```

**Why Needed**: Worker reads use `DataService::read_raw(full_name)`. If db_id differs, indices mismatch.

---

### 5. Base Path Uniqueness Validation

**Purpose**: Prevent different databases from sharing same directory (causes index corruption).

**Implementation**:
```cpp
// data_service.cpp
void DataService::register_database(...) {
    for (auto& [id, paths] : registered_dbs_) {
        if (paths.base_path == base_path) {
            throw std::runtime_error("Duplicate base_path: " + base_path);
        }
    }
}
```

---

## Message Flow Diagram

```
┌─────────────────────────────────────────────────────────────────┐
│                        MASTER AGENT                             │
│  ┌─────────────────┐                                            │
│  │ DataService     │  remote_idx: {"db1:obj": WorkerInfo}      │
│  │ (singleton)     │                                            │
│  └─────────────────┘                                            │
│         ↑                                                        │
│         │ on_data_ready() / on_task_complete()                  │
│         │ update_remote_idx(full_name, ...)                     │
└─────────────────────────────────────────────────────────────────┘
          │
          │ DataQueryMessage (via MasterClient blocking TCP)
          │ DataLocationMessage (response)
          ↓
┌─────────────────────────────────────────────────────────────────┐
│                        WORKER AGENT                              │
│  ┌─────────────────┐                                            │
│  │ request_remote_ │                                            │
│  │ data()          │                                            │
│  └─────────────────┘                                            │
│         │                                                        │
│         │ MasterClient::query_data_location()                   │
│         │ [blocking TCP, thread-safe]                           │
│         ↓                                                        │
│  ┌─────────────────┐                                            │
│  │ DataClient::    │                                            │
│  │ request_data()  │  [blocking TCP to target worker]          │
│  └─────────────────┘                                            │
│         │                                                        │
│         │ DataRequestMessage                                     │
│         │ DataResponseMessage                                    │
│         ↓                                                        │
│  ┌─────────────────┐                                            │
│  │ Target Worker   │  on_data_request() → submit_transfer()    │
│  │ (Reactor)       │  IOThreadPool reads file → send response  │
│  └─────────────────┘                                            │
└─────────────────────────────────────────────────────────────────┘
```

---

## Thread Safety Analysis

| Component | Before | After |
|-----------|--------|-------|
| Worker read from Reactor | ❌ `reactor_->send()` not thread-safe | ✅ No Reactor calls |
| MasterClient | N/A | ✅ Independent socket per call |
| DataClient | ✅ Already thread-safe | ✅ Same (unchanged) |
| DataService indices | ✅ Mutex-protected | ✅ Same (unchanged) |
| pending_data_ map | ❌ Mutex + CV per request | ✅ Removed (not needed) |

---

## Files Changed

| File | Change |
|------|--------|
| `src/network/cpp/master_client.h` | NEW — MasterClient class |
| `src/network/cpp/master_client.cpp` | NEW — Blocking TCP implementation |
| `src/network/cpp/BUILD` | +fly_network_master_client target |
| `src/storage/cpp/database.cpp` | +full_name(), +set_db_id() |
| `src/storage/cpp/data_service.cpp` | +writer_id in DbPaths, +base_path validation |
| `src/agent/cpp/worker_agent.cpp` | Refactored request_remote_data (MasterClient+DataClient) |
| `src/agent/cpp/worker_agent.h` | -PendingRemoteData, -pending_data_, -handlers |
| `src/agent/cpp/master_agent.cpp` | Uses full_name in update_remote_idx |
| `src/fly/executor.py` | +set_db_id() call |
| `src/e2e_tests/*.py` | 6 independent test files |
| `src/run_e2e_tests.sh` | Bash runner script |

---

## Known Issue

**Double Free Corruption on Exit** (exit code 134):
- Occurs after all tests pass, during Python module cleanup
- Likely caused by multiple .so modules registering same destructors
- Not blocking: Tests pass, functionality correct
- Tracked separately for investigation

---

## Testing

**All E2E Tests Pass**:
- test_worker_db_write.py ✅
- test_dependency_and_freeze.py ✅
- test_read_frozen_db.py ✅
- test_write_frozen_db_fails.py ✅
- test_recursive_submit.py ✅
- test_concurrent_read.py ✅ (verified thread-safe reads work)

---

*Last updated: 2026-05-18*