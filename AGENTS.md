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
./qa/runqa                     # Preferred runner (default 4 parallel, capped at 32; -j N override)
./qa/runqa qa/storage          # Run a single category dir
./qa/runqa qa/storage/test_x.py  # Run a single case
bash qa/run_qa_tests.sh        # Legacy wrapper, same thing
```

`runqa` launches each `test_*.py` in a fresh `fly` process with isolated C++ singletons. Logs go to `{test_dir}/{test_name}/`. Before each run, `runqa` cleans the test's historical logs (`.N` variants + helper nested logs). Test discovery uses `os.walk` (not glob), skipping `.latest`/`.N` log residues and symlinked dirs so each case is collected exactly once — directory and file args both resolve to canonical real paths.

**QA case 脚本不需要 `sys.path.insert`** — fly 启动时已自动配好所有模块路径。获取 fly binary 路径用 `get_fly_binary()`，不要硬编码 `bazel-bin/...`。

### 开发流程：直接推送到 main（不建分支）

当前处于早期开发阶段，**直接在 `main` 分支提交并推送，不创建 feature 分支**。commit 后直接 `git push`（pre-push hook 自动跑全量校验）。待项目进入稳定期再切换为分支模型。

### Pre-push hook（push 前自动校验）

`.git/hooks/pre-push` 在每次 `git push` 前自动跑 **build → unit test → 全量 QA**，任一阶段失败即阻止 push。**禁止以 `git push --no-verify` 绕过** —— 失败时必须修复根因让流水线自然通过。详见 [`docs/push-hook.md`](docs/push-hook.md)。

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
- **Must pass full test suite before committing** — cpp/python unit tests + QA tests must ALL pass, ZERO failures allowed. No exceptions, no "pre-existing" excuses. If a test fails, fix it before committing.
- **Never blame "pre-existing bugs"** — all crashes/instability are assumed from your changes
- **Fix crashes immediately** — no deferring, no marking as "known issues"
- **Never `rm -rf qa/*/test_*` or any `test_*` glob in qa/** — the glob matches both `test_x.py` source files and `test_x/` log dirs, routinely deleting test sources. Clean qa/ with `git clean -fd qa/` (untracked only) or by precise paths. Put useful non-test resources in dedicated subdirs (e.g. `qa/solver/matrices/`), never loose under test dirs.
- **Python 跨模块 import**：一律 `from module import symbol`（包根 re-export），禁止 `from module.py.xxx import`。`_` 前缀仅限完全确定模块内部使用的符号；其余一律不加前缀允许 `import *` 导出。禁止 `__all__`。详见 CLAUDE.md §3「Python 包布局与 import 规范」。
- **定位 runqa 失败**：读 `qa/logs/qa.log`（含失败详情 + fly.log 路径），不要反复重跑覆盖 fly.log 丢失现场。

## Stability: Zero Tolerance

- All crashes (SIGSEGV, SIGABRT) → fix immediately, never mark "known issue"
- All flaky tests → fix immediately, never increase timeout or add retries
- Stability tests (50+ rounds via `stability_test.sh`) must be 100% pass rate

## Debugging Rules

- **Add logs, don't guess** — instrument at decision points with DBG, run, observe
- **Repro scripts must stop-on-first-failure** — don't run all rounds then check
- **Repro scripts must have timeout** — prevent hangs eating your whole night
- **Load `/systematic-debugging-analysis` skill first** — follow its workflow

### QA Debug 日志保护规则（必须遵守）

- **一次只跑一轮 runqa**，失败后**立即停下来看日志**。绝不在失败后继续跑下一轮——runqa 的 `run_one` 在每次运行前会清理该 case 的历史日志（`{test_dir}/{test_name}/fly.log`），下一轮会覆盖失败轮的日志，导致**永久丢失失败现场**。
- **多轮稳定性测试用 `-j4` 跑一次**（147 个 case 一次跑完），而非 for 循环跑多轮。要验证多轮稳定性，每轮之间必须**检查是否有失败**，有失败则立即停止分析，不得继续。
- **失败日志位置**：`qa/{category}/{test_name}/fly.log`（stdout+stderr 合并）、`master.log`、`worker1.log` 等。这些是分析 timeout/fail 的唯一现场。
- **runqa 的 `qa/logs/qa.log`** 每轮重建（覆盖），但 per-case 的 `fly.log` 只被同 case 下一轮覆盖。

## Config vs ProcessInfo

- **Config**: shared across all processes, synced by master before starting workers. heartbeat/backup/compression/log_dir
- **ProcessInfo**: per-process, never synced. worker_mode, worker_id, master_host/port, hostname
- **`--host` CLI flag**: overrides ProcessInfo hostname, for single-machine multi-host testing
- **log_dir** lives in Config (all processes share the same log directory)

---

## Module Map

```
src/storage/    → Layer 1: Database, DataService, DataWriter, DataReader, CompressingStreamBuf, ObjectCache (两层 LRU), DataServer (epoll+线程池)
src/network/    → Layer 2: Transport + EpollMultiplexer + ConnectionManager 抽象, Reactor, MessageProtocol + DataResponseProtocol (两段式), DataClientPool, 33 msg types
src/task/       → Layer 3: DependencyGraph, TaskScheduler, WorkerManager
src/agent/      → Layer 4: MasterAgent, WorkerAgent, TaskExecutor
src/core/       → Config (shared), ProcessInfo (per-process)
src/fly/        → Layer 5: Python public API (__init__.py, runtime.py, mapreduce.py)
src/solver/     → Layer 6: Distributed RAS solver (C++ core + Python orchestration)
src/common/     → CM* type aliases (CMSharedPtr, CMString, CMVector…), FlyBuffer, FlyBufferPtr, WriterID, ErrorTypes
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
