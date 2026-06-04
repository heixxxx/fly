# 覆盖率测试方案

## 1. 概述

本项目使用 GCC gcov + lcov 进行 C++ 代码覆盖率检测。编译使用 `--config=coverage`（定义在 `.bazelrc`），该配置启用 `--copt=--coverage --linkopt=--coverage` 并设置 `--spawn_strategy=standalone` 以保留 gcno 文件。

**当前覆盖率** (2026-06-04，仅源文件，不含测试/系统头)：
- 行覆盖率: 88.4% (4251/4807)
- 函数覆盖率: 81.9% (704/860)
- 分支覆盖率: 49.7% (3610/7265)

## 2. 环境要求

| 工具 | 版本 | 用途 |
|------|------|------|
| gcc-12 | 12.5.0 | 编译带覆盖率标志的二进制 |
| gcov-12 | 12.5.0 | 解析 gcno/gcda（**必须与 gcc 版本匹配**） |
| lcov | 任意 | 收集和生成覆盖率报告 |
| genhtml | 任意 | 生成 HTML 可视化报告 |

## 3. 完整流程

### Step 1: 定义路径

```bash
BAZEL_CACHE="/root/.cache/bazel/_bazel_root/$(ls /root/.cache/bazel/_bazel_root/ | head -1)/execroot/_main/bazel-out/k8-fastbuild/bin"
```

### Step 2: 清理旧数据

gcda 是累积文件，不同编译的 gcda 不能混合。每次测试前必须清理。

```bash
/usr/bin/find $BAZEL_CACHE -name "*.gcda" -delete
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

QA 测试通过 fly 进程运行，需要先安装：

```bash
./fly.sh install
bash qa/run_qa_tests.sh
```

**验证**：`Passed: 47, Failed: 0`。

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
  --rc lcov_branch_coverage=1 \
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
  --rc lcov_branch_coverage=1
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

> 注意：2026-06-03 的 90.2% 包含测试文件和系统头，不可与 06-04 的 88.4%（纯源文件）直接比较。逐文件对比见 §5。

## 5. 模块覆盖率详情

按行覆盖率降序排列（2026-06-04，源文件口径）：

| 模块 | 行覆盖率 | 函数覆盖率 | 行数 | 评估 |
|------|----------|-----------|------|------|
| dependency_graph.cpp | 100% | 100% | 86 | 优秀 |
| storage_manager.cpp | 100% | 100% | 34 | 优秀 |
| task_manager.cpp | 100% | 100% | 79 | 优秀 |
| temp_store.cpp | 100% | 100% | 88 | 优秀 |
| heartbeat_monitor.cpp | 100% | 100% | 21 | 优秀 |
| net_utils.cpp | 100% | 100% | 21 | 优秀 |
| decompress_helper.cpp | 100% | 100% | 12 | 优秀 |
| process_info.h | 100% | 100% | 1 | 优秀 |
| compressor.cpp | 97.6% | 100% | 41 | 优秀 |
| write_back_queue.cpp | 96.6% | 100% | 59 | 优秀 |
| worker_manager.cpp | 96.9% | 93.8% | 127 | 优秀 |
| task_scheduler.cpp | 96.4% | 100% | 56 | 优秀 |
| io_thread_pool.cpp | 96.1% | 100% | 77 | 优秀 |
| data_reader.cpp | 96.3% | 88.2% | 54 | 优秀 |
| compression_utils.cpp | 94.9% | 100% | 39 | 优秀 |
| lz4_compressor.cpp | 94.9% | 69.2% | 39 | 良好 |
| logger.cpp | 94.7% | 100% | 94 | 优秀 |
| data_writer.cpp | 94.1% | 87.5% | 85 | 良好 |
| object_header.cpp | 94.1% | 100% | 51 | 良好 |
| local_index.cpp | 94.3% | 66.7% | 209 | 良好 |
| config.cpp | 93.6% | 100% | 47 | 优秀 |
| data_service.cpp | 93.2% | 98.4% | 591 | 优秀 |
| compressing_streambuf.cpp | 93.0% | 83.3% | 43 | 优秀 |
| database.cpp | 92.7% | 76.5% | 341 | 良好 |
| zlib_compressor.cpp | 93.3% | 72.7% | 45 | 良好 |
| zstd_compressor.cpp | 92.3% | 72.7% | 39 | 良好 |
| decompressing_streambuf.cpp | 90.2% | 66.7% | 61 | 良好 |
| reactor.cpp | 90.1% | 87.5% | 81 | 优秀 |
| master_agent.cpp | 87.6% | 75.1% | 1033 | 良好 |
| worker_agent.cpp | 84.7% | 80.2% | 693 | 良好 |
| task_executor.cpp | 82.9% | 100% | 35 | 良好 |
| data_client.cpp | 79.2% | 100% | 48 | 良好 |
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

## 8. Python 测试

Python 测试依赖 Bazel 构建的 nanobind C++ 模块，不能直接 `python3` 运行。通过 `bazel test` 执行：

```bash
bazel test //src/fly/tests:user_task_test
bazel test //src/fly/tests:executor_test
bazel test //src/fly/tests:main_test
```

Python 测试覆盖 `task/py/task.py`、`agent/py/agent.py`、`agent/py/executor.py` 等模块。Python 覆盖率不计入本报告的 C++ 覆盖率数字。
