# Fly — Agent Quick Reference

> **Primary agent guide**: [`CLAUDE.md`](CLAUDE.md) — read it first. This file adds gotchas and patterns that took reading multiple files to infer.

---

## Non-obvious Gotchas

### fly.sh build EXIT CODE IS MISLEADING

```bash
./fly.sh build //src/main/cpp:fly
# May print "ERROR: Build did NOT complete successfully" at the end
# IGNORE this — it's the refresh_compile_commands step failing, NOT the actual build.
# Check for "Build completed successfully" earlier in the output.
# The binary is in bazel-bin/src/main/cpp/fly regardless.
```

Workaround: use `./fly.sh buildonly` to skip clangd refresh entirely.

### Must install before QA tests

```bash
./fly.sh build //src/main/cpp:fly
./fly.sh install          # Creates build/ with symlinks
bash qa/run_qa_tests.sh   # QA tests depend on build/ layout
```

### Adding to the `fly` public API

When adding a new symbol (decorator, function, class) to `from fly import ...`:

1. **Define it** in `src/task/py/task.py` (or appropriate module)
2. **Export it** from `src/task/py/__init__.py` (add to `__all__`)
3. **Export it** from `src/task/__init__.py` (Bazel runfiles compat, try/except)
4. **Import it** in `src/fly/__init__.py` (try/except for both path layouts)
5. **Add to `__all__`** in `src/fly/__init__.py`

The try/except pattern handles two Python path layouts:
```python
try:
    from task.task import as_task        # Bazel runfiles (task/__init__.py re-exports)
except ImportError:
    from task.py.task import as_task     # Direct Python (standard package path)
```

### New C++ module registration checklist

When adding a new module under `src/<module>/`:
1. `src/<module>/cpp/BUILD`: `cc_library` + `cc_shared_library`
2. `src/<module>/export/BUILD`: `cc_binary(linkshared=True)` with `dynamic_deps`
3. `src/main/cpp/BUILD`: add to `deps` + `dynamic_deps` of `fly` target
4. `src/main/cpp/main.cpp`: add to `setup_sys_path()` + `import _fly_<module>`
5. `fly.sh` `do_install()`: add `mkdir` + `symlink` for `build/python/<module>/`
6. Run `./fly.sh install` and verify with `./build/bin/fly`

Full guide: [`docs/NEW_MODULE_GUIDE.md`](docs/NEW_MODULE_GUIDE.md)

---

## LSP False Positives (always ignore)

These clangd errors are **not real** — they come from Bazel's virtual include paths. Build with `./fly.sh` to verify:

| LSP Error | Cause |
|-----------|-------|
| `common/cpp/writer_id.h file not found` | Virtual includes path |
| `No template named 'remove_cvref_t' in namespace 'fmt'` | clangd parsing bazel virtual headers |
| `Import "_fly_*" could not be resolved` | nanobind dynamic .so |
| `Cannot access attribute "..." for class "FlyAgent"` | Abstract class, concrete methods on subclasses |

---

## Build & Test Quick Reference

```bash
./fly.sh build [target...]     # Build + refresh clangd
./fly.sh test [target...]      # Test (no clangd refresh)
./fly.sh buildonly [target...] # Build only, skip clangd
./fly.sh check                 # Build + test + clangd refresh
./fly.sh install               # Symlink bazel-bin → build/

# Unit tests (gtest + pytest)
./fly.sh test //src/...

# Single test file
./fly.sh test //src/task/tests:dependency_graph_test

# QA integration tests (must install first)
./fly.sh build //src/main/cpp:fly && ./fly.sh install
bash qa/run_qa_tests.sh
```

---

## Key Constraints

- **Never use `bazel build` directly** — always `./fly.sh build` (keeps clangd working)
- **TDD**: write test → implement → verify tests pass → commit
- **Zero tolerance for flaky tests** — no `sleep(); assert()`, no deleting failing tests
- **No `@ts-ignore`/`as any` equivalents** — fix the root cause
- **C++20 / gcc12** — use `CMString`, `CMVector`, etc. from `common_types.h`
- **Module-style includes**: `<module/cpp/file.h>` NOT `"../cpp/file.h"`
- **Macros over raw APIs**: use `FLY_SERIALIZE_*` not bitsery, `FLY_EXPORT_*` not nanobind

---

## Module Map

```
src/storage/    → Layer 1: Database, DataService, DataWriter, DataReader
src/network/    → Layer 2: Reactor, TCP transport, message protocol (27 msg types)
src/task/       → Layer 3: DependencyGraph, TaskScheduler, WorkerManager
src/agent/      → Layer 4: MasterAgent, WorkerAgent, TaskExecutor
src/fly/        → Layer 5: Python public API (__init__.py, runtime.py)
src/common/     → CM* type aliases (CMString, CMVector…)
src/log/        → DBG/INFO/WARN/ERR macros, CM_FORMAT_CLASS/ENUM
src/test/       → TestObject, e2e_tasks.py, test_tasks.py (not public API)
```

---

## Key Documentation Files

| File | Content |
|------|---------|
| [`CLAUDE.md`](CLAUDE.md) | Primary agent guide — conventions, macros, design constraints |
| [`docs/DEVELOPMENT_GUIDELINES.md`](docs/DEVELOPMENT_GUIDELINES.md) | Code standards, naming, macro reference (768 lines) |
| [`docs/NEW_MODULE_GUIDE.md`](docs/NEW_MODULE_GUIDE.md) | Step-by-step new module creation (439 lines) |
| [`docs/architecture.md`](docs/architecture.md) | System architecture, data flow, thread model (630 lines) |
| [`qa/README.md`](qa/README.md) | QA test framework and conventions |
