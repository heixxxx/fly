# 覆盖率测试方案

## 1. 概述

本项目使用 GCC gcov + lcov 进行 C++ 代码覆盖率检测。编译使用 `--config=coverage`（定义在 `.bazelrc`），该配置启用 `--copt=--coverage --linkopt=--coverage` 并设置 `--spawn_strategy=standalone` 以保留 gcno 文件。

**当前 C++ 覆盖率** (2026-07-31，仅源文件 `.cpp`，不含测试/系统头/虚拟头，全量单测 + 全量 QA，master + workers gcda 均纳入；死代码已清理)：
- 行覆盖率: **86.4%** (6991/8089)
- 函数覆盖率: 80.0% (1099/1373)
- 分支覆盖率: 39.3% (5366/13643)

**当前 Python 覆盖率** (2026-07-31，全部 Python 模块，Master + Worker 合并，全量 QA)：
- 行覆盖率: **83%** (1682/1981 stmts)
- 分支覆盖率: 47% (含 sitecustomize 等未链接模块)

> 完整的覆盖率分析报告（含未覆盖代码业务场景、改进建议）：[`coverage-report-2026-07-31.md`](coverage-report-2026-07-31.md)

## 2. 环境要求

| 工具 | 版本 | 用途 |
|------|------|------|
| gcc-12 | 12.3.0 | 编译带覆盖率标志的二进制 |
| gcov-12 | 12.3.0 | 解析 gcno/gcda（**必须与 gcc 版本匹配**） |
| lcov | **2.0+**（实测 2.5） | 收集和生成覆盖率报告（1.14/1.16 不支持 worker gcda 合并所需错误处理） |
| genhtml | 2.0+ | 生成 HTML 可视化报告 |
| coverage.py | 7.15+ | Python 覆盖率（支持 `process_startup` 多进程机制） |

> **lcov 升级**：Ubuntu apt 源仅提供 1.x，需从源码安装 2.0+。安装步骤：
> ```bash
> apt install libcapture-tiny-perl libdatetime-perl libdatetime-format-iso8601-perl
> git clone --depth 1 --branch v2.5 https://github.com/linux-test-project/lcov.git /tmp/lcov
> cd /tmp/lcov && install -m 755 bin/lcov bin/genhtml bin/geninfo bin/gendesc \
>     bin/genpng bin/get_version.sh bin/get_changes.sh bin/copy_dates.sh \
>     bin/fix.pl bin/checkstyle.sh /usr/local/bin/
> install -m 644 lib/lcovutil.pm /usr/local/lib/
> ```
>
> **lcov 2.x 命令兼容性**（升级后必须改）：
> - RC 选项改名：`--rc lcov_branch_coverage=1` → `--rc branch_coverage=1`（旧名报 `deprecated` 硬错误）
> - 可忽略警告升级为硬错误，必须加 `--ignore-errors empty,deprecated,source,inconsistent`
>   （`inconsistent` 处理 gcov「函数未命中但行命中」的数据 fidelity 差异）

## 3. 完整流程

### Step 1: 定义路径

```bash
BAZEL_CACHE="/root/.cache/bazel/_bazel_root/$(ls /root/.cache/bazel/_bazel_root/ | head -1)/execroot/_main/bazel-out/k8-fastbuild/bin"
```

### Step 2: 清理旧数据

gcda 是累积文件，不同编译的 gcda 不能混合。每次测试前必须清理。

> **必须用 `find -L`**：`bazel-bin` 是指向 execroot 的 symlink，`find` 默认不跟随符号链接，`find bazel-bin -name "*.gcda" -delete` 会**静默删不掉任何文件**，残留的旧 gcda 会让 lcov 读到陈旧数据（表现：全 0% 或 `inconsistent` 错误）。

```bash
/usr/bin/find -L $BAZEL_CACHE -name "*.gcda" -delete
rm -f /tmp/coverage*.info
```

### Step 3: 全量编译

```bash
bazel clean
bazel build //src/... --config=coverage
```

**验证**：编译完成后，检查 gcno 文件是否存在：

```bash
find $BAZEL_CACHE/src -path "*/cpp/_objs/*" -name "*.gcno" | wc -l
# 期望: > 30（若为 0 说明 --config=coverage 未生效，参见 §7.2）
```

### Step 4: 运行 C++ 单元测试

**必须直接运行测试二进制**，不能用 `bazel test`（bazel test 在 sandbox 中运行，gcda 会写入错误位置）。

```bash
for test_bin in $(ls $BAZEL_CACHE/src/agent/tests/*_test \
    $BAZEL_CACHE/src/common/tests/*_test \
    $BAZEL_CACHE/src/core/tests/*_test \
    $BAZEL_CACHE/src/export/tests/*_test \
    $BAZEL_CACHE/src/fly/tests/*_test \
    $BAZEL_CACHE/src/log/tests/*_test \
    $BAZEL_CACHE/src/network/tests/*_test \
    $BAZEL_CACHE/src/serialization/tests/*_test \
    $BAZEL_CACHE/src/storage/tests/*_test \
    $BAZEL_CACHE/src/task/tests/*_test 2>/dev/null); do
  echo -n "  $(basename $test_bin): "
  timeout 60 $test_bin 2>/dev/null && echo "OK" || echo "FAIL"
done
```

**验证**：所有测试输出 `OK`。

### Step 5: 运行 QA 集成测试

QA 测试通过 fly 进程运行，需要先安装。**必须设 `GCOV_PREFIX_STRIP` 并串行运行**（原因详见 §10 步骤 4）：

```bash
./fly.sh install
# bazel-bin 解引用到 execroot；GCOV_PREFIX_STRIP=3 把 gcda 重定向到 execroot
BAZEL_CACHE="$(readlink -f bazel-bin)"
EXECROOT="${BAZEL_CACHE%/bazel-out/*}"
GCOV_PREFIX="$EXECROOT" GCOV_PREFIX_STRIP=3 bash qa/run_qa_tests.sh -j 1
```

**验证**：`Passed: 133, Failed: 0`。

### Step 6: 收集覆盖率

只收集 `src/*/cpp/_objs` 目录（源码），不收集 `src/*/tests/_objs`（测试文件本身覆盖率虚高）：

```bash
lcov --capture \
  --directory $BAZEL_CACHE/src/agent/cpp/_objs \
  --directory $BAZEL_CACHE/src/storage/cpp/_objs \
  --directory $BAZEL_CACHE/src/network/cpp/_objs \
  --directory $BAZEL_CACHE/src/task/cpp/_objs \
  --directory $BAZEL_CACHE/src/core/cpp/_objs \
  --directory $BAZEL_CACHE/src/log/cpp/_objs \
  --directory $BAZEL_CACHE/src/serialization/cpp/_objs \
  --directory $BAZEL_CACHE/src/main/cpp/_objs \
  --output-file /tmp/coverage_raw.info \
  --rc branch_coverage=1 \
  --ignore-errors empty,deprecated,source,inconsistent,negative \
  --gcov-tool /usr/bin/gcov-12
```

**验证**：无 `stamp mismatch` 错误。查看概要：

```bash
lcov --summary /tmp/coverage_raw.info
```

若关键字段（master_agent.cpp 等）显示 0%，说明 gcno 缺失，需回到 Step 3 排查。

### Step 7: 过滤系统头文件

去除系统头文件、外部库、Bazel 虚拟头文件，只保留项目源码：

```bash
lcov --remove /tmp/coverage_raw.info \
  '/usr/include/*' \
  '/usr/lib/*' \
  '*/c++/12/*' \
  '*/x86_64-linux-gnu/*' \
  '*/external/*' \
  '*/_virtual_includes/*' \
  --output-file /tmp/coverage.info \
  --rc branch_coverage=1 \
  --ignore-errors empty,deprecated,source,inconsistent,negative
```

**验证**：

```bash
lcov --list /tmp/coverage.info
# 应只看到 src/ 下的项目源文件
```

### Step 8: 查看报告

```bash
# 概要
lcov --summary /tmp/coverage.info

# 逐文件列表
lcov --list /tmp/coverage.info
```

### Step 9: 生成 HTML 报告（可选）

lcov 中的路径是 `/proc/self/cwd/src/...`，genhtml 无法解析，需先替换：

```bash
sed 's|/proc/self/cwd/|/root/fly/|g' /tmp/coverage.info > /tmp/coverage_html_ready.info
genhtml /tmp/coverage_html_ready.info \
  --output-directory /tmp/coverage_html \
  --rc lcov_branch_coverage=1
```

查看：`/tmp/coverage_html/index.html`

## 4. 覆盖率数据解读

### 4.1 口径说明

覆盖率数字因过滤范围不同差异极大，对比时必须统一口径：

| 口径 | 总行数 | 行覆盖率 | 说明 |
|------|--------|----------|------|
| **原始**（lcov capture 直出） | ~9400 | ~88% | 含系统头文件、外部库内联代码 |
| **源文件**（过滤后） | ~4800 | ~88% | 仅 `src/*/cpp/*.cpp`，不含测试 |
| **含测试**（含 tests/_objs） | ~9600 | ~90% | 包含 `*_test.cpp`（覆盖率接近 100%，虚高） |

> 本文档中的覆盖率数字统一使用**源文件口径**（§3 Step 7 过滤后的结果）。

### 4.2 历史数据

| 日期 | 行覆盖率 | 函数覆盖率 | 过滤口径 | 测试数 | 主要变更 |
|------|----------|-----------|---------|--------|---------|
| 2026-05-25 | 84.1% (8057/9576) | 70.6% | 全文件 | 336 | 首次覆盖率测试，agent 仅 51.6% |
| 2026-06-03 | 90.2% (12525/13881) | 88.7% | 含测试+系统头 | ~500 | 补充 142 个测试，文档基线 |
| 2026-06-04 | 88.4% (4251/4807) | 81.9% | **仅源文件** | 44+47QA | ProcessInfo 重构，修复测量方法 |
| 2026-06-05a | 88.4% (C++) + 82% (Python MR) | 81.9% / 100% | **源文件 + Python MR** | 44+51QA | MapReduce 框架，Worker 覆盖率基础设施 |
| 2026-06-05b | 88.0% (4303/4887) C++ + 54% (667/1268) Python | 81.7% / — | **源文件 + 全部 Python** | 44+51QA | 修复 lcov 收集时机，扩展 Python 覆盖率到所有模块 |
| 2026-06-15 | 84.5% (4874/5766) C++ + 53% (674/1307) Python | 78.3% / — | **源文件 + 全部 Python** | 44+74QA | 网络层重构(Transport/ConnectionManager) + db_id 重构 + connect 非致命化。C++ 总行数增加（新增网络抽象层、tcp_socket/connection_manager 等），新文件覆盖率待提升致总数下降 |
| 2026-07-31 | 77.9% (6528/8377) C++ + 60.2% (1247/2091) Python | 70.7% / — | **源文件 + 全部 Python** | 52+131QA | message 系统重构 + lcov 升级 2.0。⚠️ 两项方法论缺陷致虚低：worker gcda 未合并（data_client 等显 0%）、Python import 早于 coverage.start。详见 §12 |
| 2026-07-31 (修复后) | **85.3%** (6963/8162) C++ + **76%** (1625/2063) Python | 79.2% / — | **源文件 + 全部 Python** | 51单测+131QA | 修复覆盖率收集基础设施：C++ 用 `GCOV_PREFIX_STRIP=3` 重定向 gcda 到 execroot + `find -L` 清理；Python 用 sitecustomize 早期注入 coverage + `data_file` 绝对路径防 cwd 漂移。worker_agent 0%→82%、mapreduce 55%→96%。详见 §12 |

> 注意：2026-06-03 的 90.2% 包含测试文件和系统头，不可与 06-04 的 88.4%（纯源文件）直接比较。逐文件对比见 §5。

## 5. 模块覆盖率详情

按行覆盖率降序排列（2026-06-05，源文件口径）：

| 模块 | 行覆盖率 | 函数覆盖率 | 行数 | 评估 |
|------|----------|-----------|------|------|
| dependency_graph.cpp | 100% | 100% | 88 | 优秀 |
| storage_manager.cpp | 100% | 100% | 34 | 优秀 |
| task_manager.cpp | 100% | 100% | 79 | 优秀 |
| temp_store.cpp | 100% | 100% | 88 | 优秀 |
| heartbeat_monitor.cpp | 100% | 100% | 21 | 优秀 |
| decompress_helper.cpp | 100% | 100% | 12 | 优秀 |
| process_info.h | 100% | 100% | 1 | 优秀 |
| compressor.cpp | 97.6% | 100% | 41 | 优秀 |
| write_back_queue.cpp | 96.6% | 100% | 59 | 优秀 |
| worker_manager.cpp | 96.9% | 93.8% | 128 | 优秀 |
| task_scheduler.cpp | 96.4% | 100% | 56 | 优秀 |
| io_thread_pool.cpp | 96.1% | 100% | 77 | 优秀 |
| data_reader.cpp | 96.3% | 88.2% | 54 | 优秀 |
| lz4_compressor.cpp | 94.9% | 69.2% | 39 | 良好 |
| logger.cpp | 94.7% | 100% | 94 | 优秀 |
| data_writer.cpp | 94.1% | 87.5% | 85 | 良好 |
| object_header.cpp | 94.1% | 100% | 51 | 良好 |
| local_index.cpp | 94.3% | 66.7% | 209 | 良好 |
| config.cpp | 93.6% | 100% | 47 | 优秀 |
| data_service.cpp | 93.7% | 98.5% | 638 | 优秀 |
| compressing_streambuf.cpp | 93.0% | 83.3% | 43 | 优秀 |
| database.cpp | 92.7% | 76.5% | 341 | 良好 |
| zlib_compressor.cpp | 93.3% | 72.7% | 45 | 良好 |
| zstd_compressor.cpp | 92.3% | 72.7% | 39 | 良好 |
| decompressing_streambuf.cpp | 90.2% | 66.7% | 61 | 良好 |
| reactor.cpp | 90.1% | 87.5% | 81 | 优秀 |
| master_agent.cpp | 85.3% | 74.2% | 1063 | 良好 |
| worker_agent.cpp | 84.7% | 80.2% | 693 | 良好 |
| task_executor.cpp | 82.9% | 100% | 35 | 良好 |
| ~~data_client.cpp~~ | 79.2% | 100% | 48 | 已删除（死代码，见 coverage-report §4.2） |
| metadata_client.cpp | 74.6% | 100% | 63 | 良好 |
| tcp_transport.cpp | 69.6% | 73.9% | 227 | 需改进 |
| main.cpp | 55.8% | 28.6% | 156 | 需改进 |
| process_info.cpp | 45.5% | 66.7% | 22 | 需改进 |
| graceful_exit.cpp | 33.3% | 50.0% | 9 | 需改进 |

## 6. 低覆盖率分析与改进方向

| 文件 | 行覆盖率 | 原因 | 改进方向 |
|------|----------|------|----------|
| `graceful_exit.cpp` | 33.3% | 调用 `_exit()` 终止进程，单元测试无法捕获 | 进程级集成测试 |
| `process_info.cpp` | 45.5% | `get_hostname()` 缓存路径和错误处理未覆盖 | 添加 hostname override/getenv 单元测试 |
| `main.cpp` | 55.8% | 入口函数、信号处理、命令行解析 | 进程级集成测试 |
| `tcp_transport.cpp` | 69.6% | 网络错误路径（socket/bind/listen 失败） | mock 或异常注入 |
| `metadata_client.cpp` | 74.6% | 远程元数据查询的错误路径 | 错误注入测试 |

## 7. 已知问题与排障

### 7.1 gcda 写入位置错误（bazel test）

**现象**：lcov 报 0% 覆盖率，但测试确实在运行。

**原因**：`bazel test` 在 sandbox 中运行，gcda 写入 `runfiles/` 目录而非 `_objs/`。lcov 只从 `_objs/` 收集。

**解决**：直接运行测试二进制文件，不用 `bazel test`。

### 7.2 gcno 文件丢失（sandbox 编译）

**现象**：lcov 报 `stamp mismatch`，master_agent.cpp 等文件显示 0%。

**诊断**：
```bash
find $BAZEL_CACHE/src -path "*/cpp/_objs/*" -name "*.gcno" | wc -l
# 若为 0：gcno 丢失
```

**原因**：`bazel build --copt=--coverage` 在 linux-sandbox 中编译，gcno 作为副作用文件留在 sandbox stash 中，不会复制到 `_objs/`。

**解决**：使用 `--config=coverage`（`.bazelrc` 中定义），包含 `--spawn_strategy=standalone`，编译在本地执行，gcno 保留在 `_objs/`。

```bash
# 正确
bazel build //src/... --config=coverage

# 错误（gcno 丢失）
bazel build //src/... --copt=--coverage --linkopt=--coverage
```

### 7.3 gcov 版本不匹配

**现象**：lcov 报 `stamp mismatch with notes file`。

**原因**：二进制用 gcc-12 编译，但 lcov 默认调用系统 gcov (9.4.0)。

**解决**：lcov 命令必须加 `--gcov-tool /usr/bin/gcov-12`。

### 7.4 bazel coverage 命令卡死

**现象**：`bazel coverage //src/...` 在分析阶段无限循环。

**原因**：Bazel 9.0.0 的 coverage aspect 遍历依赖图时死循环。

**解决**：不用 `bazel coverage`，使用本流程（`bazel build --config=coverage` + 直接运行二进制）。

### 7.5 gcda 累积导致数字混乱

**现象**：多次运行后覆盖率数字忽高忽低。

**原因**：gcda 文件是累积的，不同编译产生的 gcda 不能合并。

**解决**：每次覆盖率测试前清理所有 gcda 文件（Step 2）。

### 7.6 genhtml 路径错误

**现象**：`genhtml: ERROR: cannot read /proc/self/cwd/src/...`

**原因**：lcov 记录的源文件路径包含 `/proc/self/cwd/`，genhtml 无法解析。

**解决**：生成 HTML 前替换路径（见 Step 9）。

### 7.7 find 不跟随 bazel-bin 符号链接（gcda 清理静默失败）

**现象**：lcov 报全 0%，或大量 `inconsistent` 错误，但测试确实在运行。

**原因**：`bazel-bin` 是指向 execroot 的 symlink。`find bazel-bin -name "*.gcda" -delete`（不带 `-L`）**不跟随符号链接，静默删不掉任何文件**。残留的上一次编译的旧 gcda 被 lcov 读到，与当前 gcno 不匹配 → 0% / inconsistent。

**解决**：清理必须用 `find -L bazel-bin -name "*.gcda" -delete`（见 Step 2）。

### 7.8 gcda 随 runqa 的 cwd 漂移（worker gcda 全部丢失）

**现象**：worker 进程执行的代码（`worker_agent.cpp`、`data_client_pool.cpp` 等）整体 0% 或大幅虚低，但 master 侧正常。

**原因**：gcov 把每个对象的构建路径记为 `/proc/self/cwd/bazel-out/...`（相对 `/proc/self/cwd`），进程退出时按**当前 cwd** 解析写出。runqa 把每个测试以 `cwd=qa/<cat>/<test>/` 运行，于是所有 fly 进程（master + worker）的 gcda 都写到 `qa/<cat>/bazel-out/...`，而 lcov 只扫 `bazel-bin/_objs`，全部丢失。

**解决**：QA 阶段设 `GCOV_PREFIX_STRIP=3 GCOV_PREFIX=$EXECROOT`（见 Step 5 / §10）。`STRIP=3` 剥掉 `/proc/self/cwd` 前缀，`PREFIX=execroot` 把 gcda 定向到 .gcno 所在地。

### 7.9 Python coverage data_file 随 cwd 漂移（worker 数据丢失）

**现象**：Python `read_cache.py`、`mapreduce.py` 等 worker 侧模块虚低甚至 0%，但 `fly/__init__.py` 等正常。

**原因**：coverage 默认 `data_file=.coverage`（相对路径）。runqa 切换 cwd 后，parallel 模式生成的 `.coverage.<host>.<pid>.<rand>` 散落到各 `qa/*/`，`coverage combine` 从项目根只找得到零星几个 → worker 数据大量丢失。

**解决**：`.coveragerc` 固定 `data_file = /tmp/fly_py_coverage/.coverage`（绝对路径），所有进程数据集中一处（见 §8.2）。

### 7.10 lcov 2.x 的 negative 分支计数错误

**现象**：`lcov: ERROR: (negative) Unexpected negative taken count '-1' for branch ...`，capture 中断。

**原因**：gcov 数据里偶现负的分支计数值，lcov 2.x 默认当硬错误。

**解决**：`--ignore-errors` 加 `negative`（完整：`empty,deprecated,source,inconsistent,negative`）。

## 8. Python 覆盖率

### 8.1 概述

Python 测试依赖 Bazel 构建的 nanobind C++ 模块，运行在 Fly 嵌入式 Python 解释器中。覆盖率使用 `coverage.py` 测量。

**当前 Python 覆盖率** (2026-07-31，全部 Python 模块，Master + Worker 合并，全量 QA)：
- 行覆盖率: **83%** (1682/1981 stmts)
- 分支覆盖率: 47% (含 sitecustomize 等未链接模块)

### 8.2 机制：sitecustomize 早期注入

Fly 的 Worker 通过 `subprocess.Popen` 启动独立子进程。coverage.py 必须在每个进程**解释器启动最早期**就接入，否则 `fly` 包的 import（及其连带的 storage/core/task 等模块级代码）会先于 coverage.start() 执行而漏测。

实现：`coverage.process_startup()` + `sitecustomize.py`（官方多进程方案）。
- `src/fly/sitecustomize.py`：CPython 的 `Py_Initialize` 默认 import `site`，后者自动 import `sys.path` 上第一个 `sitecustomize` 模块。fly.sh wrapper 设 `PYTHONPATH="$BUILD_DIR/python"`，使 `build/python/sitecustomize.py`（install 时从 `src/fly/` 软链）在任何 `import fly` 之前加载。该模块检测 `FLY_PYCOVERAGE`，设置 `COVERAGE_PROCESS_START` 指向 `.coveragerc` 并调 `coverage.process_startup()`。Worker 子进程继承环境变量，同样自启动 coverage——无需 spawn 时特殊接线。
- `src/fly/.coveragerc`：`parallel=True`（每进程独立数据文件）、`concurrency=thread`、`sigterm=True`、**`data_file` 绝对路径**（见下）。
- `src/fly/main.py`：`_cleanup() → _stop_coverage()` 在退出时显式 stop/save（应对 C++ `graceful_exit.cpp` 调 `_exit()` 跳过 Python atexit 的路径）。

> **`data_file` 绝对路径（关键）**：runqa 以 `cwd=test_dir` 运行每个测试。coverage 默认 `data_file=.coverage`（相对路径），parallel 模式生成的 `.coverage.<host>.<pid>.<rand>` 会**散落到各 `qa/*/` 目录**，`coverage combine` 从项目根只找得到根目录那几个——静默丢失绝大部分数据（read_cache.py 等显示 0%）。`.coveragerc` 固定 `data_file = /tmp/fly_py_coverage/.coverage` 后，所有进程数据集中一处。

### 8.3 运行 Python 覆盖率

**方式一：使用自动化脚本（推荐）**

```bash
./tools/measure_coverage.sh python
```

**方式二：手动运行**

```bash
# 1. 构建并安装（install 会把 sitecustomize.py + .coveragerc 软链到 build/python/）
./fly.sh build //src/main/cpp:fly && ./fly.sh install

# 2. 清理固定数据目录（.coveragerc 把 data_file 指到这里）
rm -rf /tmp/fly_py_coverage && mkdir -p /tmp/fly_py_coverage

# 3. 运行全量 QA（FLY_PYCOVERAGE=1 触发 sitecustomize 注入；parallel 模式可并行，无需 -j 1）
FLY_PYCOVERAGE=1 bash qa/run_qa_tests.sh

# 4. 合并所有进程的 parallel 数据文件
coverage combine --data-file=/tmp/fly_py_coverage/.coverage /tmp/fly_py_coverage

# 5. 生成报告
coverage report  --data-file=/tmp/fly_py_coverage/.coverage --show-missing
coverage html    --data-file=/tmp/fly_py_coverage/.coverage -d /tmp/fly_coverage/python/html
```

### 8.4 Python 单元测试

Python 单元测试通过 Bazel 运行（覆盖率由 QA 集成测试覆盖，单测用于功能验证）：

```bash
bazel test //src/fly/tests:user_task_test
bazel test //src/fly/tests:executor_test
bazel test //src/fly/tests:main_test
```

### 8.5 关键注意事项

- C++ `graceful_exit.cpp` 调用 `_exit()` 会跳过 Python 的 `atexit` 处理器，因此 `main.py::_cleanup()` 里显式调 `coverage.Coverage.current().stop()/save()` 兜底（sitecustomize 启动的 Coverage 也会注册自己的 atexit，双保险）。
- Worker 子进程继承 `FLY_PYCOVERAGE` 与 `COVERAGE_PROCESS_START`，无需 `agent.py` 在 spawn 时特殊接线——这是相对旧方案（spawn 时传 `FLY_PYCOVERAGE_DATA`）的简化。

## 9. 自动化脚本

`tools/measure_coverage.sh` 提供一键覆盖率测量：

```bash
# Python 覆盖率（含 Worker 进程）
./tools/measure_coverage.sh python

# C++ 覆盖率（单元测试 + QA）
./tools/measure_coverage.sh cpp

# 全部
./tools/measure_coverage.sh all
```

输出目录：
- `/tmp/fly_coverage/python/` — Python HTML + JSON + text 报告
- `/tmp/fly_coverage/cpp/` — C++ HTML + lcov 报告

## 10. 全量覆盖率测试流程

以下为完整的 C++ + Python 覆盖率测试流程。**推荐直接用 `./tools/measure_coverage.sh all`**；下方为展开版，便于理解每一步和排障。

```bash
# ── C++ 覆盖率 ──
# 1. 编译（coverage 模式）。--config=coverage 见 .bazelrc，含 --spawn_strategy=standalone
#    （sandbox 会吞掉 gcno，必须 standalone）。
bazel build //src/... --config=coverage

# 2. 清理旧 gcda。
#    ⚠️ 必须用 find -L：bazel-bin 是 symlink，不带 -L 的 find 静默删不掉任何 gcda，
#    残留旧数据会让 lcov 读到陈旧 gcda → 全 0% 或 inconsistent 错误。
BAZEL_BIN="$(readlink -f bazel-bin)"   # 解引用到 execroot 真实路径
find -L "$BAZEL_BIN" -name "*.gcda" -delete

# 3. 运行 C++ 单元测试（直接跑二进制，不用 bazel test——sandbox 会把 gcda 写错位置；
#    且过滤掉 .runfiles 镜像副本）。单测覆盖了大量纯逻辑（task/graph/solver 算法）。
for t in $(find -L "$BAZEL_BIN/src" -type f -name "*_test" -executable | grep -v '\.runfiles'); do
  timeout 60 "$t" 2>/dev/null && echo "OK: $(basename $t)" || echo "FAIL: $(basename $t)"
done

# 4. 运行 QA 测试（串行 + gcda 重定向）。
#    ⚠️ GCOV_PREFIX_STRIP=3 + GCOV_PREFIX=execroot 是关键：gcov 把构建路径记为
#    /proc/self/cwd/bazel-out/...，进程退出时按【当前 cwd】解析写出。runqa 以
#    cwd=test_dir 运行，会把 gcda 散落到 qa/<cat>/bazel-out/... 而 lcov 扫不到。
#    GCOV_PREFIX_STRIP=3 剥掉 /proc/self/cwd 前缀，GCOV_PREFIX=execroot 把 gcda 定向
#    到 .gcno 所在地（lcov 扫描地）。master 与所有 worker 继承此环境，统一写入。
#    ⚠️ 必须串行 -j 1：gcda 退出时的 read-merge-write 非原子，并发写会互相覆盖。
./fly.sh install
EXECROOT="${BAZEL_BIN%/bazel-out/*}"
GCOV_PREFIX="$EXECROOT" GCOV_PREFIX_STRIP=3 bash qa/run_qa_tests.sh -j 1

# 5. 收集覆盖率。lcov 2.x：RC 键改名 lcov_branch_coverage→branch_coverage，
#    且 empty/deprecated/source/inconsistent/negative 都是硬错误，必须 --ignore-errors。
OBJ_DIRS=""
for dir in agent storage network task core log serialization common main message solver; do
  d="$BAZEL_BIN/src/$dir/cpp/_objs"; [ -d "$d" ] && OBJ_DIRS="$OBJ_DIRS --directory $d"
done
lcov --capture $OBJ_DIRS \
  --output-file /tmp/fly_coverage/cpp/raw.info \
  --rc branch_coverage=1 --gcov-tool /usr/bin/gcov-12 \
  --ignore-errors empty,deprecated,source,inconsistent,negative

# 6. 过滤系统头/外部库/测试文件 + 生成 HTML
lcov --remove /tmp/fly_coverage/cpp/raw.info \
  '/usr/include/*' '/usr/lib/*' '*/c++/12/*' '*/x86_64-linux-gnu/*' \
  '*/external/*' '*/_virtual_includes/*' '*/tests/*' '*/test_*' '*/export/*' \
  --rc branch_coverage=1 --ignore-errors empty,deprecated,source,inconsistent,negative \
  --output-file /tmp/fly_coverage/cpp/coverage.info

sed -i 's|/proc/self/cwd/|/root/fly/|g' /tmp/fly_coverage/cpp/coverage.info
genhtml /tmp/fly_coverage/cpp/coverage.info \
  --branch-coverage --legend --ignore-errors inconsistent \
  --output-directory /tmp/fly_coverage/cpp/html

# ── Python 覆盖率 ──
./tools/measure_coverage.sh python
```

## 11. 覆盖率数据合并说明

### C++ 数据

C++ 覆盖率使用 gcov + lcov。gcda 文件由运行的二进制在退出时自动写入（经 §10 步骤 4 的 `GCOV_PREFIX_STRIP` 重定向到 execroot）。lcov `--capture` 直接从 gcno/gcda 读取数据，master 与所有 worker 的 gcda 落在同一 execroot 树（串行运行避免并发写冲突），无需额外合并步骤。

### Python 数据

Python 覆盖率经 sitecustomize 早期注入，`parallel=True` 让每个进程（master + 各 worker）写独立数据文件到 `.coveragerc` 固定的 `data_file = /tmp/fly_py_coverage/.coverage`（parallel 模式自动加 `.<host>.<pid>.<rand>` 后缀）。`coverage combine` 合并该目录下所有文件：

```bash
coverage combine --data-file=/tmp/fly_py_coverage/.coverage /tmp/fly_py_coverage
```

> `data_file` 绝对路径是关键——相对路径会让数据随 runqa 的 cwd 散落到 `qa/*/`，combine 只能从项目根找到零星几个，静默丢失绝大部分（详见 §12.1）。

## 12. 覆盖率收集缺陷（已修复）

> **状态**：本节原记录的两个方法论缺陷已于本次修复消除。下方保留现象、真根因与修复方案作为方法论记录。修复前的虚低数字见 §4.2 历史表的 2026-07-31 行。

### 12.1 Python：fly 包 import 早于 coverage.start()（✅ 已修复）

**现象（修复前）**：`fly/__init__.py`(36%)、`main.py`(11%)、`bootstrap.py`(31%) 虚低，但这些模块的函数在 QA 中被大量调用。

**真根因**：`src/fly/main.py::_run_master()/_run_worker()` 内部才调用 `coverage.start()`，但此时 `fly/__init__.py`（连带 storage/core/task/userdoc/mapreduce/project 全套 import）、C++ 绑定 import、bootstrap 都已执行完毕——coverage 无法测量「先 import 后 start」的代码。

**修复方案**：`coverage.process_startup()` + `sitecustomize.py`，在解释器启动最早期注入 coverage。

- `src/fly/sitecustomize.py`：CPython 的 `Py_Initialize` 默认 import `site`，后者自动 import `sys.path` 上第一个 `sitecustomize` 模块。fly.sh wrapper 设 `PYTHONPATH="$BUILD_DIR/python"`，使得 `build/python/sitecustomize.py`（install 时从 `src/fly/` 软链过来）在**任何 `import fly` 之前**被加载。该模块检测 `FLY_PYCOVERAGE` 环境变量，设置 `COVERAGE_PROCESS_START` 指向 `.coveragerc` 并调用 `coverage.process_startup()`。Worker 子进程继承环境变量，同样在启动早期自启动 coverage——无需 spawn 时特殊接线。
- `src/fly/.coveragerc`：`parallel=True`（每进程独立数据文件，`concurrency=thread` 支持多线程，`sigterm=True` 应对 worker 的 SIGTERM 退出）；**`data_file` 必须用绝对路径**（见下）。
- `src/fly/main.py`：删除了 `_run_master/_run_worker` 内部的 coverage.start() 块；统一由 `_cleanup() → _stop_coverage()` 在退出时显式 stop/save（应对 C++ `graceful_exit.cpp` 调 `_exit()` 跳过 Python atexit 的路径）。
- `fly.sh do_install()`：把 sitecustomize.py 与 .coveragerc 软链到 `build/python/` 顶层。
- `tools/measure_coverage.sh`：仅设 `FLY_PYCOVERAGE=1`，覆盖率自动启动。

**`data_file` 绝对路径（关键，与 C++ gcda cwd 漂移同源）**：runqa 以 `cwd=test_dir`（`qa/<cat>/<test>/`）运行每个测试。coverage 默认 `data_file=.coverage`（相对路径），parallel 模式下生成 `.coverage.<host>.<pid>.<rand>`，这些文件会**散落到各 `qa/*/` 目录**。`coverage combine` 从项目根运行只找得到根目录那几个，**静默丢失绝大部分 worker 数据**——表现为 `read_cache.py`、`mapreduce.py` 等 worker 侧模块虚低甚至 0%。`.coveragerc` 里固定 `data_file = /tmp/fly_py_coverage/.coverage` 后，所有进程（master + worker）的数据集中一处，combine 不再遗漏。

**验证**：修复后全量 QA（master + workers 合并），`fly/__init__.py` 36%→66%、`main.py` 11%→69%、`mapreduce.py` 55%→96%、`database.py` 60%→91%，整体 **60%→76%**。（注：`temp_store.py` 0% 经核查为孤儿代码——与 C++ `fly::TempStore` API 分歧、0 个 Python 调用者，已删除，0% 缺口随之消除。）

### 12.2 C++：worker gcda 丢失（✅ 已修复）

**现象（修复前）**：整个 worker 进程执行的代码（`worker_agent.cpp`、`data_client_pool.cpp`、`data_server.cpp` 等）覆盖率丢失，部分文件显示 0%。

**真根因（与原诊断不同）**：不是「worker gcda 用 GCOV_PREFIX 隔离到 /tmp 导致 lcov 漏扫」。经系统排查，真根因是**两个叠加问题**：

1. **gcda 写入位置随 cwd 漂移**：gcov 把每个对象的构建路径记为 `/proc/self/cwd/bazel-out/k8-fastbuild/bin/src/.../*.gcda`（相对 `/proc/self/cwd`），进程退出时按**当前 cwd** 解析写出。runqa 把每个测试以 `cwd=test_dir`（`qa/<category>/`）运行，于是所有 fly 进程（master + worker）的 gcda 都写到 `qa/<category>/bazel-out/...`，**而非 bazel-bin/**。lcov `--capture` 只扫 `bazel-bin/_objs`，全部丢失。
2. **`find` 不跟随 bazel-bin 符号链接**：`bazel-bin` 是指向 execroot 的 symlink，`find bazel-bin -name "*.gcda" -delete`（不带 `-L`）**静默删不掉任何 gcda**，导致 lcov 读到的是上一次编译的陈旧残留，产生 0% 与 `inconsistent` 错误。

**修复方案**（`tools/measure_coverage.sh` + `src/agent/py/agent.py`）：

- **gcda 重定向到 execroot**：QA 阶段设 `GCOV_PREFIX=$EXECROOT GCOV_PREFIX_STRIP=3`。`GCOV_PREFIX_STRIP=3` 剥掉构建路径里的 `/proc/self/cwd` 前缀，`GCOV_PREFIX=execroot` 把 gcda 定向到 execroot（即 `bazel-bin` 真实指向的目录，.gcno 所在地、lcov 扫描地）。master 与所有 worker 进程继承这两个变量，统一写入同一 execroot 树。
- **QA 串行 `-j 1`**：gcda 退出时的 read-merge-write 非原子，串行消除并发写风险。覆盖率测量是周期性诊断任务，可接受。
- **`find -L` 清理 gcda**：跟随 symlink 正确清理。
- **`agent.py::_spawn_process_worker`**：当父进程设了 `GCOV_PREFIX_STRIP`（新机制）时，worker 不再隔离到 `worker_N/` 子目录（否则又跳出 lcov 扫描范围）；仅旧的纯 `GCOV_PREFIX`（无 STRIP）场景保留隔离。

**验证**：修复后跑**单个** QA 测试（`test_netprobe_remote_read`），`worker_agent.cpp` 34%、`data_client_pool.cpp` 59%、`data_server.cpp` 70%、`master_agent.cpp` 31%——worker 进程的覆盖率不再丢失。

**关于 `data_client.cpp` / `metadata_client.cpp` 显示 0%**：原报告 §4.2 判断它们「实际被 QA 执行，只是 gcda 未合并」是**误判**。经核查（`nm fly.bin | grep DataClient` = 0），这两个模块的独立符号未出现在符号表——但并非"未链接"，而是被**内联进调用者**（链接优化消除独立符号，gcov 计数器仍归属原源文件）。修复后实测 `metadata_client.cpp` 达 79.7%，说明它实际被内联链接且覆盖良好。唯 `data_client.cpp` 的 `request_compressed_data` 函数确属未被任何路径调用，0% 真实。

### 12.3 lcov 2.x 升级兼容性（✅ 已处理）

升级到 lcov 2.5 后，旧脚本/文档的命令会失败。已在 `tools/measure_coverage.sh` 全部修正：

| 旧写法 | 新写法 | 原因 |
|--------|--------|------|
| `--rc lcov_branch_coverage=1` | `--rc branch_coverage=1` | RC 键改名，旧名是 deprecated 硬错误 |
| （无） | `--ignore-errors empty,deprecated,source,inconsistent` | lcov 2.x 把可忽略警告升级为硬错误 |
