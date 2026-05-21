---
name: systematic-debugging-analysis
description: Multi-dimensional debugging combining code flow, architecture, logs, and runtime results. NEVER guess — instrument first with logs at every decision point until exact failure is visible. Use for distributed system issues, concurrency bugs, network communication problems, cross-language (C++/Python) boundaries, mysterious failures, NameError, ImportError, or when logs are missing for the failure path.
---

# Systematic Debugging

## Core Principle

**Don't guess. Add logs. Run. Observe.**

Guessing wastes hours. Instrumented logs find bugs in minutes.

## Workflow

### Phase 1: Reproduce & Classify

```
1. Reproduce the failure (deterministic test case)
2. Classify:
   - Component: Master / Worker / Network / Storage / Task
   - Type: Network / Memory / Logic / Concurrency
   - Layer: C++ / Python / C++↔Python boundary
```

### Phase 2: Instrument First (MANDATORY)

Before any code change or hypothesis, add logs:

```
1. Entry/exit logs on all involved functions
2. Value logs at every decision point (if/switch/branch)
3. Import/load logs at module boundaries
4. State logs before and after mutations

Example:
  void MasterAgent::assign_task(uint64_t task_id, uint64_t worker_id) {
      LOG("assign_task START: task=" + task_id + " worker=" + worker_id);
      LOG("worker state: " + worker_manager_->get_worker_state(worker_id));
      ... original logic ...
      LOG("assign_task END: success");
  }
```

**Run → last successful log = exact failure location.**

### Phase 3: Multi-Dimensional Analysis

If logs alone don't reveal root cause, expand dimensions:

| Dimension | What to check |
|-----------|--------------|
| **Code flow** | Call chain, data flow, lifecycle |
| **Logs** | Master log, Worker logs, timestamps, operation sequence |
| **Architecture** | Master↔Worker interaction, message flow, data paths |
| **Runtime** | Thread safety, resource state, timing issues |

### Phase 4: Fix Root Cause

```
1. Identify from log evidence (not speculation)
2. Minimal fix — no refactoring
3. Run tests → verify
4. Remove debug logs
```

## When to Use

- Any mysterious failure ("code should work but doesn't")
- NameError / ImportError / undefined variable
- Multi-language boundaries (Python ↔ C++)
- Distributed system issues (Master↔Worker communication)
- Concurrency bugs, race conditions
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

## Success Criteria

1. Root cause identified from log evidence
2. Minimal fix applied, tests pass
3. Debug logs removed
4. No new issues introduced
