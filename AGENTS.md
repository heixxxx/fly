# Fly — Agent Quick Reference

> **Primary agent guide**: [`CLAUDE.md`](CLAUDE.md) — read it first. This file adds gotchas and patterns that took reading multiple files to infer.

---

## Build & Test

**Always use `./fly.sh`, never raw `bazel`.** Direct `bazel build` doesn't refresh `compile_commands.json`, breaking clangd.

```bash
./fly.sh build [target...]     # Build + refresh clangd
./fly.sh test [target...]      # Test + refresh clangd
./fly.sh buildonly [target...] # Build only, skip clangd
./fly.sh refresh               # Refresh compile_commands.json only
./fly.sh check                 # Build + test + clangd refresh
./fly.sh install               # Create build/ with symlinks to bazel-bin
```

### QA tests require install first

```bash
./fly.sh build //src/main/cpp:fly
./fly.sh install               # Creates build/ with symlinks
./qa/runqa                     # Preferred runner (supports -j concurrency)
bash qa/run_qa_tests.sh        # Legacy wrapper, same thing
```

`runqa` launches each `test_*.py` in a fresh `fly` process with isolated C++ singletons. Logs go to `qa/logs/`.

---

## Public API Export Chain

When adding a new symbol to `from fly import ...`:

1. **Define** in `src/task/py/task.py` (or appropriate module)
2. **Export** from `src/task/py/__init__.py` (add to `__all__`)
3. **Export** from `src/task/__init__.py` (Bazel runfiles compat, try/except)
4. **Import** in `src/fly/__init__.py` (try/except for both path layouts)
5. **Add to `__all__`** in `src/fly/__init__.py`

```python
try:
    from task.task import as_task        # Bazel runfiles (task/__init__.py re-exports)
except ImportError:
    from task.py.task import as_task     # Direct Python (standard package path)
```

---

## New C++ Module Registration

1. `src/<module>/cpp/BUILD`: `cc_library` + `cc_shared_library`
2. `src/<module>/export/BUILD`: `cc_binary(linkshared=True)` with `dynamic_deps`
3. `src/main/cpp/BUILD`: add to `deps` + `dynamic_deps` of `fly` target
4. `src/main/cpp/main.cpp`: add to `setup_sys_path()` + `import _fly_<module>`
5. `fly.sh` `do_install()`: add `mkdir` + `symlink` for `build/python/<module>/`
6. Run `./fly.sh install` and verify with `./build/bin/fly`

Full guide: [`docs/NEW_MODULE_GUIDE.md`](docs/NEW_MODULE_GUIDE.md)

---

## LSP False Positives

These clangd errors are **not real** — they come from Bazel's virtual include paths. Build with `./fly.sh` to verify before investigating:

| LSP Error | Cause |
|-----------|-------|
| `common/cpp/writer_id.h file not found` | Virtual includes path |
| `No template named 'remove_cvref_t' in namespace 'fmt'` | clangd parsing bazel virtual headers |
| `Import "_fly_*" could not be resolved` | nanobind dynamic .so |
| `Cannot access attribute "..." for class "FlyAgent"` | Abstract class, concrete methods on subclasses |

---

## Language

- **使用中文进行思考和回复** — 所有与用户的交互使用中文

---

## Key Constraints

- **Never use `bazel build` directly** — always `./fly.sh build` (keeps clangd working)
- **TDD**: write test → implement → verify tests pass → commit
- **Zero tolerance for flaky tests** — no `sleep(); assert()`, no deleting failing tests
- **No `@ts-ignore`/`as any` equivalents** — fix the root cause
- **C++20 / gcc12** — use `CMString`, `CMVector`, etc. from `common/cpp/common_types.h`
- **Module-style includes**: `<module/cpp/file.h>` NOT `"../cpp/file.h"`
- **Macros over raw APIs**: use `FLY_SERIALIZE_*` not bitsery, `FLY_EXPORT_*` not nanobind
- **Must pass full test suite before completing** — cpp/python unit tests + QA tests must all pass
- **Never blame "pre-existing bugs"** — all crashes/instability are assumed from your changes
- **Fix crashes immediately** — no deferring, no marking as "known issues"

## Stability: Zero Tolerance

- All crashes (SIGSEGV, SIGABRT) → fix immediately, never mark "known issue"
- All flaky tests → fix immediately, never increase timeout or add retries
- Stability tests (50+ rounds via `stability_test.sh`) must be 100% pass rate

## Debugging Rules

- **Add logs, don't guess** — instrument at decision points with DBG, run, observe
- **Repro scripts must stop-on-first-failure** — don't run all rounds then check
- **Repro scripts must have timeout** — prevent hangs eating your whole night
- **Load `/systematic-debugging-analysis` skill first** — follow its workflow

## Config vs ProcessInfo

- **Config**: shared across all processes, synced by master before starting workers. heartbeat/backup/compression/log_dir
- **ProcessInfo**: per-process, never synced. worker_mode, worker_id, master_host/port, hostname
- **`--host` CLI flag**: overrides ProcessInfo hostname, for single-machine multi-host testing
- **log_dir** lives in Config (all processes share the same log directory)

---

## Module Map

```
src/storage/    → Layer 1: Database, DataService, DataWriter, DataReader, CompressingStreamBuf
src/network/    → Layer 2: Reactor, TCP transport, message protocol (27 msg types + IdxLoad)
src/task/       → Layer 3: DependencyGraph, TaskScheduler, WorkerManager
src/agent/      → Layer 4: MasterAgent, WorkerAgent, TaskExecutor
src/core/       → Config (shared), ProcessInfo (per-process)
src/fly/        → Layer 5: Python public API (__init__.py, runtime.py, mapreduce.py)
src/solver/     → Layer 6: Distributed RAS solver (C++ core + Python orchestration)
src/common/     → CM* type aliases (CMString, CMVector…)
src/log/        → DBG/INFO/WARN/ERR macros, CM_FORMAT_CLASS/ENUM
src/test/       → TestObject, e2e_tasks.py, test_tasks.py (not public API)
```

---

## Key Documentation Files

| File | Content |
|------|---------|
| [`CLAUDE.md`](CLAUDE.md) | Primary agent guide — conventions, macros, design constraints |
| [`docs/DEVELOPMENT_GUIDELINES.md`](docs/DEVELOPMENT_GUIDELINES.md) | Code standards, naming, macro reference |
| [`docs/NEW_MODULE_GUIDE.md`](docs/NEW_MODULE_GUIDE.md) | Step-by-step new module creation |
| [`docs/architecture.md`](docs/architecture.md) | System architecture, data flow, thread model |
| [`docs/matrix-solver-analysis.md`](docs/matrix-solver-analysis.md) | RAS algorithm analysis, convergence theory |
| [`qa/README.md`](qa/README.md) | QA test framework and conventions |
| `big_qa/` | Large matrix solver tests (n≥1000), not run in regular QA |


<!-- open-mem-context -->
## Project Activity (auto-generated by open-mem)

### ./
| ID | Type | Title | Date |
|----|------|-------|------|
| 9fa1e7be-522c-4e4a-bf61-e7ab72bf43b5 | 🔵 discovery | open-mem redaction fails on .bashrc-exported Gemini API key | 2026-06-13 |
| 5b85579f-20c2-418e-9427-b5d886b993e3 | 🔵 discovery | opencode.json registers open-mem and @opentrace/opencode as active plugins | 2026-06-13 |
| 38450c87-b28a-4b4c-b67b-ab4dff2b4282 | 🟣 feature | OpenTrace plugin installation completed via 3-step todo | 2026-06-13 |
| b7e388ec-7c2b-4aef-9bfc-1dbcf7c0b00f | 🟣 feature | OpenTrace plugin installation completed via 3-step todo | 2026-06-13 |
| 312d54c5-62b3-4768-81ac-5d8ec09d0c76 | 🟣 feature | open-mem plugin installation completed via 3-step todo | 2026-06-13 |
| 08798531-523d-4eb5-bf07-f85bddf5b6f6 | 🔵 discovery | open-mem plugin stores per-project SQLite memory in .open-mem/memory.db | 2026-06-13 |

**Key concepts:** gotcha, plaintext-secret-exposure, redaction-bypass, credential-leak-in-memory-store, how-it-works, plugin-registration, adapter-pattern, what-changed, pattern, installation-workflow

### .opentrace/
| ID | Type | Title | Date |
|----|------|-------|------|
| 120e7104-d07c-48b0-a548-33f0084603be | 🟣 feature | OpenTrace index.db promoted to production at 16MB | 2026-06-13 |
| da11a4e9-e146-4c65-87f6-624cb9aa3acf | 🟣 feature | OpenTrace indexed fly repo: 4359 nodes, 8030 relationships in 31.5s | 2026-06-13 |

**Key concepts:** what-changed, how-it-works, knowledge-graph, atomic-promotion, sqlite-persistence, staging-database, pattern, code-intelligence

💡 *Use `mem-find` to search full details. Use `mem-create` to save important decisions.*
<!-- /open-mem-context -->
