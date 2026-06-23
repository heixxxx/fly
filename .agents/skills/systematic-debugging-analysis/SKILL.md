---
name: systematic-debugging-analysis
description: Multi-dimensional debugging combining code flow, architecture, logs, and runtime results. NEVER guess — instrument first with logs at every decision point until exact failure is visible. Use for distributed system issues, concurrency bugs, network communication problems, cross-language (C++/Python) boundaries, mysterious failures, NameError, ImportError, or when logs are missing for the failure path.
---

# Systematic Debugging

## Core Principle

**Don't guess. Add logs. Run. Observe.**

Guessing wastes hours. Instrumented logs find bugs in minutes.

But when logs aren't enough — **use runtime introspection tools (GDB, pstack) to get thread stacks directly**. This is mandatory for deadlock/concurrency bugs where logs alone can't reveal cross-thread state.

## Workflow

### Phase 1: Reproduce & Classify

```
1. Reproduce the failure (deterministic test case)
2. Classify:
   - Component: Master / Worker / Network / Storage / Task
   - Type: Network / Memory / Logic / Concurrency / Deadlock
   - Layer: C++ / Python / C++↔Python boundary
```

### Phase 2: Instrument First (MANDATORY)

Before any code change or hypothesis, add logs:

```
1. Entry/exit logs on all involved functions
2. Value logs at every decision point (if/switch/branch)
3. Import/load logs at module boundaries
4. State logs before and after mutations
```

Example:
  void MasterAgent::assign_task(uint64_t task_id, uint64_t worker_id) {
      LOG("assign_task START: task=" + task_id + " worker=" + worker_id);
      LOG("worker state: " + worker_manager_->get_worker_state(worker_id));
      ... original logic ...
      LOG("assign_task END: success");
  }

**Run → last successful log = exact failure location.**

### Phase 3: Runtime Introspection (for deadlocks, hangs, concurrency)

When the process hangs and logs don't reveal why, **DO NOT keep guessing with more logs**. Get the actual thread state:

#### 3a. GDB attach (most powerful — full stack traces)

```bash
# Find the hanging process
WORKER_PID=$(pgrep -f "worker_id" | head -1)

# Get ALL thread stacks — reveals exactly where each thread is blocked
gdb -batch -ex "thread apply all bt" -p $WORKER_PID 2>/dev/null

# Key patterns to look for in stack traces:
#   futex_wait / pthread_cond_wait  → thread is blocked on a mutex/cv
#   PyGILState_Ensure              → thread waiting for Python GIL (C++↔Python deadlock)
#   __lll_lock_wait                 → thread waiting for a mutex
#   follow the call chain up to find WHO holds the lock the thread is waiting for
```

#### 3b. pstack (quick, no GDB overhead)

```bash
pstack $PID                        # single process
pstack $PID | grep -A5 "Thread"    # summarize thread states
```

#### 3c. strace (for I/O hangs, network issues)

```bash
strace -p $PID -e trace=network,read,write -f  # see what syscalls are blocking
```

**When to escalate from logs to GDB:**
- Process hangs (no crash, no log output, just stuck)
- Suspected deadlock (mutex, condition variable, GIL)
- Intermittent hang that's hard to reproduce with logs
- Logs show the entry point but not the block point

**GDB stack trace analysis is often faster than adding more logs for concurrency bugs.**

### Phase 4: Multi-Dimensional Analysis

If logs + GDB don't fully reveal root cause, expand dimensions:

| Dimension | What to check |
|-----------|--------------|
| **Code flow** | Call chain, data flow, lifecycle |
| **Logs** | Master log, Worker logs, timestamps, operation sequence |
| **Architecture** | Master↔Worker interaction, message flow, data paths |
| **Runtime** | Thread safety, resource state, timing issues |
| **GDB stacks** | Cross-thread lock dependencies, GIL contention |

### Phase 5: Fix Root Cause

```
1. Identify from log/GDB evidence (not speculation)
2. Minimal fix — no refactoring
3. Run tests → verify
4. Remove debug logs
```

## When to Use

- Any mysterious failure ("code should work but doesn't")
- NameError / ImportError / undefined variable
- Multi-language boundaries (Python ↔ C++)
- Distributed system issues (Master↔Worker communication)
- Concurrency bugs, race conditions, **deadlocks (use GDB!)**
- Network failures, connection issues
- After 2+ guess-based fixes failed

## Key Logging Points by Component

```
MasterAgent: Worker register/disconnect, task assign/schedule,
             heartbeat, DB register, data ready handling

WorkerAgent: Master connection, task start/end,
             data read/write, heartbeat, write tracking

Network:     Connection open/close, message send/recv,
             errors, timeouts

Storage:     DB create/write/read/freeze, index operations,
             DataService local/remote idx updates
```

## Anti-Patterns (NEVER)

- ❌ Guess without logs → add logs first, always
- ❌ Pure static code analysis for runtime issues
- ❌ Shotgun debugging (random changes hoping something works)
- ❌ Skip logs and jump to "fixing"
- ❌ Refactor while debugging
- ❌ Keep adding logs when process is clearly hung → **use GDB thread stacks instead**
- ❌ Blame "pre-existing bugs" without evidence → assume your changes caused it
- ❌ Attribute deadlock to "timing" without GDB evidence showing the lock chain

## Success Criteria

1. Root cause identified from log/GDB evidence
2. Minimal fix applied, tests pass
3. Debug logs removed
4. No new issues introduced

## Case Study: GIL Deadlock via nanobind py_deleter

**Symptom**: Worker process hangs during `remove_object` after FlyStream write. 30s timeout, no crash.

**Failed approaches**:
- Added logs at every step of `remove_object` → showed it entered `request_remove` and blocked on cv
- Guessed mutex contention in DataService → added more mutex logs → inconclusive
- Guessed FlyBuffer lifetime issue → added reset() in finish_write → didn't fix
- Compared old vs new path byte-by-byte → data was identical, mystery deepened

**What worked**: GDB `thread apply all bt` on the hung worker revealed:
```
Thread 7 (reactor):
  DataService::remove_local_index → ObjectCache::remove → unordered_map::erase
  → std::any::~any() → shared_ptr<FlyBuffer>::~shared_ptr()
  → py_deleter → gil_scoped_acquire → PyGILState_Ensure  ← BLOCKED HERE
Thread 3 (task executor):
  request_remove → cv_.wait  ← holding GIL, waiting for RemoveAck from reactor
```

**Root cause**: FlyBufferPtr from Python (via FlyStream.finish()) carried nanobind's `py_deleter`. When `ObjectCache::remove` destructed the cached entry on the reactor thread, the `py_deleter` tried to acquire GIL — but the task executor thread held GIL while waiting for reactor to process RemoveAck. Classic AB-BA deadlock.

**Fix**: `commit_stream` creates a pure-C++ `FlyBuffer` copy (no py_deleter) before passing to `commit_write`.

**Lesson**: When C++ objects cross the Python boundary via nanobind, their `shared_ptr` may carry `py_deleter`. Storing these in C++ containers that are destructed from non-Python threads causes GIL deadlocks. Always strip py_deleter by copying data into a fresh C++ object before long-term storage.
