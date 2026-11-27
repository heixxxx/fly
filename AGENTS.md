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
./qa/runqa                     # Preferred runner (fixed default parallelism, capped; -j N override — 并行度口径见 qa/README.md)
./qa/runqa qa/storage          # Run a single category dir
./qa/runqa qa/storage/test_x.pyt  # Run a single case
bash qa/run_qa_tests.sh        # Legacy wrapper, same thing
```

### Push：agent 的推送工作必须用 `./scripts/push`

**不要直接 `git push`**——pre-push 钩子只核对门禁标记，直接推会被拒绝并提示本节的做法。`./scripts/push`（参数与 `git push` 相同）先跑 **build → unit test → 全量 QA**，全部通过后把提交号写进 `.git/fly-push-marker` 再推送；推送前工作区必须干净（没有未提交、未跟踪的改动）。**禁止 `git push --no-verify`、禁止手写标记文件**。详见 [`docs/push-hook.md`](docs/push-hook.md)。

`runqa` 只发现 `.pyt` 编排脚本（编排脚本是唯一的用例单位）；每个用例在 runqa 进程内 exec，其中的 `run_subcase` 为每个 sub case 起一个全新 `fly` 进程（隔离的 C++ 单例）。裸 `test_*.py` 是子阶段脚本，不被发现也不被运行——必须被某个 `.pyt` 调度（不能按同名判断），显式传 `.py` 路径报错退出（退出码 2），全量运行结束后的引用审计会把没被调度的 `test_*.py` 判为失败。Logs go to `{test_dir}/{test_name}/{N}_{sub_stem}/`. Before each run, `runqa` cleans the case's historical logs (`.N` variants + helper nested logs). Test discovery uses `os.walk` (not glob), skipping `.latest`/`.N` log residues and symlinked dirs so each case is collected exactly once — directory and file args both resolve to canonical real paths.

**QA case 脚本不需要 `sys.path.insert`** — fly 启动时已自动配好所有模块路径。获取 fly binary 路径用 `get_fly_binary()`，不要硬编码 `bazel-bin/...`。

### 开发流程：直接推送到 main（不建分支）

当前处于早期开发阶段，**直接在 `main` 分支提交并推送，不创建 feature 分支**。commit 后用 `./scripts/push` 推送（参数与 `git push` 相同；**agent 的推送工作必须使用该脚本，不要直接 `git push`**）：它先跑 **build → unit test → 全量 QA**，全部通过后写标记文件再推送。不要直接 `git push`——会被 pre-push hook 拒绝。工作区必须干净（没有未提交、未跟踪的改动），`./scripts/push` 才开始跑门禁。待项目进入稳定期再切换为分支模型。

### 推送门禁（`scripts/push` 先跑校验，pre-push hook 只核对标记）

**Agent 的推送工作必须使用 `./scripts/push` 脚本进行**，不要直接 `git push`。门禁 **build → unit test → 全量 QA** 由 `./scripts/push` 在 `git push` **之前**跑，全部通过后把提交号写进 `.git/fly-push-marker`。`.git/hooks/pre-push` 只核对该标记与本次推送的提交是否一致：一致放行，不一致阻止 push 并提示用 `./scripts/push`。门禁不放在钩子里，是因为 `git push` 先连接远端再跑钩子，门禁超过约 6 分钟时连接空闲超时，推送以退出码 141 失败。**禁止以 `git push --no-verify` 或手写标记文件绕过** —— 失败时必须修复根因让流水线自然通过。详见 [`docs/push-hook.md`](docs/push-hook.md)。

### 手动跑 fly 烟测/脚本的日志落点（禁止污染仓库根）

`fly script.py` 不传 `--log-dir` 时默认在 **cwd** 建 `fly_log/`（已存在则 `.N` 递增——每跑一次进程多一个目录）。**不带脚本的调用（例如 `./build/bin/fly --version`）同样会在当前目录建 `fly_log/`**——确认二进制可运行时在 `.work/` 下的空目录里执行，跑完删除。手动烟测必须在 `.work/` 下跑并显式指定：

```bash
./build/bin/fly --log-dir .work/<name>_log .work/<script>.py   # 不要裸跑
```

历史上根目录曾累积 16 个 `fly_log.N`（手动烟测裸跑所致）；同理测试数据目录一律走 `from test import qa_tmp`（不硬编码 `/tmp/fly_*`——曾累积 44334 目录 36G 触发 WSL 磁盘事件，2026-08-29 治理）。

### 设计数据子模块（`designs/`——真实布线设计数据，独立仓库）

用户裁定（2026-11-24）：**设计文件单独用 git 仓库管理，fly 仅作引用。** 用开源工具跑出的已完成布局布线的设计数据（DEF、SPEF、网表、时序窗口、翻转活动等）放在独立仓库里，fly 以 git 子模块引用；fly 仓库自带的只有手写的小型测试设计（`qa/emir/data/`）。理由是体积（约 176 MB）与许可（各设计不同，逐项写明在设计数据仓库里）。

- **首次检出**：新克隆用 `git clone --recurse-submodules`；已有检出用 `git submodule update --init designs`。
- **目录约定**（以下相对数据仓库自己的根；子模块挂在 fly 仓库根的 `designs/`，而数据仓库根下还有一层 `designs/`——所以在 fly 仓库里的实际路径是 `designs/designs/<设计名>/…`）：`designs/<设计名>/<工艺>/<时钟周期>ns/` 下有 `results/`（布局布线产出）、`twf/`（时序窗口）、`vcd/`（翻转活动）、`reports/`、`stats.json`；输入网表与约束在 `designs/inputs/`；各来源许可原文在 `designs/LICENSES/`。规模、已知局限、下游该用哪些文件，见 `designs/README.md`。
- **用例写法**：按路径读取。需要这些数据的用例在子模块未初始化时必须**明确失败并提示上面的命令**，不得静默跳过；存在性检查要放在任何读文件的动作之前（否则抛出的是不带提示的 `FileNotFoundError`）。
- **更新指向**：子模块内容的改动在它自己的仓库里提交推送（复现脚本需要的外部位置经环境变量传入，见其 `scripts/env.sh`）；然后在 fly 仓库根 `git add designs` 提交新的指向。
- 构建与 bazel 不读取它；端到端测试里读它的只有 `qa/emir/test_emir_real_gcd.py`（真实 gcd 设计的四库链用例），它在推送门禁的全量端到端里运行——所以推送前子模块必须已初始化。

---

## Public API Export Chain

When adding a new symbol to `from fly import ...`:

1. **Define** in `src/<module>/py/<file>.py`（不加 `_` 前缀，允许 `import *` 导出）
2. **级联 re-export**：`src/<module>/py/__init__.py` 用 `from .<file> import *`，`src/<module>/__init__.py` 用 `from <module>.py import *`
3. **Import in `src/fly/__init__.py`**：裸 `from <module> import <symbol>`（包根导入，两种布局统一）

```python
# src/task/py/__init__.py
from .task import *

# src/task/__init__.py
from task.py import *  # noqa: F401,F403

# src/fly/__init__.py
from task import as_task, task_name, wait_obj
```

**禁止** `__all__`、`try/except` 双布局 import、`from <module>.py.xxx import`。详见 [`CLAUDE.md`](CLAUDE.md) §3「Python 包布局与 import 规范」。

---

## New C++ Module Registration

1. `src/<module>/cpp/BUILD`: `cc_library` + `cc_shared_library`
2. `src/<module>/export/BUILD`: `cc_binary(linkshared=True)` with `dynamic_deps`
3. `src/main/cpp/BUILD`: add to `deps` + `dynamic_deps` of `fly` target
4. `src/main/cpp/main.cpp`: add to `setup_sys_path()` + `import _fly_<module>`
5. `fly.sh` `do_install()`: add `<module>` to the unified `for mod in ...` install loop
6. Run `./fly.sh install` and verify with `./build/bin/fly`

Full guide: [`docs/NEW_MODULE_GUIDE.md`](docs/NEW_MODULE_GUIDE.md)

---

## LSP False Positives

These clangd errors are **not real** — they come from Bazel's virtual include paths. Build with `./fly.sh` to verify before investigating:

| LSP Error | Cause |
|-----------|-------|
| `common/runtime/cpp/writer_id.h file not found` | Virtual includes path |
| `No template named 'remove_cvref_t' in namespace 'fmt'` | clangd parsing bazel virtual headers |
| `Import "_fly_*" could not be resolved` | nanobind dynamic .so |
| `Cannot access attribute "..." for class "FlyAgent"` | Abstract class, concrete methods on subclasses |

---

## Language

- **使用中文进行思考和回复** — 所有与用户的交互使用中文
- **禁止缩写与简写** — 回答用户问题和写文档时，一律使用完整、清晰的名称描述，不使用缩写、简写或自造代号。例：写 ThreadSanitizer（线程竞态检测器）而非 TSAN、写 WriteBackQueue（写回队列）而非 WBQ、写「正常退出处理」而非用裸函数名指代概念。例外：代码引用（文件名、函数名、commit 号）本身是精确定位，可以原样出现；但用代码符号指代概念时，必须附带一句话说明它是什么。

---

## Key Constraints

- **Never use `bazel build` directly** — always `./fly.sh build` (keeps clangd working)
- **TDD**: write test → implement → verify tests pass → commit
- **Zero tolerance for flaky tests** — no `sleep(); assert()`, no deleting failing tests
- **No `@ts-ignore`/`as any` equivalents** — fix the root cause
- **C++20 / gcc12** — use `CMString`, `CMVector`, etc. from `container/cpp/container_aliases.h`（容器别名，可替换层；智能指针族在 `common/types/cpp/pointer_aliases.h`，前者已聚合后者）
- **Module-style includes**: `<module/cpp/file.h>` NOT `"../cpp/file.h"`
- **Macros over raw APIs**: use `FLY_SERIALIZE_*` not bitsery, `FLY_EXPORT_*` not nanobind
- **Must pass full test suite before committing** — cpp/python unit tests + QA tests must ALL pass, ZERO failures allowed. No exceptions, no "pre-existing" excuses. If a test fails, fix it before committing.
- **Never blame "pre-existing bugs"** — all crashes/instability are assumed from your changes
- **Fix crashes immediately** — no deferring, no marking as "known issues"
- **Never `rm -rf qa/*/test_*` or any `test_*` glob in qa/** — the glob matches both `test_x.py` source files and `test_x/` log dirs, routinely deleting test sources. Clean qa/ with `git clean -fd qa/` (untracked only) or by precise paths. Put useful non-test resources in dedicated subdirs (e.g. `qa/solver/matrices/`), never loose under test dirs.
- **Python 跨模块 import**：一律 `from module import symbol`（包根 re-export），禁止 `from module.py.xxx import`。`_` 前缀仅限完全确定模块内部使用的符号；其余一律不加前缀允许 `import *` 导出。禁止 `__all__`。详见 CLAUDE.md §3「Python 包布局与 import 规范」。
- **定位 runqa 失败**：读 `qa/logs/qa.log`（含失败详情 + fly.log 路径），不要反复重跑覆盖 fly.log 丢失现场。
- **并发封装优先**：多线程共享容器用 `ConcurrentMap`/`ConcurrentUnorderedSet`，pending 状态机（登记→等完成→消费）用 `PendingRpcMap`，禁止新增裸 mutex+容器成员对；cv notify 必须持锁。详见 `docs/DEVELOPMENT_GUIDELINES.md` §13。
- **非必须场景禁止裸指针**：跨对象/跨线程生命周期的引用一律用智能指针（`CMSharedPtr`/`CMUniquePtr`）；裸指针仅限非拥有观察与 Python 绑定所有权转移。案例：writer 持 server 裸指针 + `stop_peer_rpc()` reset → 在途发送 SIGSEGV。详见 `docs/DEVELOPMENT_GUIDELINES.md` §16。
- **禁止使用 `/tmp` 存放中间任务数据** —— 中间文件放在项目 `.work/` 目录下，任务结束前 `rm -rf .work/` 清理。`/tmp` 无限累积会填满磁盘导致 WSL2 崩溃。

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

- **一次只跑一轮 runqa**，失败后**立即停下来看日志**。绝不在失败后继续跑下一轮——runqa 的 `_clean_case_logs`（由 `run_pyt` 在每次运行前调用）会清理该用例的历史日志（子阶段 `fly.log` 在 `{test_dir}/{test_name}/{N}_{子阶段名}/` 下），下一轮会覆盖失败轮的日志，导致**永久丢失失败现场**。
- **多轮稳定性测试用 `-j4` 跑一次**（全量 case 一次跑完，数量以 runqa 运行输出为准），而非 for 循环跑多轮。要验证多轮稳定性，每轮之间必须**检查是否有失败**，有失败则立即停止分析，不得继续。
- **失败日志位置**：`qa/{类别}/{用例名}/{N}_{子阶段名}/fly.log`（stdout+stderr 合并），`master.log`、`worker{N}.log` 在同一个子阶段目录里。这些是分析 timeout/fail 的唯一现场。
- **runqa 的 `qa/logs/qa.log`** 每轮重建（覆盖），但 per-case 的 `fly.log` 只被同 case 下一轮覆盖。
- **基准重录是显式操作**：`./qa/runqa --regold <用例.pyt>` 用上次运行的结果文件覆盖基准文件（`qa/<类别>/gold/` 下的 `.gold` 文件），只在开发者判定差异合理之后执行；推送前校验不调用它，运行用例时也绝不写基准。重录后必须重跑确认通过，基准文件的变更随提交评审。迁移用例时先保留原有断言并行加 `record`，运行确认断言全部通过之后再重录初始基准——直接删断言再重录会把实现缺陷固化成期望值。结果文件顶层布尔为假的条目，重录会被拒绝（判定型条目期望恒为 true）。规则见 `qa/README.md`「基准比对机制」。

## Config vs ProcessInfo

- **Config**: shared across all processes, synced by master before starting workers. heartbeat/backup/compression/log_dir + **master 寻址键**（`master_host`/`master_advertise_host`/`master_port`——master 写入 `.fly_config`，local/ssh/bsub worker 统一凭 `--config-file` 引导，首写完备 + 原子写；权威说明见 `docs/core/module.md`）
- **ProcessInfo**: per-process, never synced. worker_mode, worker_id, master_host/port（worker 进程运行视角，由 main.cpp 从 CLI/config 兑现）, hostname
- **`--host` CLI flag**: overrides ProcessInfo hostname, for single-machine multi-host testing
- **`--master-host/--master-port` CLI flags**: 调试覆盖口（优先级最高）——正常场景 worker 寻址只走 `.fly_config`
- **log_dir** lives in Config (all processes share the same log directory)

---

## Module Map

```
src/storage/    → Layer 1: Database, DataService, DataWriter, DataReader, CompressingStreamBuf, ObjectCache (单层 LRU 读缓存), DataServer (epoll+线程池)
src/network/    → Layer 2: Transport + EpollMultiplexer + ConnectionManager 抽象, Reactor, MessageProtocol + DataResponseProtocol (两段式), DataClientPool
                  (消息类型语义全表: docs/network/module.md「消息类型总表」)
src/task/       → Layer 3: DependencyGraph, TaskScheduler, WorkerManager
src/agent/      → Layer 4: MasterAgent, WorkerAgent, TaskExecutor, PeerRpcServer
src/core/       → Config (shared), ProcessInfo (per-process)
src/fly/        → Layer 5: Python public API (公开符号权威总表: docs/python-api/module.md)
src/solver/     → Layer 6: Distributed RAS solver (C++ core + Python orchestration)
src/monitor/    → cluster monitor: 采集落盘 (MetricsDb 单写 monitor.db, 与心跳解耦的
                  MONITOR_SAMPLE 通道) + Web GUI (serve.py + ECharts, fly --serve-monitor)
src/message/    → 消息日志系统: 高价值日志推送/配额/终端唯一透出 (docs/message-system.md)
src/common/     → 模块族: types(指针别名) buffer(FlyBuffer+data_checksum) concurrent io(FdHandle+ChunkSource) runtime(WriterID等) testing serialization(序列化宏+对象头)
src/container/  → 容器别名层(CMVector/CMMap/CMString…, 可整体替换底层实现) + 自定义容器(CMLookupTable…); EMIR 业务模块族(src/emir: EMIRProject + lib/design 等 db 子模块)亦按此模式
src/geometry/   → 顶层独立模块: 纯几何结构(GEOPointT/GEORectT/GEOPolygonT 模板+无后缀 int32 别名) + GEOOrientation/GEOTransformT(放置变换 D4 八方向, 枚举值=Si2 DEF_ORIENT_*; design db 坐标 int32 裁定)
src/log/        → DBG/INFO/WARN/ERR macros, CM_FORMAT_CLASS/ENUM
src/test/       → TestObject, e2e_tasks.py, test_tasks.py (not public API)
```

---

## Key Documentation Files

| File | Content |
|------|---------|
| [`docs/README.md`](docs/README.md) | **文档地图** — 按用途的快速入口与全量索引 |
| [`CLAUDE.md`](CLAUDE.md) | Primary agent guide — conventions, macros, design constraints |
| [`docs/DEVELOPMENT_GUIDELINES.md`](docs/DEVELOPMENT_GUIDELINES.md) | Code standards, naming, macro reference |
| [`docs/NEW_MODULE_GUIDE.md`](docs/NEW_MODULE_GUIDE.md) | Step-by-step new module creation |
| [`docs/architecture.md`](docs/architecture.md) | System architecture, data flow, thread model |
| [`docs/matrix-solver-analysis.md`](docs/matrix-solver-analysis.md) | RAS algorithm analysis, convergence theory |
| [`qa/README.md`](qa/README.md) | QA test framework and conventions |
| `big_qa/` | Large matrix solver tests (n≥1000), not run in regular QA |
