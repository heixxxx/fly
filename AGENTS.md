# Fly — Agent Quick Reference

> **Primary agent guide**: [`CLAUDE.md`](CLAUDE.md) — read it first. This file adds gotchas and patterns that took reading multiple files to infer.

---

## 构建与测试

**必须使用 `./fly.sh` 而非裸 `bazel` 命令！** 直接使用 `bazel build` 不会刷新 `compile_commands.json`，导致 clangd 无法工作。

```bash
./fly.sh build [target...]     # 构建 + 刷新 clangd
./fly.sh test [target...]      # 测试 + 刷新 clangd
./fly.sh buildonly [target...] # 仅构建，不刷新
./fly.sh refresh               # 仅刷新 clangd
./fly.sh check                 # 构建 + 测试 + 刷新
./fly.sh install               # 创建 build/ 目录，symlink 到 bazel-bin 产物

# 单元测试
./fly.sh test //src/...

# QA 测试（需先构建并安装）
./fly.sh build //src/main/cpp:fly
./fly.sh install
bash qa/run_qa_tests.sh
```

## Non-obvious Gotchas

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
- **必须通过全量测试**: 所有新增代码，必须在完成后通过全量测试(cpp/python unittests， qa tests)，才可算完成，需要保证下一次的开发时，不存在任何未通过的测试
- **禁止归因为"之前代码就存在的问题"**：所有 crash 和不稳定问题一定为本次代码修改引入的，不得以"pre-existing bug"为由跳过或忽略
- **禁止忽略任何 crash 和不稳定问题**：发现的第一时间必须修复，不允许搁置或推迟

## 崩溃与不稳定性零容忍

- 所有 crash（SIGSEGV、SIGABRT 等）必须立即修复，不得标记为"已知问题"
- 所有间歇性失败（flaky test）必须立即修复，不得提高超时或增加重试
- 稳定性测试（50 轮以上）必须 100% 通过，任何一轮失败都是必须修复的 bug

## Config vs ProcessInfo

- **Config**：所有进程共享，master 通过 ConfigSyncMessage 自动同步给 worker。包含 heartbeat/backup/compression/log_dir 等配置
- **ProcessInfo**：进程私有，不同步。包含 worker_mode, worker_id, master_host/port, hostname 等
- **`--host` CLI**：覆盖 ProcessInfo 的 hostname，用于单机模拟多 host 测试
- **log_dir** 在 Config 中（所有进程共享同一日志目录）
---

## Module Map

```
src/storage/    → Layer 1: Database, DataService, DataWriter (纯落盘), DataReader (纯读字节), CompressingStreamBuf, DecompressingStreamBuf
src/network/    → Layer 2: Reactor, TCP transport, message protocol (27 msg types + ConfigSync + IdxLoad)
src/task/       → Layer 3: DependencyGraph, TaskScheduler, WorkerManager
src/agent/      → Layer 4: MasterAgent, WorkerAgent, TaskExecutor
src/core/       → Config (共享配置), ProcessInfo (进程私有数据, hostname)
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
