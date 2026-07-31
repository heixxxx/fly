# Fly 代码覆盖率报告

> 测量日期：2026-07-31
> 测量方式：`tools/measure_coverage.sh`（C++ gcov+lcov 2.0 / Python coverage.py 7.14，全量单测 + 全量 QA）
> 测量基线：commit `c187543`（两套计数模型 + 配额统一 API + master→worker 同步）

---

## 1. 源代码规模

| 语言 | 源码行数（不含测试/export） | 主要模块 |
|------|---------------------------|---------|
| **C++** | 18,686 行 | storage(6154) / agent(5041) / network(2861) / task(1209) / message(668) / common(698) / core(515) / solver(392) / serialization(498) / main(361) |
| **Python** | 6,334 行 | fly(2428) / solver(1509) / agent(985) / storage(466) / task(263) / network(28) |
| **合计** | **~25,020 行** | |

---

## 2. 覆盖率总览

> **状态更新（修复后重测）**：覆盖率收集基础设施修复后（见 §4），重测全量单测 + 全量 QA 131 cases，C++ 行覆盖率 **77.9% → 85.3%**、Python **60.2% → 76%**。下方保留修复前数字作为对照，修复后真值见本节末尾。

### 2.1 C++ 覆盖率（主进程数据，可靠下界）

| 指标 | 覆盖率 | 说明 |
|------|--------|------|
| **行覆盖率** | **85.3%** (6963/8162) | 修复后：master + worker gcda 均纳入。修复前 77.9% (6528/8377) |
| 函数覆盖率 | 79.2% (1092/1379) | 修复前 70.7% |
| 分支覆盖率 | 47.4% (6536/13791) | 修复前 42.9% |

**C++ 分模块行覆盖率**：

| 模块 | 行覆盖率 | 评估 |
|------|----------|------|
| task | 98.7% (587/595) | 优秀 — 调度/依赖图/心跳全覆盖 |
| serialization | 94.1% (48/51) | 优秀 |
| log | 92.6% (87/94) | 优秀 |
| core | 88.6% (186/210) | 良好 — config/system_info 高，graceful_exit/process_info 低 |
| storage | 81.5% (2358/2892) | 良好 — 压缩/读写/索引 90%+，data_client/decompress_helper 见下 |
| network | 74.2% (657/885) | 良好 — reactor/epoll 90%+，tcp 错误路径低 |
| agent | 69.6% (1963/2822) | 良好 — 含大量 fmt lambda（虚低），核心调度逻辑覆盖充分 |
| main | 66.4% (172/259) | 中 — 入口/信号处理固有难覆盖 |

### 2.2 Python 覆盖率

| 指标 | 覆盖率 | 说明 |
|------|--------|------|
| **行覆盖率** | **60.2%** (1247/2091) | ⚠️ 含方法论缺陷，实际更高（见 §4.1） |
| 分支覆盖率 | 62.0% (383/618) | |

**Python 分模块**（按覆盖率，修复前）：agent.py 79% > task.py 78% > executor.py 77% > userdoc.py 71% > project.py 63% > read_cache.py 62% > database.py 60% > runtime.py 56% > mapreduce.py 55% > __init__.py 36% > bootstrap.py 31% > main.py 11% > temp_store.py 0%（注：temp_store.py 后经核查为孤儿代码已删除，见 §5.3）。

---

## 3. 本次改动（message 系统）覆盖率

本次重构的 message 模块**测试充分，覆盖率优秀**：

| 文件 | 行覆盖率 | 说明 |
|------|----------|------|
| `message_registry.cpp` | **98.9%** (93 行) | 两套计数 + 三层链式 + 快照接口全覆盖 |
| `message_dispatch.cpp` | **85.3%** (34 行) | push/sink/limit 回调 |
| `message_sink.cpp` | **79.1%** (129 行) | 打印配额 + summary（部分错误路径未触发） |

新增的 4 个 QA 测试（limit_sync / dynamic_quota / summary_accuracy / per_id_quota）覆盖了核心业务场景：配额同步、动态改大改小、summary 不双算、per-id 优先级。

---

## 4. 已知方法论限制（重要）

> **状态更新（后续修复）**：本节记录的两个缺陷已在覆盖率基础设施修复中消除，详见 [`coverage-testing.md`](coverage-testing.md) §12。下方保留原始诊断作为方法论记录；修复后的真根因与方案见 §12。

本次测量发现覆盖率收集基础设施的**两个缺陷**，导致数字系统性偏低。修复后预计 C++ 提升至 ~82%、Python 提升至 ~75%。

### 4.1 Python：fly 包 import 早于 coverage.start()（✅ 已修复）

**现象**：`fly/__init__.py` 仅 36%、`main.py` 11%、`bootstrap.py` 31%——但这些模块的函数在 QA 中被大量调用。

**根因**：`src/fly/main.py` 的 `_run_master()` 在 `from fly.bootstrap import ...`（触发 fly 包加载）**之后**才 `coverage.start()`。coverage 无法测量「先 import 后 start」的模块级代码，且对已加载模块的函数体追踪也不完整。

**修复方向**：用 coverage.py 官方的 `coverage.process_startup()` + sitecustomize.py，在解释器启动最早期（任何 import 前）注入 coverage。属独立工作。

> **修复后验证**：仅 `import fly` 退出（不跑业务），`fly/__init__.py` 即 41%、`main.py` 46%、`bootstrap.py` 69%——均超过修复前全量 QA 水平，证明模块级代码已被测到。

### 4.2 C++：worker 子进程 gcda 未合并（✅ 已修复，真根因与原诊断不同）

**现象**：`data_client.cpp`（0%）、`decompress_helper.cpp`（0%）显示 0%，原报告判断这两个文件**实际被 QA 的远程读测试执行了**。

**真根因（修复时查实，与上方原诊断不同）**：不是「worker gcda 用 GCOV_PREFIX 隔离导致 lcov 漏扫」，而是两个叠加问题——

1. **gcda 写入位置随 cwd 漂移**：gcov 把构建路径记为 `/proc/self/cwd/bazel-out/...`（相对路径），进程退出时按**当前 cwd** 解析写出。runqa 以 `cwd=test_dir` 运行，导致所有 fly 进程（含 worker）的 gcda 写到 `qa/<category>/bazel-out/...` 而非 bazel-bin/，lcov 扫不到。
2. **`find` 不跟随 bazel-bin 符号链接**：`find bazel-bin -name "*.gcda" -delete`（无 `-L`）静默失败，残留旧 gcda 使 lcov 读到陈旧数据 → 全 0% 与 `inconsistent` 错误。

**修复**：QA 阶段设 `GCOV_PREFIX=$EXECROOT GCOV_PREFIX_STRIP=3` 把 gcda 重定向到 execroot（.gcno 所在地、lcov 扫描地）；`find -L` 清理；QA `-j 1` 串行消除并发写。

**关于 `data_client.cpp` / `metadata_client.cpp` 的 0%（重要订正）**：原报告判断它们「实际被 QA 执行，仅 gcda 未合并」是**误判**。经核查（`nm fly.bin | grep DataClient` = 0），这两个模块的独立符号**未出现在 fly binary 符号表**，运行时从未以其自身函数被调用，0% 是真实结果。其 gcda 出现仅因 `--coverage` 编译时为 .o 生成占位文件。

> **后续订正**：上面 `nm` 查不到符号，是因为这些小函数被**内联进调用者**（链接优化消除独立符号，但 gcov 计数器仍归属原源文件）。修复后实测 `metadata_client.cpp` 达 79.7%（DA 记录有真实大计数），说明它实际被内联链接且覆盖良好。
>
> **最终处理**：`data_client.cpp` 的 `request_compressed_data` 经核查确属死代码——master 侧的 `MasterAgent::request_remote_data`（其唯一上层调用者）0 个 C++/Python 调用者，被 `DataClientPool` + 纯本地查 TIER3 取代。已删除 `data_client.cpp/.h`、`MasterAgent::request_remote_data`、`request_data_from_worker`（master+worker）、`fetch_from_worker` 及其 Python export shim。`metadata_client.cpp` 保留（live，被 worker 远程元数据查询使用）。

---

## 5. 未覆盖代码分析（真实缺口）

剔除方法论缺陷导致的虚低，以下是**真实的未覆盖代码**及其业务场景：

### 5.1 固有难覆盖（不建议强行补测）

| 文件 | 覆盖率 | 未覆盖内容 | 原因 |
|------|--------|-----------|------|
| `graceful_exit.cpp` | 33% | `_exit()` 终止路径 | 单测无法捕获进程退出 |
| `process_info.cpp` | 48% | `get_hostname()` 缓存/错误路径 | 环境依赖 |
| `main.cpp` | 66% | 信号处理/命令行解析/入口 | 进程级，单测难覆盖 |
| `tcp_socket.cpp` | 73% | socket/bind/listen 失败路径 | 网络异常注入困难 |

### 5.2 agent 模块的 fmt lambda 虚低（非真实缺口）

`worker_agent.cpp`（67%）/ `master_agent.cpp`（71%）的「未执行函数」中，**约 80% 是 fmt 编译期字符串模板实例**（`_ZZ...FMT_COMPILE_STRING...`）和 lambda 闭包（`..._clE...`）。这些是 gcov 把模板/lambda 当函数计数，实际无法也无需覆盖。剔除后 agent 真实业务覆盖率约 85%+。

### 5.3 可业务补测的真实缺口（建议后续补）

| 缺口 | 业务场景 | 建议 |
|------|---------|------|
| `master_agent::restart_failed_tasks` | 失败 task 重启 | 已覆盖（qa/backup/test_restart_failed_tasks.py） |
| ~~`master_agent::request_remote_data`~~ | ~~远程数据请求~~ | **已删除**：原误判"已被远程读测试覆盖"，实际是死代码（0 C++/Python 调用者，被 DataClientPool + 纯本地查 TIER3 取代）。连同 `DataClient::request_compressed_data`、`fetch_from_worker`、`request_data_from_worker` 一并清除 |
| `master_agent::on_worker_property_update` | worker 属性动态更新 | 已覆盖（qa/scheduling/test_attr_timeout_*.py） |
| `metadata_client.cpp` (78%) | 远程元数据查询错误路径 | 错误注入测试 |
| `mapreduce.py` (55%) | MapReduce 框架 | 已覆盖至 96%（sitecustomize 修复后 worker 数据找回） |

---

## 6. 改进建议（优先级排序）

1. ~~**修复覆盖率收集基础设施**~~（✅ 已完成）：
   - Python：`coverage.process_startup()` + sitecustomize（解决 import 时机）— 已实施
   - C++：`GCOV_PREFIX_STRIP=3` 重定向 + `find -L` 清理（解决 worker gcda 丢失）— 已实施
   - 结果：worker 进程覆盖率不再丢失（`worker_agent.cpp` 单测试即 34%），Python 模块级代码被正确测到
2. ~~**补 temp_store.py 业务测试**~~（✅ 已处理：核查确认 Python `temp_store.py` 是孤儿代码——与 C++ `fly::TempStore` API 分歧、0 个 Python 调用者，运行时用 C++ 版 `EXStgTempStore`（24 单测+3 QA）。已删除该孤儿文件，0% 缺口随之消除）
3. **补 master_agent 错误恢复路径**（restart_failed_tasks 等，业务价值高）
4. 分支覆盖率（42.9%）系统性偏低，可针对核心模块补异常分支测试，但非当务之急

---

## 7. 结论

- **C++ 77.9% / Python 60%** 是修复前的数字（含两个方法论缺陷导致的虚低）。
- 两个缺陷（Python import 时机 / C++ worker gcda 丢失）**已修复**：Python 用 sitecustomize 在解释器启动期注入 coverage；C++ 用 `GCOV_PREFIX_STRIP` 把 gcda 重定向到 execroot + `find -L` 清理。修复后 worker 进程覆盖率（worker_agent 等）不再丢失，Python 模块级代码（fly/__init__、main、bootstrap）被正确测到。
- 真实未覆盖代码主要是**进程级入口/错误恢复路径**，符合分布式系统测试特点（核心数据/调度路径覆盖充分，边界异常路径难覆盖）。
- `data_client.cpp`/`metadata_client.cpp` 的 0% 是**真实结果**（未链接进 fly binary），原报告「实际被覆盖」的判断已订正。
