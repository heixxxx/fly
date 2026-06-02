# 覆盖率测试方案

## 1. 概述

本项目使用 GCC gcov + lcov 进行 C++ 代码覆盖率检测。

## 2. 环境要求

| 工具 | 版本 | 用途 |
|------|------|------|
| gcc-12 | 12.5.0 | 编译带覆盖率标志的二进制 |
| gcov-12 | 12.5.0 | 解析 gcda 文件 |
| lcov | 任意 | 收集和生成覆盖率报告 |

## 3. 一键覆盖率检测

```bash
# 1. 清理旧的 gcda 和报告文件
BAZEL_CACHE="/root/.cache/bazel/_bazel_root/$(ls /root/.cache/bazel/_bazel_root/ | head -1)/execroot/_main/bazel-out/k8-fastbuild/bin"
/usr/bin/find $BAZEL_CACHE -name "*.gcda" -delete
rm -f /tmp/coverage*.info

# 2. 编译、运行单元测试（带覆盖率标志）
bazel test //src/... --spawn_strategy=local --copt=--coverage --linkopt=--coverage --cache_test_results=no

# 3. 安装并运行 QA 测试
./fly.sh install
bash qa/run_qa_tests.sh

# 4. 收集覆盖率
lcov --capture \
  --directory $BAZEL_CACHE/src/agent/cpp/_objs \
  --directory $BAZEL_CACHE/src/agent/tests/_objs \
  --directory $BAZEL_CACHE/src/storage/cpp/_objs \
  --directory $BAZEL_CACHE/src/network/cpp/_objs \
  --directory $BAZEL_CACHE/src/network/tests/_objs \
  --directory $BAZEL_CACHE/src/task/cpp/_objs \
  --directory $BAZEL_CACHE/src/task/tests/_objs \
  --directory $BAZEL_CACHE/src/core/cpp/_objs \
  --directory $BAZEL_CACHE/src/log/cpp/_objs \
  --directory $BAZEL_CACHE/src/serialization/cpp/_objs \
  --directory $BAZEL_CACHE/src/main/cpp/_objs \
  --output-file /tmp/coverage.info \
  --rc lcov_branch_coverage=1 \
  --gcov-tool /usr/bin/gcov-12

# 5. 查看报告
lcov --summary /tmp/coverage.info
lcov --list /tmp/coverage.info

# 6. 生成 HTML 报告（可选）
genhtml /tmp/coverage.info --output-directory /tmp/coverage_html --rc lcov_branch_coverage=1
```

## 4. 已知问题

### 4.1 Bazel coverage 命令卡死

`bazel coverage //src/...` 在当前环境（Bazel 9.0.0）的分析阶段会卡死。

**解决方案**: 使用 `bazel test --copt=--coverage --linkopt=--coverage` 代替。

### 4.2 gcov 版本必须匹配

gcda 文件必须用与编译器相同版本的 gcov 解析:
- gcc-12 编译 → gcov-12 解析
- gcov 9.4.0 无法解析 gcc-12 生成的 gcda

### 4.3 runfiles 目录干扰

bazel 的 runfiles 目录包含 gcda 文件的副本，但与 gcno 文件不匹配（stamp mismatch）。lcov 收集时只收集 `_objs` 目录，不要包含 runfiles。

### 4.4 每次运行前必须清理

gcda 文件是累积的，不清理会导致覆盖率数据混淆。每次覆盖率检测前必须:
1. 清理 gcda 文件: `find $BAZEL_CACHE -name "*.gcda" -delete`
2. 清理旧报告: `rm -f /tmp/coverage*.info`

## 5. 测试覆盖范围

### 单元测试 (44 个)

C++ gtest 测试，通过 `bazel test //src/...` 运行。

### QA 测试 (46 个)

端到端集成测试，通过 `bash qa/run_qa_tests.sh` 运行。每个 QA 测试启动独立的 fly 进程，测试完整的 Master/Worker 流程。

## 6. 低覆盖率文件

| 文件 | 行覆盖率 | 原因 | 改进方向 |
|------|----------|------|----------|
| `graceful_exit.cpp` | 33.3% | 调用 _exit()，进程退出 | 进程级集成测试 |
| `main.cpp` | 54.4% | 入口点，信号处理 | 进程级集成测试 |
| `tcp_transport.cpp` | 69.6% | 网络错误路径 | mock 或异常注入 |
| `metadata_client.cpp` | 74.6% | 网络错误路径 | mock 或异常注入 |
| `master_agent.cpp` | 74.4% | 复杂集成逻辑 | 补充集成测试 |
| `worker_agent.cpp` | 69.6% | 复杂集成逻辑 | 补充集成测试 |
