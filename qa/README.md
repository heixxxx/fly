# Fly QA 测试指南

## 1. 运行测试

```bash
# 构建fly二进制
./fly.sh build //src/main/cpp:fly

# 运行全部QA测试（-j 并发，-t 超时秒数）
./qa/runqa -j 6 -t 40

# 运行指定测试
./qa/runqa qa/storage/test_read_cache_basic.pyt

# 兼容旧入口
./qa/run_qa_tests.sh
```

> **并行度权威口径**（其他文档只链接此处，不复制数字）：
> 默认并行度为**固定值**（2026-08-16 用户裁定：不随核数自适应；当前值见 `qa/runqa` 的
> `_resolve_default_parallelism()`，以源码为准），上限钳制、`-j 0`/负数行为同见源码。显式 `-j N` 可覆盖。
> runqa 以 `os.walk` 递归发现 `*.pyt` 编排脚本（跳过 `.latest`/`.N` 日志残留与符号链接目录）。
> **编排脚本（.pyt）是唯一的用例单位**：裸 `test_*.py` 不被发现也不被运行，必须被某个
> `.pyt` 调度（全量运行结束后有引用审计兜底，详见下文）；显式传入 `.py` 路径报错退出（退出码 2）。
> **case 总数随开发持续增长，任何文档不写死具体数字**，以 `./qa/runqa` 运行输出为准。

### 测试运行机制

`runqa`（Python 运行器）对每个用例（一个 `.pyt` 编排脚本）：

1. 清理该用例的历史日志（`.N` 变体 + helper 嵌套 log）
2. 在 runqa 进程内 `exec` 编排脚本；脚本里的每个 `run_subcase` 调用启动一个
   `fly --log-dir <dir> <脚本>` **独立子进程**跑一个子阶段（`cwd` 设为用例所在目录）
3. fly 二进制初始化全新的 C++ 运行时（DataService、StorageManager 等单例都是全新的）
4. 执行子阶段 Python 脚本
5. 通过进程退出码判断 pass/fail（0=pass, 非0=fail）；声明了 `check_list` 的子阶段
   再把结果文件与基准文件按 key 比对，有差异同样判 fail
6. 日志写入 `{test_dir}/{test_name}/{N}_{子阶段名}/` 目录；子阶段未达期望或比对
   有差异 → `run_subcase` 抛 `SubcaseError` → 编排脚本中断 → 整个用例失败

**引用审计**：全量运行（不带路径参数）、没有触发 `--fail-fast` 并且全部用例通过时，
运行结束前检查 `qa/` 下是否还有没被任何 `.pyt` 调度过的 `test_*.py`（有的话说明
新用例忘了包 `.pyt`，会被静默跳过）——发现即打印清单并把本轮判为失败（退出码 1）。
有用例失败时跳过审计（失败用例可能没有调度完它的子阶段，审计会误报）；带路径
参数的局部运行不做审计。

---

## 2. 创建新测例

### 基本模板

**不需要任何 `sys.path.insert` 操作** — fly 启动时已自动配好所有模块路径。

```python
from _fly_log import INFO
import time
import os
import shutil

from e2e_tasks import write_data
from fly import open_db, get_config, get_work_directory

DB_PATH = os.path.join(get_work_directory(), "db")


def cleanup():
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)


def wait_for(condition, timeout=20.0, interval=0.5):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if condition():
            return True
        time.sleep(interval)
    return False


cleanup()
get_config().set_int("fail_unscheduleable_tasks", 0)

from fly.runtime import get_agent
master = get_agent()

master.launch_local_workers([{}])
assert wait_for(lambda: master._agent.get_connection_count() >= 1)

db = open_db(DB_PATH)

# ... test logic ...

master.stop()
INFO(f"[PASS] test_<feature>")
```

**新用例必须带 `.pyt` 包装**（编排脚本，runqa 只发现 `.pyt`）：把上面的脚本
（命名 `test_<feature>.py`）作为一个子阶段调度，旁边建 `test_<feature>.pyt`：

```python
# test_<feature>.pyt —— 编排脚本（用例入口，runqa 进程内执行）
run_subcase("test_<feature>.py", timeout=60)
```

没有包装的裸 `test_*.py` 不会被运行（静默跳过），并且全量运行结束后的引用审计
会把它列为未调度脚本、将本轮判为失败。单进程场景用上面的一行薄包装即可；
多阶段编排见下文 `.pyt` 机制一节。

### 多进程测试（协调器模式，legacy）—— 新测试请用 .pyt 机制（下节）

如果测试需要跨进程重启（如 load_db），历史上使用协调器模式（一个 `test_*.py`
经 `subprocess.run` 驱动辅助脚本）。**runqa 现在只发现 `.pyt`**：遗留的协调器
脚本已经或必须包一层同名 `.pyt`（`run_subcase("test_xxx.py", timeout=60)`）
才会被运行，裸 `test_*.py` 不再被发现。新测试一律用 `.pyt` 编排：

```
qa/storage/
├── test_load_db.pyt         # 编排脚本（runqa 发现此文件）
├── test_load_db.py          # 协调器（被 .pyt 调度为一个子阶段，不再独立发现）
├── load_db_run1.py          # 辅助脚本（非 test_ 前缀，不被 runqa 直接运行）
└── load_db_run2.py          # 辅助脚本
```

协调器通过 `subprocess.run` 运行辅助脚本：

```python
import subprocess
from fly import get_fly_binary

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
FLY_BIN = get_fly_binary()

result = subprocess.run(
    [FLY_BIN, "--log-dir", log_dir, script_path],
    capture_output=True, text=True, timeout=120, cwd=SCRIPT_DIR,
)
assert result.returncode == 0, f"Failed: {result.stderr}"
```

> **注意**：使用 `get_fly_binary()` 获取 fly 路径，不要硬编码 `bazel-bin/...`。`get_fly_binary()` 在任何场景（build/ 布局、bazel-bin/ 布局、tar 打包重定位）都能返回正确路径。

### 多阶段复合测试（.pyt 机制，推荐）

多阶段测试（跨进程重启、failed task + load_db + rerun 等）用 **`.pyt` 编排脚本 + `run_subcase` 原语**，不再手写 subprocess。比上面的协调器模式更声明式、更安全（sub case 失败即 case 失败）。

**.pyt 是 case 单位**（编排脚本，python），在 runqa 进程 exec；调 `run_subcase` 起 fly 子进程跑各阶段（sub case，每个 fresh C++ singleton）。

```
qa/storage/
├── test_load_db_two_processes.pyt   # 编排脚本（runqa 发现 *.pyt）
├── load_db_run1.py                  # sub case（被 .pyt 调，非 test_ 前缀）
└── load_db_run2.py
```

**`run_subcase` 原语**（runqa 注入 .pyt globals，直接用，无需 import）：

```python
run_subcase(script, timeout=30, setup=None, teardown=None, expect_pass=True, env=None,
            check_list=None)
```
- `script`：sub case .py 路径（相对 .pyt 目录或绝对）
- `setup`/`teardown`：无参 callable，sub case 级预处理/后处理（teardown 无论成败都跑）
- `expect_pass`：True=期望 fly 正常退出（默认）；**False=期望 fly 失败**（测试失败场景，如 failed task）。未达期望 → `raise SubcaseError` → .pyt 中断 → case 失败
- `env`：per-subcase env（如 `{"FLY_DB_PATH": ...}`），不污染全局 os.environ（-j 并发安全）
- `check_list`：基准比对清单，`None` 或空表示不比对，行为与从前完全一致。每项是该子阶段产出的一个结果文件，子阶段结束后运行器与基准文件按 key 比对，有差异即 `raise SubcaseError`。写法与规则见下文「基准比对机制」
- 返回 `SubcaseResult`（`.ok`/`.status`/`.tail()` 等；失败时不返回，直接 raise）

**示例**（failed task + load_db + rerun）：

```python
# failed_task_load_db_rerun.pyt
import os, shutil
DB = os.path.join(FLY_CASE_LOG_DIR, "my_db")   # FLY_CASE_LOG_DIR 是注入变量（case log 目录）

def setup_clean():
    if os.path.isdir(DB): shutil.rmtree(DB)

run_subcase("failed_task_run.py", timeout=30, setup=setup_clean, expect_pass=False)  # 期望失败
run_subcase("load_db_rerun.py", timeout=60)                                          # 默认期望成功
shutil.rmtree(DB, ignore_errors=True)                                                # 成功路径清理 log 外产物
INFO("[PASS] case passed")
```

**约定**：
- **sub case .py 自包含**：db 路径在 .py 里显式写明（读 `FLY_CASE_LOG_DIR` 或硬编码）；setup/teardown 是副作用预处理/后处理，**不向 sub case 传参**
- `FLY_CASE_DIR`/`FLY_CASE_LOG_DIR`/`FLY_CASE_STEM` 是 runqa **注入到 .pyt 的变量**（直接用，不依赖 `os.environ`，-j 并发 case 互不污染）
- log 外产物（如 DB）须 .pyt 成功路径末尾显式清；失败残留由下次 setup 清
- **sub case 未达 `expect_pass` 期望 → run_subcase raise → .pyt exec 中断 → 整个 case 失败**（fail-fast；teardown 仍跑）

**发现机制**：runqa 只发现 `*.pyt`——编排脚本是唯一的用例单位。裸 `test_*.py` 是 sub case 脚本，不独立发现；每个 `test_*.py` 必须被某个 `.pyt` 调度（有的 `.pyt` 运行多个名称不同的 `.py`，不能按同名判断），全量运行结束后的引用审计负责兜底。sub case 日志在 `{case_log_dir}/{N}_{script_stem}/fly.log`（N = sub case 序号，避免多阶段互相覆盖）。

**编排脚本的写法约定**（重录模式会 `exec` 编排脚本：调度的 `setup`/`teardown` 不会被调用，但子阶段调用之间的内联语句会真的执行）：
- 子阶段调用之间不写有副作用的语句和断言。文件操作放进下一个 `run_subcase` 的 `setup`；阶段之间的状态检查挪到下一个子阶段脚本的开头；最后一个子阶段之后的检查挪到该子阶段脚本的末尾（子阶段脚本已有同样检查时直接删掉编排脚本里的重复）。
- 编排脚本一个子阶段都没调度判为失败（防止静默假绿）。
- 含故意失败场景的用例（如自测）在重录模式下必须整体跳过：用注入的 `QA_MODE`（`"run"`/`"regold"`）判断，`if QA_MODE != "regold":` 包住全部场景。

### 基准比对机制（记录接口 + check_list + gold 文件）

通过判定可以不靠脚本里的 `assert`，改为「子阶段脚本把待检查的数据写进结果文件，运行器按 key 与基准文件比对」。退出状态符合预期且比对无差异才算通过；差异非空时 `run_subcase` 抛 `SubcaseError`（差异在前、退出状态问题在后），完整报告追加落盘到 `{case_log_dir}/<编排脚本名>.check.txt`（多个子阶段先后失败时各占一段）。

**测例产物的命名约定**：记 S 为子阶段脚本的文件名去掉 `.py`，产物一律以测试脚本名称为主题、后缀区分类型：

| 产物 | 命名 | 位置 |
|---|---|---|
| 结果文件（缺省） | `S.result.json` | 子阶段日志目录 |
| 结果文件（其他类型） | `S.<类型>.<扩展名>` | 子阶段日志目录 |
| 诊断信息 | `S.detail.json`（一个子阶段一份，键为 `<结果文件名>/<键>`） | 子阶段日志目录 |
| 基准文件（缺省类型） | `S.gold` | `qa/<编排脚本所在目录>/gold/` |
| 基准文件（其他类型） | `S.<类型>.gold` | `qa/<编排脚本所在目录>/gold/` |
| 差异报告 | `<编排脚本名>.check.txt` | 用例日志目录 |

**记录接口**（子阶段脚本内）：

```python
from test import record

# 取值型条目：基准存期望值
record("stage1", {"alpha": db.read_object("stage1/alpha")})
# 判定型条目：基准恒为 true，detail 只用于失败报告（不比对、不进基准）
record("no_failed_tasks", len(master.failed_tasks) == 0, detail=master.failed_tasks)
```

- 落点：本子阶段的日志目录（运行器注入 `FLY_CASE_SUB_DIR`）；每次调用立即写盘（临时文件 + 原子替换），进程中途崩溃时已记录的键保留。
- `file` 参数：缺省是 `S.result.json`；传入其他文件名时必须是纯文件名（不允许路径分隔符）且以 `S.` 开头，否则抛 `ValueError`；同一个结果文件里重复记录同一键抛 `RuntimeError`。
- **顶层布尔条目是判定**：结果文件顶层的布尔条目基准恒为 `true`，结果为 `false` 报「判定不成立」，重录时拒绝。期望值本来就是假的布尔量，要么改写成应为真的判定（如 `record("not_large", not result.is_large)`），要么放进字典里作为数据（如 `record("entry", {"is_large": result.is_large})`）——**嵌套在字典、列表里的布尔值是数据，`false` 是合法的期望值，可正常比对与重录**。
- 值规范化后写入：元组/列表→列表、集合→排序后的列表、字典键转字符串、字节串→`{len, sha256}`、`pathlib.Path`→字符串、numpy 标量/数组→Python 标量/嵌套列表、其他对象抛 `TypeError`（逐字段记录，不静默跳过）。
- 路径掩蔽缺省开启：字符串里的五个路径来源分别替换为对应占位符——子阶段日志目录（`FLY_CASE_SUB_DIR`）→ `<sub_log_dir>`、用例日志目录（`FLY_CASE_LOG_DIR`）→ `<case_log_dir>`、用例目录（`FLY_CASE_DIR`）→ `<case_dir>`、build 目录（`FLY_BUILD`）→ `<build>`、仓库根 → `<repo>`（环境变量不存在时跳过对应项），换机器换检出位置后基准仍然有效。
- 等待原语（`assert wait_until(...)`）与后续步骤还要使用的对象的判空守卫仍是 `assert`，不转记录。

**基准文件（gold）**：一个结果文件一个基准文件，按命名约定放在 `qa/<编排脚本所在目录>/gold/`：`S.result.json` 的基准是 `S.gold`，`S.<类型>.<扩展名>` 的基准是 `S.<类型>.gold`。`kv` 预设的基准内容就是与结果同形的 JSON 对象本身；`contains` 的基准内容是 `{"must": [...], "must_not": [...]}`。写入格式：缩进 2、键排序、末尾换行（文件名不带 `.json`，内容仍是 JSON）。基准里**顶层**的布尔值只允许 `true`（判定型条目）；嵌套布尔不受限。

**check_list**（`run_subcase` 的参数）：清单为空时行为与从前完全一致。每项是结果文件的类型后缀（运行器自动补 `S.` 前缀，如 `"result.json"` → `S.result.json`）、完整文件名（以 `S.` 开头），或选项字典：

```python
run_subcase("load_db_run2.py", timeout=60, env=shared_env, check_list=[
    "result.json",                                       # 仅类型后缀：自动补 S. 前缀，用缺省预设 kv
    {"file": "messages.txt", "method": "contains"},      # contains 预设（手工维护），基准是 S.messages.gold
    {"file": "trace.txt", "compare": compare_trace},     # 自定义比较方式
])
```

清单项的 `file` 必须是带扩展名的纯文件名（不允许路径分隔符；缺扩展名在启动子进程之前就报 `ValueError`）。

- **产品生成的文件不能直接写进 `check_list`**：写进去会被按命名约定展开成 `S.` 开头的名字，结果文件缺失时会提示已按命名约定展开。要让运行器检查产品文件（如 `message.log`），先由子阶段脚本把它复制或摘录成 `S.<类型>` 文件再列入清单。
- 预设：`kv`（缺省，JSON 键值比对，可重录）、`contains`（文本包含 `must`/`must_not`，手工维护）。
- 非法写法（缺 `file`、未知键、`method` 与 `compare` 同时出现、含路径分隔符或 `..`、缺扩展名、同一清单里 `file` 重复、预设名不存在、`gold` 不符合命名约定）在启动子进程之前抛 `ValueError`。
- 自定义比较方式是普通 Python 函数 `compare(result_path, gold)`，返回差异字符串列表（空列表表示通过）。`gold` 入参：声明了 `gold=` 时是 `gold/data/<gold>` 独立基准文件的绝对路径（文件名同样以 `S.` 开头、以 `.gold` 结尾，内容格式由自定义函数自己约定）；否则是 `S.<类型>.gold` 文件里的 JSON 值（不存在则传 `None`）。函数对象上定义 `make_gold(result_path)` 可支持重录（返回值写到对应基准位置）。编排脚本里注入了 `qa_record` 模块对象，可复用 `qa_record.diff_value(实际值, 期望值, 路径)` 等比对原语。
- 同一编排脚本不可重复调度同一子阶段脚本（基准按子阶段脚本名区分）。

**重录模式（`--regold`）**：`./qa/runqa --regold [<路径>...]`（路径可省略=全部 `.pyt`、目录、单个 `.pyt`，可多个；显式 `.py` 报错退出）。逐用例串行执行编排脚本：不启动 fly 子进程、不调 `setup`/`teardown`，调度函数按与正常运行相同的规则定位上次运行的结果文件，只对 `check_list` 逐项重录基准；基准内容有变化才写盘，并打印每个结果文件的新增/删除/改值键数。不做的事：不构建 fly、不清用例日志目录（上次的结果正是重录的输入）、不重建 `qa/logs/qa.log`（本轮日志写 `qa/logs/regold.log`）。单项规则：结果文件不存在 → 未重录；结果**顶层**布尔为 `false` → 拒绝重录并计为失败（判定型条目的期望恒为 true，重录会把 false 写成期望，检查从此永久失效；嵌套布尔是数据，可重录）；手工维护项 → 跳过并提示（`contains`、没有 `make_gold` 的自定义比较，以及显式写了 `manual=True` 的项）；结果文件比 `build/bin/fly` 旧 → 非阻断提示。编排脚本可用注入的 `QA_MODE`（`"run"`/`"regold"`）感知当前模式。**重录是显式操作，推送前校验不会调用它**。


**expect_pass 期望矩阵**：

| expect_pass | fly 结果 | 行为 |
|---|---|---|
| True（默认）| PASSED | 正常返回 |
| True | FAILED / TIMEOUT | raise（不该失败/hang）|
| False | FAILED | 正常返回（**预期失败达成**）|
| False | PASSED | raise（**不该成功**——场景没复现）|
| False | TIMEOUT | raise（hang 不是预期失败）|

### 任务函数

所有任务函数定义在 `src/test/py/e2e_tasks.py`。常用任务：

| 任务 | 说明 |
|------|------|
| `write_data(db, key, value)` | 写入数据（无依赖） |
| `read_data(db, key, deps)` | 读取数据（有依赖） |
| `gpu_write(db, key, value)` | 需要 gpu 属性 |
| `write_and_remove(db, key, value)` | 写入后删除 |
| `compute_sum(db, read_a, read_b, result)` | 读取两个值，写入和 |

如需新任务，在 `src/test/py/e2e_tasks.py` 中添加。

### 使用 worker 属性确定性钉选任务

通过 `@as_task(requires=...)` 和 worker 属性，可以确定性地把任务调度到指定 worker：

```python
from fly import as_task

# 定义带动态 requires 的任务（支持 lambda）
@as_task(inputs=lambda db, key, deps, cap: list(deps),
         requires=lambda db, key, deps, cap: [cap])
def read_on(db, key, deps, cap):
    return db.read_object(key)

# 启动带属性的 worker
master.launch_local_workers([
    {"attributes": ["reader_a"]},
    {"attributes": ["reader_b"]},
])

# 确定性调度：读任务必然跑在 reader_a worker
read_on(db, "key", [full_name], "reader_a")
```

---

## 3. 必须遵循的原则

### 扁平脚本，不要用 `__main__` 或 `main()` 包装

QA 测试脚本是**扁平的 Python 脚本**，代码从上到下直接执行，像写 bash 一样。

```python
# ✅ 正确 — 代码直接执行
cleanup()
master = get_agent()
master.start()
db = open_db(DB_PATH)
write_data(db, "key", 42)
master.stop()
INFO("[PASS] test_feature")

# ❌ 错误 — 不要用 if __name__ 或 main() 包装
def main():
    cleanup()
    master = get_agent()
    ...

if __name__ == "__main__":
    main()
```

**原因**：QA runner 通过 `fly --log-dir <dir> <script>` 启动独立进程执行脚本，脚本内容会被 `exec()` 直接执行。不需要 `__main__` 守卫，也不需要 `main()` 函数。

### 不要在测试脚本中做路径推断

fly 启动时已自动配好 `sys.path`（C++ `setup_sys_path` 注入 build/python/* 模块，Python 自动注入脚本所在目录）。测试脚本**不需要** `sys.path.insert` 操作。

```python
# ✅ 正确 — 直接 import
from e2e_tasks import write_data
from fly import open_db, get_config

# ❌ 错误 — 手动推断路径
import sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'src'))
```

获取 fly binary 路径（用于 spawn 子进程）：

```python
# ✅ 正确 — 使用 API
from fly import get_fly_binary
FLY_BIN = get_fly_binary()

# ❌ 错误 — 硬编码路径
FLY_BIN = os.path.join(PROJECT_ROOT, "bazel-bin", "src", "main", "cpp", "fly")
```

### Worker 模式

`launch_local_workers` 始终使用进程模式启动 Worker：

```python
# Worker 进程通过 subprocess.Popen 启动 fly --worker 子进程
master.launch_local_workers([{}])
```

**架构**：
- Worker 是独立进程，拥有独立的 DataService 单例和内存空间
- Worker 进程通过 TCP 连接 Master，实现真正的进程隔离
- 生产环境与测试环境使用完全一致的通信方式

### 在测试脚本中定义 `@as_task()` 函数

测试脚本中可以直接用 `@as_task()` 定义任务（通过 cloudpickle 序列化分发到 worker）。如需确定性调度到指定 worker，配合 `requires` 参数使用。

```python
# ✅ 正确 — 普通任务（任意 idle worker 执行）
@as_task()
def my_task(db, key):
    db.write_object(key, "value")

# ✅ 正确 — 带 requires 的任务（只在有对应属性的 worker 上执行）
@as_task(requires=lambda db, key, value: ["writer"])
def write_on(db, key, value):
    db.write_object(key, "value")

# ✅ 正确 — 使用 e2e_tasks.py 中的共享任务
from e2e_tasks import write_data
write_data(db, "key", "value")
```

常用任务（write_data、read_data 等）建议统一放在 `src/test/py/e2e_tasks.py` 中复用。仅在测试需要特殊行为（如自定义 requires、特定写入逻辑）时在测试脚本中定义。

### 只使用公共 Python API

QA 测试是高层集成测试，必须只使用公共 Python API。底层 C++ 测试属于 unit test（`qa/unit/` 或 `src/*/tests/`），不属于 QA。

```python
# ✅ 正确 — 使用公共 API
master = get_agent()
master.launch_local_workers([{}])
master.wait_for_workers(1)
assert master.worker_count >= 1
assert len(master.completed_tasks) >= 1

# ❌ 错误 — 使用内部 API
master._agent.get_connection_count()
master._agent.submit_task_with_deps(...)
master._worker_procs[0].kill()
import _fly_storage as storage
storage.ex_stg_get_data_service()
```

**公共 API 速查**：

| API | 说明 |
|-----|------|
| `get_agent()` | 获取 Master/Worker 单例 |
| `get_work_directory()` | 当前进程的工作目录路径（log_dir），用于拼接 DB 路径 |
| `master.launch_local_workers(configs)` | 启动 Worker 进程 |
| `master.wait_for_workers(n, timeout=30)` | 等待 N 个 Worker 连接，返回 True/False |
| `master.worker_count` | 当前连接的 Worker 数量 |
| `master.is_running()` | Master 是否在运行 |
| `master.get_worker_pids()` | 获取 Worker 进程 PID 列表 |
| `master.port` | Master 监听端口 |
| `master.completed_tasks` | 已完成任务 ID 列表 |
| `master.failed_tasks` | 失败任务 ID 列表 |
| `master.pending_tasks` | 等待中任务 ID 列表 |
| `master.stop()` | 停止 Master 和所有 Worker |
| `open_db(path)` | 打开/创建数据库 |
| `load_db(path)` | 加载已有数据库 |
| `get_fly_binary()` | 获取 fly 二进制路径（任何布局下都正确） |
| `db.read_object(name, cache="low")` | 读取数据 |
| `db.write_object(name, obj, save_to_db=True)` | 写入数据 |
| `db.remove_object(name)` | 删除数据 |

### 每个测试文件只测试一个场景

一个文件 = 一个场景 = 一次 agent 生命周期。需要多次启停 agent 的场景，使用协调器模式（subprocess）。

```python
# ✅ 正确 — 单场景单文件
cleanup()
master = get_agent()
master.launch_local_workers([{}])
# ... test logic ...

# ❌ 错误 — 多个测试函数共享 agent
def test_part1():
    master = get_agent()
    master.launch_local_workers([{}])
    # ...

def test_part2():
    master = get_agent()  # 复用 Part1 的 agent！
    # ...
```

### 文件命名

| 类型 | 命名 | 说明 |
|------|------|------|
| 编排脚本（用例单位） | `test_<name>.pyt` | runqa 只发现 `.pyt`，runqa 进程 exec，调 `run_subcase` 编排 sub case；单进程 case 是一行薄包装 |
| 子阶段脚本 | `test_<name>.py`（同名的 `.pyt` 调度它） | 被 `.pyt` 以 `run_subcase` 调度，fly 进程执行；不被 runqa 独立发现，没被任何 `.pyt` 调度会被引用审计拦下 |
| 辅助脚本 | `<name>.py`（非 test_ 前缀） | 由编排脚本或子阶段脚本调用的 sub case |

### DB 路径

使用 `os.path.join(get_work_directory(), "db")` 格式。`get_work_directory()` 返回当前进程的 log_dir 路径——每个 QA 测试由 runqa 分配独立 log_dir，用它拼接 DB 路径可获得进程隔离的工作空间，避免并发测试间互相干扰。测试前清理，测试后不清理（方便调试）。

### 日志系统

#### 统一日志原则

Fly 进程内**禁止使用 `print`**，所有日志输出统一使用 `_fly_log` 模块：

```python
from _fly_log import DBG, INFO, WARN, ERR

INFO(f"[PASS] test_feature")
WARN(f"重试中: attempt={i}")
ERR(f"连接失败: {error}")
DBG(f"调试信息: value={v}")
```

`_fly_log` 是 C++ Logger 的 Python binding，与 C++ 侧的 `INFO`/`WARN`/`ERR`/`DBG` 宏共用同一套日志系统。所有日志带有统一的时间戳和级别前缀。

**禁止**：
- `print(...)` / `print(..., file=sys.stderr)` — 日志混入非格式化输出
- `import logging` — 引入第二套日志系统
- `traceback.print_exc(file=sys.stderr)` — 改用 `ERR(traceback.format_exc())`

#### 日志架构

所有进程 debug 日志只落盘；终端输出唯一来源是 message 系统（`dual_output=false`，详见 [docs/message-system.md](../docs/message-system.md)）：

```
C++ Logger (src/log/cpp/logger.h)  →  {log_dir}/*.log（仅文件）
message 系统                        →  高价值日志经 LOG_MESSAGE 推送 master 终端
```

| 进程 | 文件输出 | 终端输出 |
|------|---------|-------------|
| Master | `{log_dir}/master.log` | ✅ 经 message 系统透出 |
| Worker | `{log_dir}/worker{N}.log` | ❌ 仅文件（message 配额内推送 master） |

#### QA 日志查看

`runqa` 为每个测试生成日志，同时 runner 自身输出到终端和 `qa/logs/qa.log`：

```
{test_dir}/{test_name}/            # 测试日志目录（fly --log-dir 指定）
├── fly.log                        # fly 进程全部输出（stdout + stderr 合并）
├── master.log                     # Master C++ Logger 文件输出
└── worker{N}.log                  # Worker C++ Logger 文件输出
```

**快速查看**：
```bash
# 查看 runner 汇总（与终端输出相同）
cat qa/logs/qa.log

# 查看失败测试的完整日志
cat qa/storage/test_read_cache_basic/fly.log

# 仅查看 Worker 日志（任务执行细节）
cat qa/storage/test_read_cache_basic/worker1.log

# 查看所有 ERROR 级别日志
grep '\[ERROR\]' qa/storage/test_read_cache_basic/fly.log
```

**测试失败时**，`runqa` 自动在终端打印失败测试的最后 15 行日志。

#### 生产环境日志

用户直接运行 `fly script.py` 时，日志写入 `fly_log/` 目录（可通过 `--log-dir` 自定义）：

```bash
fly --log-dir /var/log/fly my_script.py
# /var/log/fly/master.log     — Master 日志（同时显示在终端）
# /var/log/fly/worker1.log    — Worker 日志
```

每次运行自动创建新目录（`fly_log.1`, `fly_log.2`, ...），`fly_log.latest` 符号链接指向最新一次。

### 资源清理

- 每个 test 函数开头调用 `cleanup()` 删除旧 DB
- 测试结束时调用 `master.stop()`
- 不要在 `finally` 中清理 DB 目录（保留现场便于调试）

### 等待模式

```python
# ✅ 推荐 — 使用 wait_for 辅助函数
def wait_for(condition, timeout=20.0, interval=0.5):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if condition():
            return True
        time.sleep(interval)
    return False

assert wait_for(lambda: master._agent.get_connection_count() >= 1)

# 或使用 master 内置方法
completed = master.wait_for_all_tasks(expected=3, timeout=30)

# ❌ 错误 — 硬编码 sleep
time.sleep(5)
```

### 不要使用 `from fly.runtime import reset`

QA 测试由 fly 二进制以独立进程运行。`reset()` 用于单进程内模拟重启，不适合 QA 测试。如需跨进程测试，使用 subprocess 协调器模式。

---

## 4. 目录结构

```
qa/
├── README.md                    # 本文档
├── runqa                       # Python 测试运行器（-j 并发、计时、排序、qa.log 双写）
├── run_qa_tests.sh             # 兼容入口（薄 wrapper → runqa）
├── .gitignore                   # 忽略运行产物
│
├── api/                         # API 测试（缓存、进程模式、用户脚本）
├── backup/                      # 备份测试（自动备份、数据备份、持久化）
├── dependency/                  # 依赖测试（wait_obj、依赖链、超时）
├── fault/                       # 容错测试（graceful shutdown、worker crash、task exception）
├── mapreduce/                   # MapReduce 测试
├── performance/                 # 性能测试（fanout、copy profiling、complex scenario）
├── scheduling/                  # 调度测试（capability、load balancing、fail unscheduleable）
├── solver/                      # RAS 求解器测试（golden、RAS 参数矩阵）
├── storage/                     # 存储测试（read cache、save_to_db_false、load_db、frozen DB）
├── stress/                      # 压力测试（并发写、读写混合、稳定性）
├── write_provenance/            # 写入溯源测试（幂等、mismatch、load_db 溯源）
│
├── unit/                        # pytest 单元测试（不经 fly 运行，独立 import C++ 模块）
│   ├── smoke_test.py            #   Config + serialization + export 集成
│   └── storage_test.py          #   DataService + Database 单元测试
│
└── scripts/                     # 基准测试 / 调试脚本（不在 QA 套件中）
    ├── bench_ras*.py            #   RAS 性能基准
    ├── debug_*.py               #   调试辅助脚本
    └── sweep_*.py               #   参数扫描脚本
```

### 测试分类原则

| 目录 | 分类标准 | 示例 |
|------|---------|------|
| `api/` | fly 公共 API 的端到端验证 | agent cache、进程模式、用户脚本 |
| `backup/` | 数据备份/恢复相关 | 自动备份、数据复制、持久化 |
| `dependency/` | 任务间依赖关系 | wait_obj、依赖链、超时处理 |
| `fault/` | 错误处理和容错 | graceful shutdown、worker crash |
| `storage/` | 存储层功能 | read cache、frozen DB、load_db |
| `scheduling/` | 任务调度逻辑 | capability 匹配、负载均衡 |
| `stress/` | 高负载/并发场景 | 并发写、读写混合、稳定性 |
| `unit/` | pytest 单元测试 | C++ 模块直接测试（不经 fly） |

---

## 5. 常见问题

**Q: 测试超时怎么办？**

查看综合日志：
```bash
cat qa/<category>/<test_name>/fly.log
```
里面包含 fly 进程的全部 C++ 日志和 Python INFO/WARN/ERR 输出。搜索 `[ERROR]` 定位错误点。

**Q: 测试失败如何调试？**

1. `runqa` 失败时自动打印最后 15 行日志
2. 查看完整日志：`cat qa/<category>/<test_name>/fly.log`
3. 查看 Worker 日志（任务执行细节）：`cat qa/<category>/<test_name>/worker1.log`
4. 搜索错误：`grep '\[ERROR\]' qa/<category>/<test_name>/fly.log`

**Q: Worker 连接失败？**

确认 `./fly.sh build //src/main/cpp:fly` 已执行，fly 二进制存在且可执行。

**Q: 测试间互相干扰？**

每个测试使用独立的 `DB_PATH`（`os.path.join(get_work_directory(), "db")`），不应互相干扰。每个测试由 runqa 分配独立 log_dir，DB 路径基于该目录拼接。

**Q: 新建的 QA case 被 runqa 忽略？**

确认编排脚本以 `.pyt` 结尾且位于 `qa/` 的某个子目录下（runqa 只发现 `.pyt`；裸 `test_*.py` 不被发现，必须被某个 `.pyt` 调度，否则全量运行的引用审计会把本轮判为失败）。runqa 使用 `os.walk` 递归发现（跳过 `.latest`/`.N` 日志残留与符号链接目录），详见上文「并行度权威口径」。

---

## 6. 压力测试

### 覆盖场景

| 测试文件 | 场景 | 验证内容 |
|----------|------|----------|
| `test_stress_concurrent_write.py` | 2 Worker 并发写入同一 DB | 50 对象写入完整性、无数据丢失 |
| `test_stress_dep_chain.py` | 20 步串行依赖链 | `increment` 任务逐步 +1，最终值正确 |
| `test_stress_readwrite.py` | 并发读写混合 | 20 写 + 20 读同时进行，读结果一致 |
| `test_stress_cross_db.py` | 跨 DB 数据传输 | 3 DB 间 cross_db_copy + triple_db_sum 链 |
| `test_stress_freeze_reject.py` | freeze 后写入拒绝 | 10 写完成 → freeze → 10 写全部失败 |
| `test_stress_multi_db.py` | 多 DB 并行操作 | 4 DB × 10 写并行，互不干扰 |
| `test_stress_stability.py` | 长时间稳定性 | 100 写 + 50 compute_sum + 2×5MB 大对象 |

### 未覆盖场景

以下两种场景暂未覆盖测试：

1. **Worker 写入中途 crash**: Worker 在 `write_object` 落盘过程中崩溃（数据可能写入 data 文件但 idx 未持久化）。当前 `test_worker_crash.py` 测试的是 Worker 连接断开后的任务恢复，不涉及写半完成状态。
2. **失败 task 重跑写覆盖**: `restart_failed_tasks()` 重新执行曾写入过数据的 task，导致同一 key 的数据被重复写入。已知影响：`find_entry()` 返回旧数据、`restore_entries()` 不去重导致 `read_from_entries` 拼接。待实现 `find_entry() → back()` 和 `restore_entries()` 去重后补充测试。
