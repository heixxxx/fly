# 覆盖率测试方案

## 1. 概述

本项目使用 GCC gcov + lcov 进行 C++ 代码覆盖率检测。

**当前覆盖率** (2026-06-03):
- 行覆盖率: 90.2% (12525/13881)
- 函数覆盖率: 88.7% (13547/15270)

## 2. 环境要求

| 工具 | 版本 | 用途 |
|------|------|------|
| gcc-12 | 12.5.0 | 编译带覆盖率标志的二进制 |
| gcov-12 | 12.5.0 | 解析 gcda 文件（必须与 gcc 版本匹配） |
| lcov | 任意 | 收集和生成覆盖率报告 |

## 3. 完整覆盖率检测流程

```bash
# 1. 定义 bazel 缓存路径
BAZEL_CACHE="/root/.cache/bazel/_bazel_root/$(ls /root/.cache/bazel/_bazel_root/ | head -1)/execroot/_main/bazel-out/k8-fastbuild/bin"

# 2. 清理旧的 gcda 和报告文件（必须！gcda 是累积的）
/usr/bin/find $BAZEL_CACHE -name "*.gcda" -delete
rm -f /tmp/coverage*.info

# 3. 编译带覆盖率标志
bazel build //src/... --copt=--coverage --linkopt=--coverage

# 4. 直接运行测试二进制（关键！不能用 bazel test）
for test_bin in $(rtk ls bazel-bin/src/agent/tests/*_test \
    bazel-bin/src/common/tests/*_test \
    bazel-bin/src/core/tests/*_test \
    bazel-bin/src/export/tests/*_test \
    bazel-bin/src/fly/tests/*_test \
    bazel-bin/src/log/tests/*_test \
    bazel-bin/src/network/tests/*_test \
    bazel-bin/src/serialization/tests/*_test \
    bazel-bin/src/storage/tests/*_test \
    bazel-bin/src/task/tests/*_test 2>/dev/null); do
  echo "Running: $(basename $test_bin)"
  timeout 60 $test_bin 2>/dev/null
done

# 5. 安装并运行 QA 测试
./fly.sh install
bash qa/run_qa_tests.sh

# 6. 收集覆盖率（只收集源码目录，不收集测试目录）
lcov --capture \
  --directory $BAZEL_CACHE/src/agent/cpp/_objs \
  --directory $BAZEL_CACHE/src/storage/cpp/_objs \
  --directory $BAZEL_CACHE/src/network/cpp/_objs \
  --directory $BAZEL_CACHE/src/task/cpp/_objs \
  --directory $BAZEL_CACHE/src/core/cpp/_objs \
  --directory $BAZEL_CACHE/src/log/cpp/_objs \
  --directory $BAZEL_CACHE/src/serialization/cpp/_objs \
  --directory $BAZEL_CACHE/src/main/cpp/_objs \
  --output-file /tmp/coverage.info \
  --rc lcov_branch_coverage=1 \
  --gcov-tool /usr/bin/gcov-12

# 7. 查看报告
lcov --summary /tmp/coverage.info
lcov --list /tmp/coverage.info

# 8. 生成 HTML 报告（可选）
genhtml /tmp/coverage.info --output-directory /tmp/coverage_html --rc lcov_branch_coverage=1
```

## 4. 关键问题与解决方案

### 4.1 Bazel sandbox 导致 gcda 写入错误位置

**问题**: `bazel test` 在 sandbox 中运行，gcda 文件写入 `runfiles/` 目录而非 `_objs/` 目录。lcov 从 `_objs/` 收集，所以找不到数据，报告 0% 覆盖率。

**验证**:
```bash
# runfiles 目录有 gcda
find . -path "*runfiles*/*.gcda" | wc -l  # 大量

# _objs 目录没有
find . -path "*_objs*/*.gcda" ! -path "*runfiles*" | wc -l  # 0
```

**解决**: 直接运行测试二进制，不用 `bazel test`。二进制直接运行时 gcda 写入真实的 `_objs/` 目录。

### 4.2 `bazel coverage` 命令卡死

**问题**: `bazel coverage //src/...` 在 Bazel 9.0.0 的分析阶段无限循环。

**原因**: Bazel 9.0.0 的 aspect 应用遍历依赖图时出现死循环。

**解决**: 不用 `bazel coverage`，用 `bazel build --copt=--coverage --linkopt=--coverage` + 直接运行二进制。

### 4.3 gcov 版本不匹配

**问题**: lcov 报 `stamp mismatch with notes file` 或 `cannot open notes file`。

**原因**: 二进制用 gcc-12 编译，但 lcov 默认用系统 gcov (9.4.0)，版本不匹配。

**解决**: 必须指定 `--gcov-tool /usr/bin/gcov-12`。

### 4.4 runfiles 目录干扰

**问题**: lcov 处理 runfiles 目录的 gcda 时报错。

**原因**: runfiles 目录只有 gcda（运行时生成），没有 gcno（编译时生成），stamp 不匹配。

**解决**: lcov 只收集 `_objs` 目录，不包含 runfiles。不要用 `--include '*/_objs/*'`（会过滤掉外部库），直接指定具体目录。

### 4.5 gcda 累积导致数字混乱

**问题**: 多次运行后覆盖率数字忽高忽低。

**原因**: gcda 文件是累积的，不清理会混入旧数据。不同编译产生的 gcda 不能合并。

**解决**: 每次覆盖率检测前必须清理所有 gcda 文件和旧报告。

### 4.6 测试文件被计入覆盖率

**问题**: 覆盖率报告包含 `*_test.cpp` 文件，虚高覆盖率数字。

**原因**: 测试文件本身也有代码，运行时被覆盖，拉高平均值。

**解决**: lcov 只收集 `src/*/cpp/_objs` 目录，不收集 `src/*/tests/_objs` 目录。

### 4.7 Python 测试无法直接运行

**问题**: `python3 src/fly/tests/test_user_task.py` 报 `ModuleNotFoundError`。

**原因**: 测试依赖 Bazel 构建的 nanobind 模块，需要正确的 Python path。

**解决**: Python 测试通过 `bazel test //src/fly/tests:*_test` 运行，不直接用 python3。

## 5. 覆盖率模块详情

| 模块 | 行覆盖率 | 函数覆盖率 | 评估 |
|------|----------|-----------|------|
| dependency_graph.cpp | 100% | 100% | 优秀 |
| heartbeat_monitor.cpp | 100% | 100% | 优秀 |
| storage_manager.cpp | 100% | 100% | 优秀 |
| task_manager.cpp | 100% | 100% | 优秀 |
| temp_store.cpp | 100% | 100% | 优秀 |
| compressor.cpp | 97.6% | 100% | 优秀 |
| worker_manager.cpp | 96.9% | 93.8% | 优秀 |
| task_scheduler.cpp | 96.4% | 100% | 优秀 |
| data_service.cpp | 93.2% | 98.4% | 优秀 |
| local_index.cpp | 94.3% | 66.7% | 良好 |
| data_writer.cpp | 94.1% | 87.5% | 良好 |
| compression_utils.cpp | 94.9% | 100% | 优秀 |
| lz4_compressor.cpp | 94.9% | 69.2% | 良好 |
| zlib_compressor.cpp | 93.3% | 72.7% | 良好 |
| zstd_compressor.cpp | 92.3% | 72.7% | 良好 |
| config.cpp | 92.7% | 100% | 优秀 |
| logger.cpp | 94.7% | 100% | 优秀 |
| reactor.cpp | 95.1% | 87.5% | 优秀 |
| io_thread_pool.cpp | 96.1% | 100% | 优秀 |
| data_reader.cpp | 96.3% | 88.2% | 优秀 |
| master_agent.cpp | 82.8% | 71.2% | 良好 |
| worker_agent.cpp | 80.6% | 76.1% | 良好 |
| database.cpp | 80.1% | 70.6% | 良好 |
| tcp_transport.cpp | 70.9% | 82.6% | 需改进 |
| metadata_client.cpp | 74.6% | 100% | 良好 |
| main.cpp | 54.4% | 28.6% | 需改进 |
| graceful_exit.cpp | 33.3% | 50.0% | 需改进 |

## 6. 低覆盖率文件分析

| 文件 | 行覆盖率 | 原因 | 改进方向 |
|------|----------|------|----------|
| `graceful_exit.cpp` | 33.3% | 调用 _exit()，进程退出 | 进程级集成测试 |
| `main.cpp` | 54.4% | 入口点，信号处理，命令行解析 | 进程级集成测试 |
| `tcp_transport.cpp` | 70.9% | 网络错误路径（socket/bind/listen 失败） | mock 或异常注入 |

## 7. Python 测试覆盖率

Python 测试通过 Bazel 运行：
```bash
bazel test //src/fly/tests:user_task_test
bazel test //src/fly/tests:executor_test
bazel test //src/fly/tests:main_test
```

Python 测试覆盖 `task/py/task.py`、`agent/py/agent.py`、`agent/py/executor.py` 等模块。
