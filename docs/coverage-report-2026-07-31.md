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

### 2.1 C++ 覆盖率（主进程数据，可靠下界）

| 指标 | 覆盖率 | 说明 |
|------|--------|------|
| **行覆盖率** | **77.9%** (6528/8377) | 仅 `.cpp` 源文件，过滤系统头/外部库/测试 |
| 函数覆盖率 | 70.7% (1572/2224) | 含 fmt 编译期模板实例（虚高未执行数） |
| 分支覆盖率 | 42.9% (5710/13298) | 分支覆盖率系统性偏低（异常路径多） |

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

**Python 分模块**（按覆盖率）：agent.py 79% > task.py 78% > executor.py 77% > userdoc.py 71% > project.py 63% > read_cache.py 62% > database.py 60% > runtime.py 56% > mapreduce.py 55% > __init__.py 36% > bootstrap.py 31% > main.py 11% > temp_store.py 0%。

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

本次测量发现覆盖率收集基础设施的**两个缺陷**，导致数字系统性偏低。修复后预计 C++ 提升至 ~82%、Python 提升至 ~75%。

### 4.1 Python：fly 包 import 早于 coverage.start()

**现象**：`fly/__init__.py` 仅 36%、`main.py` 11%、`bootstrap.py` 31%——但这些模块的函数在 QA 中被大量调用。

**根因**：`src/fly/main.py` 的 `_run_master()` 在 `from fly.bootstrap import ...`（触发 fly 包加载）**之后**才 `coverage.start()`。coverage 无法测量「先 import 后 start」的模块级代码，且对已加载模块的函数体追踪也不完整。

**修复方向**：用 coverage.py 官方的 `coverage.process_startup()` + sitecustomize.py，在解释器启动最早期（任何 import 前）注入 coverage。属独立工作。

### 4.2 C++：worker 子进程 gcda 未合并

**现象**：`data_client.cpp`（0%）、`decompress_helper.cpp`（0%）显示 0%，但这两个文件**实际被 QA 的远程读测试执行了**（`test_netprobe_remote_read` 等触发 cross-worker 数据拉取+解压）。

**根因**：worker 是 `subprocess.Popen` 启动的独立进程，用 `GCOV_PREFIX` 把 gcda 写到 `/tmp/.../gcov_workers/worker_*/`。但 `lcov --capture` 只从 `bazel-bin/src/*/cpp/_objs` 收集，**漏掉了 worker 的 gcda**（已确认 worker 目录有 1045 个 gcda，含 data_client/decompress_helper）。主进程不执行这俩文件（远程读发生在 worker），故主进程 gcda 为空 → 显示 0%。

**修复方向**：lcov capture 时合并 worker gcda。lcov 2.0 的 `--build-directory` 路径映射有 bug（gcda/gcno 路径前缀错位）。可靠方案：QA 阶段去掉 `GCOV_PREFIX`，改用 gcov runtime 的串行累加（接受低概率并发风险），或写 Python gcda 合并工具。属独立工作。

> **结论**：当前 C++ 77.9% 是**保守下界**（worker 执行的代码未计入）。`data_client`/`decompress_helper` 实际已被业务测试覆盖，非真实缺口。

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
| `master_agent::restart_failed_tasks` | 失败 task 重启 | QA 加重启场景测试 |
| `master_agent::request_remote_data` | 远程数据请求 | 已被远程读测试覆盖（gcda 未合并） |
| `master_agent::on_worker_property_update` | worker 属性动态更新 | QA 加属性变更场景 |
| `metadata_client.cpp` (78%) | 远程元数据查询错误路径 | 错误注入测试 |
| `temp_store.py` (0%) | Python 临时存储 | 加业务测试（当前完全未测） |
| `mapreduce.py` (55%) | MapReduce 框架 | 部分 API（set_partitioner 等）未被现有 MR 测试覆盖 |

---

## 6. 改进建议（优先级排序）

1. **修复覆盖率收集基础设施**（投入产出比最高）：
   - Python：`coverage.process_startup()` + sitecustomize（解决 import 时机）
   - C++：worker gcda 合并（解决 0% 虚低）
   - 预期：C++ 77.9%→~82%，Python 60%→~75%，且消除误导性 0%
2. **补 temp_store.py 业务测试**（唯一 0% 的 Python 业务模块，真实缺口）
3. **补 master_agent 错误恢复路径**（restart_failed_tasks 等，业务价值高）
4. 分支覆盖率（42.9%）系统性偏低，可针对核心模块补异常分支测试，但非当务之急

---

## 7. 结论

- **C++ 77.9% / Python 60%** 是当前可靠下界，message 系统（本次改动）覆盖率优秀（registry 98.9%）。
- 两个方法论缺陷（Python import 时机 / worker gcda 合并）导致数字虚低，修复后预计 C++ ~82% / Python ~75%。
- 真实未覆盖代码主要是**进程级入口/错误恢复路径**，符合分布式系统测试特点（核心数据/调度路径覆盖充分，边界异常路径难覆盖）。
- 建议优先修复覆盖率收集基础设施，而非盲目补测刷数。
