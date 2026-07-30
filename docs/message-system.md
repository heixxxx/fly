# Message 日志系统 — 设计文档

> 本文档供 code review 使用，描述需求、架构、数据流、API、文件清单与实现决策。
> 实现已完成并通过全量测试（C++ 单测 + 端到端 QA + 全量 QA）。

---

## 1. 需求背景

fly 现有的 debug log 系统（DBG/INFO/WARN/ERR）每个进程仅写自己的 debug log 文件，信息量大且高度分散。对真实用户而言：冗余信息多、高价值信息被淹没、且分散在各 worker 的日志中难以提取关键信息。

本次新增一套**更高层次的 message 日志系统**，与现有 debug log 并行（debug log 完全不动，仍供研发使用），专门打印流程中的高价值信息，并支持远程集中收集。

### 核心目标
1. **高价值信息的远程推送**：worker 主动把高价值日志推送到 master，master 在 terminal 中直接打印，并单独收集到 `message.log`。
2. **不干扰 debug log**：debug log 仍有价值（给研发看），message 系统与之独立。message 仍会在 debug log 中打印（研发可见），但额外发送给 master。
3. **terminal 行为变更**：master 的 debug log 不再输出到 terminal（`dual_output_ = false`），terminal 唯一输出来源是 message 系统。
4. **配额控制**：避免高频 message 刷屏，支持 message id / domain 两层配额。
5. **summary**：进程结束前 master 收集所有 worker 的 message 触发次数，打印汇总。

---

## 2. message id 规范

### 格式
字符串 `"DOMAIN::NNNN"`：
- **domain**：大写自由命名（如 `SOLVER`、`STORAGE`、`FLY`），用于粗粒度控制。
- **id**：4 位 int，不足补零（如 `0047`）。

### 注册白名单 + 级别绑定
- message id 必须在**发送前注册**进白名单。注册时**绑定级别**（`INFO`/`WARN`/`ERROR`），发送时不再传级别，由 id 查表决定。
- 注册在 Python 侧模块加载时完成。**未注册的 id 视为非法**：直接丢弃，不计入触发次数。
- 重复注册同一 id，以最后一次的级别为准。
- 语义约定：触发某 domain 的 message 时，该 domain 代表的模块一定已被正确加载（即已注册）。

---

## 3. 配额机制（两层独立配额）

| 层级 | 默认配额 | 含义 |
|------|---------|------|
| **domain 配额（粗）** | `-1`（不限制），显式设置才生效 | 该 domain 下所有 id 共享的输出上限 |
| **message id 配额（细）** | `20`（每 id 独立） | 单条 message id 的输出上限 |

### 配额语义
- `-1` = 不限制；`0` = 完全禁止；`N` = 上限 N 次。

### 关键规则：计数与配额分离
- **触发次数永远累加**：不论是否超限丢弃，每次调用都让 id 计数 +1、domain 计数 +1。
- **配额仅控制是否打印/发送**，不影响计数累加。
- 发送一条 message 时，**同时检查 domain 配额和 id 配额**，任一层超限即丢弃（仍各计一次数）。

### 配额作用的两个维度（避免 summary 双算）

这是本系统最关键的设计决策，需重点 review：

message 的「触发」发生在**产生 message 的进程**（worker 或 master）。但 message 还会经过两层配额：

1. **worker 本地配额**（worker 进程的 `MessageRegistry`）：
   - 控制该 worker 是否在本地 debug log 打印、是否推送 master。
   - 超限：本地不打印、不推送 master，但仍累计 worker 本地的触发次数。

2. **master 打印配额**（master 进程的 `MessageSink`，独立于 `MessageRegistry`）：
   - 控制推送到 master 的 message（worker 推来的 + master 自身的）是否在 master 侧打印（`message.log` + terminal）。
   - **不记触发次数** —— 触发发生在 worker，已由 worker 的 `MessageRegistry` 记录。若 master 也用 `MessageRegistry::try_consume` 计数，会导致 summary 把同一次触发在 worker 和 master 各算一次（双算）。
   - 因此 master 用 `MessageSink` 内独立的 `print_counts_` 做打印配额，与 `MessageRegistry` 的触发计数完全隔离。

> **为什么有两套配额？** worker 配额从源头控制流量（避免 worker 疯狂推送）；master 配额在汇聚点控制输出（避免 terminal 刷屏）。两者独立，各管各的维度，summary 只统计触发次数（来源：各进程的 `MessageRegistry`）。

---

## 4. 系统架构

### 4.1 核心组件

| 组件 | 位置 | 职责 |
|------|------|------|
| `MessageRegistry` | `src/message/cpp/` | 进程级单例：白名单（id→级别）、id/domain 两套触发计数、id/domain 配额。`try_consume()` 是核心方法。 |
| `MessageSink` | `src/message/cpp/` | **仅 master 进程**单例：写 `message.log` + 输出 terminal + summary。含独立的 master 打印配额。 |
| `MSG` 宏 | `src/message/cpp/message_macros.h` | C++ 侧发送入口：查级别→try_consume→写本地 debug log→push_message。 |
| `push_message` / `set_message_push_func` | `src/message/cpp/` | 全局推送函数指针：worker 绑定为「发送 master」，master 绑定为 `MessageSink::handle_local`。 |
| `emit_system_message` / `set_system_sink_func` | `src/message/cpp/` | 系统 message（FLY::0000）：豁免配额，master 走 sink，worker 仅本地 debug log。 |
| `LogMessage` / `MessageCountRequestMessage` / `MessageCountReportMessage` | `src/network/cpp/message_types.h` | 三个网络消息类型。 |
| `SystemInfo` | `src/core/cpp/` | 收集机器/网络/运行时信息并格式化（FLY::0000 启动信息）。 |
| `build_info.h` | `src/build_info/`（bazel genrule 生成） | git commit / build type / build time，构建时注入。 |

### 4.2 数据流

```
【worker 进程 — MSG("SOLVER::0047", source, "...") 或 fly.message(id, source, msg)】
  1. MessageRegistry::get_level(id)  → 未注册则丢弃（不计次数）
  2. MessageRegistry::try_consume(id) → id/domain 计数各 +1，检查两层配额
     ├─ 任一层超限 → 丢弃（本地不打印、不推送；次数已计）
     └─ 两层通过
        ├─ 写 worker 本地 debug log（带 [DOMAIN::NNNN] <source> 前缀）
        └─ push_message → WorkerAgentContext → WorkerAgent::send_message_to_master
           → reactor_->send(master_conn_, LogMessage{worker_id, level, id, source, msg})

【master 进程 — MSG(...)/fly.message(...)（本进程）】
  1. get_level + try_consume（master 自己的 MessageRegistry，记录 master 触发次数）
     ├─ 超限 → 丢弃（master 计数仍 +1）
     └─ 通过 → push_message → set_message_push_func 绑定的 MessageSink::handle_local
        → message.log + terminal

【master 进程 — 收到 worker 的 LogMessage】
  on_log_message(conn_id, msg)
   → MessageSink::handle_remote(worker_id, level, id, source, msg)
     内部 print_within_limit（master 打印配额，独立计数，不双算）
     ├─ 超限 → 丢弃
     └─ 通过 → message.log（带 [workerN] 标注）+ terminal

【进程结束 — summary（master 主动拉取）】
  master stop() 新增 Phase（发 ShutdownMessage 之前）:
    广播 MSG_COUNT_REQUEST → 等所有 worker 回 MSG_COUNT_REPORT（计数屏障，复刻 MergeCleanupAck，30s 超时）
    → MessageSink::print_summary(master 本地 MessageRegistry 计数 + 各 worker 上报计数合并)

【master/worker 启动 — FLY::0000 启动信息】
  start() 末尾: SystemInfo::format_startup_info(role, port) → 逐行 emit_system_message
    master: 经 set_system_sink_func 绑定的 MessageSink → message.log + terminal
    worker: system sink 未绑定 → 仅本地 debug log，不发送 master
```

### 4.3 进程结束前 summary 屏障

复刻现有 `MergeCleanupAck` 的计数屏障模式（`master_agent.h` 的 `PendingMsgCount` + `msg_count_cv_`）：

1. master `stop()` 在 Phase 2（发 ShutdownMessage）**之前**插入新 Phase：
   - 快照当前在线 worker 数 `expected = worker_to_conn_.size()`。
   - 广播 `MessageCountRequestMessage` 给所有 worker。
   - `wait_for(msg_count_cv, 30s)`，条件 `received >= expected`。
   - 超时则 WARN 缺失的 worker，用已收到的部分计数打印 summary（容错）。
2. worker 收到 `MSG_COUNT_REQUEST`（`on_message_count_request`）：把 `MessageRegistry` 的两套 snapshot（id_counts / domain_counts）打包成 `MessageCountReportMessage` 发回。
3. master `on_message_count_report`：累加 worker 上报，凑齐 expected 后 notify。
4. `MessageSink::print_summary`：合并 master 自身 `MessageRegistry` 计数 + 各 worker 上报，打印 id 级 + domain 级汇总。

> **时序保证**：summary 收集在发 ShutdownMessage 之前，此时 worker 仍连接，能收到广播并回包。worker 断开后 master 无法再收（复刻 MergeCleanup 的时序约束）。

---

## 5. FLY::0000 启动信息

### 5.1 规则
- **仅在 master 启动时打印**到 `message.log` + terminal；worker 也在本地 debug log 打印（角色标注 worker），但**不发送 master**。
- **豁免配额**：`emit_system_message` 绕过 `try_consume`，不计入 `MessageRegistry` 触发次数，不进 summary。
- **后续重要基础信息统一用 FLY::0000**。

### 5.2 信息内容（4 分组）
```
========== Fly Startup Info (master) ==========
--- Binary ---
  binary   : <绝对路径>
  build    : <type> @ <build time> (commit <git commit>)
--- Machine ---
  host     : <user>@<hostname>
  os       : <sysname release machine>
  cpu      : <model> (<cores> cores)
  pid      : <pid>
  memory   : <free>/<available>/<total> GB
  proc mem : <VmPeak> GB
  disk     : <available>/<total> GB
--- Network ---
  listen   : <ip>:<port>
--- Runtime ---
  log      : <log_dir>
  msg log  : <log_dir>/message.log
  script   : <case script path 或 (none)>
  started  : <启动时间>
```

字段名对齐宽度 12，memory/disk 数值用 `/` 紧凑分隔、单位 GB 仅在末尾。

### 5.3 build info 注入
`src/build_info/BUILD` 用 bazel genrule（`local = True` 访问 .git）在构建时生成 `build_info.h`：
- `kGitCommit`：`git describe --tags --always --dirty`
- `kBuildType`：`fastbuild` / `release`(opt) / `debug`(dbg)
- `kBuildTime`：构建时刻时间戳

### 5.4 system sink 机制
`emit_system_message` + `set_system_sink_func`：master 绑定 sink 为 `MessageSink::handle_local`，worker 不绑定（默认 nullptr）。worker 调用时 sink 为空 → 只写本地 debug log，不发送。

> 注：`handle_local` 带 `honor_quota` 形参。普通 message 路径（`set_message_push_func`）传 `true`（受 master 打印配额控制）；系统信息路径（`set_system_sink_func`）传 `false`（豁免，见 review 问题 1 修复）。

---

## 5.5 业务 message id 注册表

各业务模块在加载时注册自己的 message id，在关键流程节点用 `MSG` 宏 / `fly.message()` 打印。仅挑选低频、用户关心的里程碑（高频/调试性用 debug log）。

| id | 级别 | 节点 | 位置 | source | 说明 |
|----|------|------|------|--------|------|
| `FLY::0000` | INFO | master/worker 启动信息 | `master_agent.cpp`/`worker_agent.cpp` `start()` 末尾 | 0 | 豁免配额，不进 summary（§5） |
| `FLY::0001` | INFO | master drain 完成 | `master_agent.cpp` `do_drain_and_stop()` 开头 | 1 | 与 FLY::0000 对称的关闭里程碑 |
| `AGENT::0001` | INFO | worker 注册上线 | `master_agent.cpp` `on_worker_register` 回 ack 后 | 1 | 集群扩容里程碑 |
| `AGENT::0002` | WARN | worker 断开 | `master_agent.cpp` `on_disconnect`（非 drain 期） | 1 | drain 期不打（避免刷屏） |
| `TASK::0001` | ERROR | task 不可恢复失败 | `master_agent.cpp` `on_task_failed` FATAL 分支 | 1 | 仅 WRITE_REGISTRATION_TIMEOUT / EXECUTION_ERROR |
| `STOR::0001` | INFO | 数据库 freeze 完成 | `master_agent.cpp` `commit_pending_frozen` / `on_master_freeze` 广播后 | 1=task提交 / 2=master直接 | 不可逆里程碑 |
| `STOR::0002` | INFO | merge_db 完成 | `agent.py` `merge_db` 末尾 | 1 | 跨机数据集中里程碑 |
| `STOR::0003` | INFO | load_db 恢复完成 | `agent.py` `load_db` 返回前 | 1 | 系统就绪里程碑 |
| `SOLVER::0001` | INFO | RAS 求解进度 | `ras_graph.py` `ras_graph_check` | 2=每10轮 / 1=收敛 | 迭代收敛观察 |

**注册位置**：C++ 侧 id 在 `MasterAgent::start()` 注册（`MessageRegistry::instance().register_id`）；Python 侧在模块顶层注册（`_msg.register_message_id`，agent.py / ras_graph.py）。

**排除的高频/调试节点**（不加 message）：task 完成、普通 task 失败（有 re-queue）、心跳/revive、自动备份、merge cleanup ack。

---

## 6. API

### 6.1 C++

```cpp
// 注册 message id + 绑定级别（同 id 重复注册以后者为准）
MessageRegistry::instance().register_id("SOLVER::0047", LogLevel::INFO);

// 发送 message（级别由 id 查表，source 为触发位置标识不参与配额）
MSG("SOLVER::0047", 3, "收敛于 {}", residual);

// 配额设置
MessageRegistry::instance().set_id_limit(20);              // 全局 id 配额（默认 20；-1 不限；0 禁止）
MessageRegistry::instance().set_domain_limit("SOLVER", 100); // 单 domain 配额（默认 -1 不限）

// master 打印配额（MessageSink，独立于 MessageRegistry）
MessageSink::instance()->set_print_id_limit(20);
MessageSink::instance()->set_print_domain_limit("SOLVER", 100);
```

**MSG 宏逻辑**：
```cpp
#define MSG(domain_id, source, fmt_str, ...) \
    do { \
        // 1. 查级别；未注册丢弃
        // 2. try_consume（计数 +1，检查 id/domain 两层配额）
        // 3. 通过则写本地 debug log（[DOMAIN::NNNN] <source> 前缀）+ push_message
    } while (0)
```

### 6.2 Python

```python
import fly

# 注册（模块加载时）+ 绑定级别
fly.register_message_id("SOLVER::0047", "INFO")

# 发送（级别由 id 决定，source 标注位置）
fly.message("SOLVER::0047", 3, "收敛于 0.001")

# 配额
fly.set_message_id_limit(20)                 # worker 进程 id 配额
fly.set_message_domain_limit("SOLVER", 100)  # worker 进程 domain 配额
fly.set_master_print_id_limit(20)            # master 打印 id 配额
fly.set_master_print_domain_limit("SOLVER", 100)  # master 打印 domain 配额
```

### 6.3 网络 message 类型

`MessageType` 新增 3 个（上界 45→48）：

| 类型 | 方向 | 用途 |
|------|------|------|
| `LOG_MESSAGE = 46` | worker → master | 推送高价值 message（async, no ack）。字段：worker_id, level, domain_id, source, msg。 |
| `MSG_COUNT_REQUEST = 47` | master → worker (broadcast) | 请求上报 message 触发次数（summary 屏障）。 |
| `MSG_COUNT_REPORT = 48` | worker → master | 上报本地 id/domain 两套计数。字段：worker_id, id_keys/id_values, domain_keys/domain_values。 |

---

## 7. terminal / message.log 行为

| 进程 | debug log（master.log/workerN.log） | message.log | terminal |
|------|-------------------------------------|-------------|----------|
| **master** | 只落盘（`dual_output_ = false`） | master 自身 message + worker 推送来的（集中收集） | message 系统（含启动信息） |
| **worker** | 只落盘（message 也在其中，带前缀） | 不写（无 message.log） | 无 |

打印格式（统一）：
```
[timestamp] [LEVEL] [来源] [DOMAIN::NNNN] <source> msg
```
- 来源：`master` / `worker1` / `worker2`...
- 例：`[2026-07-30 18:00:04] [INFO] [worker1] [SOLVER::0047] <3> 收敛于 0.001`

> `Logger::dual_output_` 改为 `false`（原 `worker_id == 0` 为 true）。这是面向客户的行为变更：所有进程 debug log 只落盘，terminal 唯一来源是 message。

---

## 8. summary 格式

进程结束前 master 打印（同时写 message.log + terminal）：

```
========== Message Trigger Summary ==========
--- By message id ---
  SOLVER::0047 : 22      # = 所有 worker 上报 + master 自身（按 id 字典序、domain 分组）
  SYS::0001    : 2
--- By domain ---
  SOLVER : 22            # = 该 domain 下所有 id 之和
  SYS    : 2
=============================================
```

无任何 message 触发时显示 `(no message triggered)`。

> FLY::0000 豁免配额，**不进 summary**。

---

## 9. 文件清单

### 新增（17 个）

**message 模块**（`src/message/`）：
- `cpp/message_registry.h/.cpp` — 白名单（id→级别）+ 两套触发计数 + id/domain 配额 + `try_consume` + `reset_for_testing`
- `cpp/message_sink.h/.cpp` — master 专用：写 message.log + terminal + 独立打印配额 + print_summary
- `cpp/message_macros.h` — `MSG` 宏 + `push_message`/`emit_system_message` 接口声明
- `cpp/message_dispatch.cpp` — `push_message`/`set_message_push_func`/`emit_system_message`/`set_system_sink_func` 实现
- `cpp/BUILD` — cc_library + cc_shared_library（dynamic_deps fly_log_so）
- `export/BUILD` + `export/message_export.cpp` — Python 绑定 `_fly_message`
- `tests/message_registry_test.cpp` + `tests/BUILD` — 11 个配额/级别单元测试

**build_info 模块**（`src/build_info/`）：
- `BUILD` — genrule 生成 build_info.h（local=True 访问 .git）

**system_info**（`src/core/cpp/`）：
- `system_info.h/.cpp` — 收集机器/网络/运行时信息并格式化

**QA 测试**（`qa/message/`）：
- `_msgtest.py` — 共享工具（日志路径解析、summary 提取）
- `test_message_basic.py` — 基础功能（注册/级别绑定/格式/丢弃）
- `test_message_worker_quota.py` — worker 本地 id 配额
- `test_message_master_quota.py` — master 打印 id 配额
- `test_message_domain_quota.py` — worker 本地 domain 配额
- `test_message_master_zero_domain.py` — master 打印 domain 配额
- `test_message_multi_worker.py` — 多 worker summary 合并
- `test_message_quota_boundary.py` — 配额 -1/0 边界
- `test_message_empty_summary.py` — 无 message summary
- `test_startup_info.py` — FLY::0000 启动信息

**文档**：
- `docs/message-system.md` — 本文档

### 修改（15 个）

| 文件 | 改动 |
|------|------|
| `src/network/cpp/message_types.h` | +3 枚举（46/47/48）+ 3 struct（LogMessage/MessageCountRequest/MessageCountReport）+ 上界 45→48 + include logger.h（LogLevel） |
| `src/network/tests/message_protocol_test.cpp` | +3 往返测试 + is_valid_message_type 上界断言 |
| `src/common/cpp/worker_context.h` | +push_message context 接口（set_push_message_func/push_message，level 用 uint8_t 避免依赖 log 模块） |
| `src/agent/cpp/worker_agent.h/.cpp` | +send_message_to_master（构造 LogMessage 发送）+ begin_task 注册 context + start() set_message_push_func + on_message_count_request（上报计数）+ start() 末尾 FLY::0000 |
| `src/agent/cpp/master_agent.h/.cpp` | +on_log_message（MessageSink::handle_remote）+ on_message_count_report（累加上报）+ collect_and_print_message_summary（屏障）+ stop() 新 Phase + start() MessageSink::init/set_message_push_func/set_system_sink_func + start() 末尾 FLY::0000 |
| `src/log/cpp/logger.h` | +format_log 辅助（MSG 宏复用，避免 message 模块直接链接 fmt） |
| `src/log/cpp/logger.cpp` | `dual_output_ = false`（terminal 行为变更） |
| `src/core/cpp/BUILD` | +fly_build_info 依赖 |
| `src/main/cpp/main.cpp` | setup_sys_path + message 路径 + import _fly_message |
| `src/main/cpp/BUILD` | deps/dynamic_deps/data 加 message |
| `src/fly/__init__.py` | +message/register_message_id/set_message_id_limit/set_message_domain_limit/set_master_print_id_limit/set_master_print_domain_limit + __all__ |
| `fly.sh` | do_install +message 目录/symlink |
| `src/agent/cpp/BUILD` | deps/dynamic_deps 加 fly_message |

---

## 10. 关键实现决策（review 重点）

### 10.1 message 模块不直接链接 fmt
`fly_log_so` 静态链接 fmt 但未导出 fmt 符号。若 message 模块直接 `deps @fmt`，bazel cc_shared_library 会报 `fmt linked statically but not exported`。

**解决**：在 `logger.h` 加 `format_log()` 模板函数（返回格式化字符串），MSG 宏复用它。message 模块只 deps `fly_log`，fmt 符号由 `fly_log_so` 动态提供。

### 10.2 master 打印配额独立于触发计数（避免双算）
见 §3「配额作用的两个维度」。master 收到 worker 推送时用 `MessageSink::print_within_limit`（独立 `print_counts_`），**不调** `MessageRegistry::try_consume`。否则同一次触发会在 worker 和 master 各计一次，summary 翻倍。

### 10.3 WorkerAgentContext 的 level 用 uint8_t
`common` 模块不依赖 `log` 模块。`WorkerAgentContext::push_message` 的 level 参数用 `uint8_t`（LogLevel 的 underlying 值）传递，`WorkerAgent` 实现里 `static_cast<LogLevel>` 还原。

### 10.4 worker 是独立子进程，需各自注册 message id
`launch_local_workers` fork 的 worker 子进程不执行 master 脚本顶部的 `register_message_id`。因此**测试中 worker task 函数内部需自己注册**（进程级白名单）。生产中各模块在 Python 模块初始化时注册，worker 加载该模块即注册。

### 10.5 FLY::0000 豁免配额 + worker 不发送
`emit_system_message` 绕过 `try_consume`（不计数、不进 summary）。`set_system_sink_func` 只 master 绑定（MessageSink），worker 不绑定（仅本地 debug log）。这与「worker 打印但不发送」需求一致。

### 10.6 build_info genrule 用 local=True
bazel sandbox 里 genrule 无法访问 `.git`。`local = True` 禁用 sandbox，genrule 在工作区执行，`git describe` 可用。代价是与工作区布局耦合（构建必在工作区内），对 fly 本地开发模式可接受。

### 10.7 summary 屏障复刻 MergeCleanupAck
直接照搬 `PendingMergeCleanup`（expected/received 计数 + condition_variable）模式，插入 master `stop()` 的 Phase 2 之前。30s 超时容错（worker 崩溃未上报时用部分计数）。

### 10.8 LogLevel 不在 _fly_message 重复导出
`_fly_log.so` 已注册 `fly::LogLevel` 为 `EXLogLevel`。`_fly_message.so` 若再 `FLY_EXPORT_ENUM(LogLevel)` 会导致 nanobind 重复注册崩溃。message export 的 `send_message` 用 `uint8_t` 接收 level，Python 侧 `_MSG_LEVELS` 用整数值（`INFO=1`/`WARN=2`/`ERROR=3`）映射。

---

## 11. 测试覆盖

### C++ 单元测试
- `message_protocol_test.cpp`：3 个新消息类型往返（LogMessage 含 source/level/domain，MessageCountReport 含 4 个 vector）+ is_valid_message_type 上界
- `message_registry_test.cpp`（11 个）：白名单/级别绑定/重复注册覆盖/id 配额(20/-1/0/N)/domain 配额/两层同时检查/extract_domain
- `worker_agent_test.cpp`（回归）：48 个全过

### 端到端 QA（qa/message/，9 个）
| 测试 | 覆盖 |
|------|------|
| test_message_basic | 注册/级别绑定(INFO/WARN/ERROR)/打印格式`[id] <source> msg`/未注册丢弃/message 写本地 debug log |
| test_message_worker_quota | worker 本地 id 配额（默认 20，超限不推送但计数，本地 debug log 同步受控）|
| test_message_master_quota | master 打印 id 配额（控制 worker 推送打印，不影响 summary 计数）|
| test_message_domain_quota | worker 本地 domain 配额（同 domain 多 id 共享，id/domain 两层同时生效）|
| test_message_master_zero_domain | master 打印 domain 配额 |
| test_message_multi_worker | **多 worker summary 合并**（2 worker × 7 + master 3，id 级 + domain 级）|
| test_message_quota_boundary | 配额 -1（不限）/ 0（禁止，不打印但计数 1）|
| test_message_empty_summary | 无 message 触发时的 summary（no message triggered 分支）|
| test_startup_info | FLY::0000 四大分组/关键字段/master+worker 都打印/worker 不发送 master/豁免 summary |

### 验证结果
- C++ 单测：11 + 46 + 48 全过
- 端到端 QA：9 个 message 测试全过
- 全量 QA：127/127 全过（0 回归）

---

## 12. 风险与后续

- **dual_output=false 是面向客户的行为变更**：研发调试时若需 master debug log 上 terminal，后续可加 `--verbose` 开关。
- **业务模块的 message id 已接入**（见 §5.5 注册表）：FLY/AGENT/TASK/STOR/SOLVER domain 的关键里程碑已实现。
- **summary 屏障超时**：worker 崩溃未上报时，master 30s 超时用部分计数打印 summary 并 WARN（复刻 MergeCleanup 容错）。
- **message 推送是 fire-and-forget**（无 ack）：与现有 ObjectRemovedMessage/BackupRequestMessage 一致，符合 best-effort 语义。worker 未连 master 时业务流程本身已崩，无需特殊处理。

---

## 13. Code Review 修复记录

依据 `docs/message-system-review.md`，修复了以下问题：

### 13.1 问题 1（必须修）：master 自身 message 不受配额控制
- **原状**：`MessageSink::handle_local` 直接 `write_line`，bypass master 打印配额，与文档 §3「master 配额控制 worker 推来的 + master 自身的」承诺矛盾。
- **修复**：`handle_local` 加 `honor_quota` 形参。普通 message 路径（`set_message_push_func`）传 `true`，走 `print_within_limit`；系统信息路径（`set_system_sink_func`，FLY::0000）传 `false` 豁免。
- **测试**：增强 `test_message_master_quota.py`，补 master 自身 message 受配额控制的断言。

### 13.2 问题 2（强烈建议修）：std::localtime 多线程不安全
- **原状**：`MessageSink::timestamp()` 用 `std::localtime`（返回 static tm*），reactor 多线程并发调用时 data race / UB。
- **修复**：改用 `localtime_r`（可重入）。`system_info.cpp` 的 `now_str()` 同步修复。

### 13.3 问题 4.2：register_message_id 非法级别静默降级
- **原状**：`parse_level` 对非法级别字符串（如 `"BOGUS"`）静默降级为 INFO。
- **修复**：非法级别直接抛 `ValueError`（`invalid message level '...': must be INFO / WARN / ERROR`）。
