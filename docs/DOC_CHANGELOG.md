# 文档变更记录

---

## 2026-05-30: backup 数据复制 — 压缩传输零解压落盘

| 文档 | 变更 |
|------|------|
| docs/architecture/overview.md | 数据副本策略从"低/未实现"更新为"已完成：backup=True 压缩传输零解压落盘" |
| docs/python-api/module.md | write_object/read_object/write_object_raw/read_object_raw 签名新增 backup=False 参数说明 |
| docs/storage/module.md | read_object/read_object_typed 签名新增 backup=false；新增 backup_object() 和 persist_read_result() 声明 |

---

## 2026-05-25: 优雅关机 + workers_mutex_ 线程安全

| 文档 | 变更 |
|------|------|
| docs/agent/module.md | 关机流程重写为"优雅关机（Graceful Shutdown）"：drain 语义、pending task 持久化、stop 幂等；MasterAgent 成员变量新增 draining_/shutdown_requested_/fatal_error_/workers_mutex_/drain_mutex_/drain_cv_/drain_thread_；WorkerAgent 新增 shutdown_triggered_；schedule_tasks 新增 draining early return；on_disconnect 新增 draining 跳过恢复逻辑；设计决策表新增 workers_mutex_/stop 幂等/SIGTERM Python 层处理 |
| CLAUDE.md | 新增 Agent 工作指南 §7 禁止项：禁止归因为 pre-existing bug、禁止忽略 crash/不稳定性、崩溃与不稳定性零容忍 |

代码变更摘要：
- `master_agent.h/cpp`: stop() 改为 drain 语义（广播 shutdown → 等待 running tasks → persist pending → cleanup）；schedule_tasks() draining early return；on_task_failed 设 fatal_error_；on_disconnect draining 跳过恢复；新增 workers_mutex_ 保护 conn_to_worker_/worker_to_conn_ 全部并发访问（修复 SIGSEGV）；新增 persist_pending_tasks()、build_failed_record()、notify_drain_if_active()、check_shutdown_request()（dead code）、do_drain_and_stop()
- `worker_agent.h/cpp`: initiate_shutdown() 幂等（shutdown_triggered_）；stop() → initiate_shutdown() → do_cleanup()
- `reactor.h`: 新增 is_running()、get_io_pool()
- `data_service.h/cpp`: stop_transfer_server() 中 reset transfer_pool_；新增 reset() 公共方法
- `main.py`: SIGTERM handler → SystemExit(0) → cleanup
- `master_agent_test.cpp`: 4 new tests (StopWithPendingTasks, StopNoRunningTasks, StopIdempotent, StopBeforeStart)
- `worker_agent_test.cpp`: 1 new test (InitiateShutdownFromOnDisconnect)
- qa/: 5 new files (test_graceful_shutdown, test_shutdown_broadcast, test_pending_task_persist + 2 helpers)

---

## 2026-05-25: FlyBuffer 统一 + 流式管线架构重构

| 文档 | 变更 |
|------|------|
| docs/storage/module.md | 写入流程改为"流式管线 + 异步落盘"架构；新增 FlyBufferStreamBuf/CountingStreamBuf 组件描述；DataWriter 新增 compress_to_buffer/write_record |
| CLAUDE.md | 存储层文件表更新（database/data_writer/fly_buffer_stream）；序列化部分新增 FlyBuffer 说明；写入架构约束更新 |

代码变更摘要：
- `fly_buffer.h`: FlyBuffer 内部存储从 `CMVector<uint8_t>` 改为 `CMString`，消除 char↔uint8_t 阻抗失配；新增 `take(CMString&&)` / `release()` 支持零拷贝
- `serialization_macros.h`: FlySerBuf 改为 FlyBuffer 别名；FLY_ENCODE/DECODE 去掉 std::transform 转换；新增 bitsery traits 特化
- `fly_buffer_stream.h`（新建）: FlyBufferStreamBuf（streambuf→FlyBuffer）+ CountingStreamBuf（字节计数）
- `data_writer.h/cpp`: 新增 `compress_to_buffer`（流式管线：FlyBufferStreamBuf→CompressingStreamBuf→FlyBuffer）和 `write_record`（仅 file_stream_.write + index 更新）
- `database.h/cpp`: write_object 模板改为调用线程 serialize+compress → WBQ 仅 write_record；新增 write_object_raw_ptr 接受裸指针
- `export_macros.h`: `__getstate_buffer__` 改用 FLY_ENCODE_TO_BYTES 直接写入 FlyBuffer
- `storage_export.cpp`: 新增 `_write_pickle_bytes`（Python bytes 裸指针直接进 compress_to_buffer）和 `_write_raw_ptr`
- `database.py`: Python pickle 路径改用 pickle.dumps + _write_pickle_bytes
- `data_reader.cpp`: 读取路径 `FlySerBuf(str.begin(), str.end())` 改为 `take(std::move(str))` 零拷贝

---

## 2026-05-25: DataService 两层索引重构 + 并发 Bug 修复

| 文档 | 变更 |
|------|------|
| docs/storage/module.md | `local_idx_`/`remote_idx_` 类型从 `Map<full_name, ...>` 改为两层嵌套 `Map<db_id, Map<short_name, ...>>`；新增 `split_full()` 定长切分（32 字符 db_id） |

代码变更摘要：
- `data_service.h/cpp`: `local_idx_`/`remote_idx_` 改为两层索引；`split_full()` 使用 32 字符定长切分；`on_flush(db_id)` 优化为 O(该 db 条目)
- `local_index.h/cpp`: 加 `std::mutex` 保护所有公共方法；`save()` 锁内取快照锁外做 I/O（修复 WBQ 线程与主线程数据竞争导致的 std::bad_alloc）
- `tcp_transport.cpp`: `send()` 处理 partial send 和 EAGAIN（poll+retry 循环）
- `worker_agent.cpp`: `on_remove_command` 提取 short name（修复 double-prefix）
- `database.cpp`: `freeze()` 不再关闭 DataWriter（修复 in-flight write 竞争）
- `main.cpp`: 新增 `std::set_terminate()` crash handler + SIGABRT/SIGSEGV handler + backtrace
- 4 个测试文件改为使用 32 字符 db_id

---

## 2026-05-24 (7): Bug 修复 + 压力测试 + freeze 机制完善

| 文档 | 变更 |
|------|------|
| CLAUDE.md | 新增 §QA 测试与 test 模块；新增 §内部接口；test 模块描述；消息结构数量 26→27 |
| qa/README.md | 新增 §6 压力测试（7 覆盖场景 + 2 未覆盖场景） |
| docs/test/module.md | 新增 `increment`、`write_after_freeze` 任务文档 |
| docs/network/module.md | 消息结构数量 26→27；新增 `DatabaseFreezeNotification` |
| docs/architecture.md | 消息结构数量 23→27 |
| docs/architecture/overview.md | 消息类型总览 22→26+header |
| docs/DOC_CHANGELOG.md | 本条记录 |

---

## 2026-05-23 (5): writer_id UUID 解耦 idx/data 文件命名

**原因**: load_db 时 worker_id 与 idx 文件名耦合导致冲突限制，Master 无法在任意机器上重启

| 模块文档 | 主要变更 |
|----------|----------|
| CLAUDE.md | §13 load_db 流程重写（Phase 0 冲突检查移除、Phase 2 不加载 idx、Phase 3 hostname 复用、Phase 4 含 Master writer_id）；§8 MasterAgent/WorkerAgent 模块描述更新；_DB_META WorkerInfo 新增 writer_id 字段 |
| docs/storage/module.md | DataWriter/DataReader 构造函数参数 `uint64_t worker_id` → `const CMString& writer_id`；私有成员 `worker_id_` → `writer_id_` |
| docs/agent/module.md | `restore_master_idx`/`send_idx_load_commands` 签名更新（writer_ids）；load_db 流程重写（Phase 2-5 全部更新） |
| docs/network/module.md | `IdxLoadCommandMessage.old_worker_ids` → `writer_ids` (CMVector\<CMString\>) |

**代码变更摘要**：
- `writer_id`: 8-char hex UUID，Database 构造时生成，用于 idx/data 文件命名
- 文件命名：`{writer_id}.idx`、`data_{writer_id}_{index:03}.dat`（替代 `worker_{id}.idx`、`aggregated_w{id}_*.dat`）
- `DataReadyMessage` 新增 `writer_id` 字段，Worker/Master 均填充
- `IdxLoadCommandMessage.old_worker_ids` → `writer_ids`
- `rebuild_remote_idx`：统一路径，worker_id==0 不再特殊处理，所有条目按 hostname 映射到新 Worker
- Master load_db 时不加载任何 idx 到 local_idx，所有旧数据通过 remote_idx 经 Worker 提供
- `MasterAgent` 新增 `get_worker_hostnames()`、`add_worker_hostname()`
- `register_worker(0, host_, port_)` 在 `start()` 中调用（Master 新数据仍需被 Worker 读取）
- `recorded_workers_` key 从 `pair<hostname, worker_id>` 改为 `tuple<db_id, hostname, writer_id>`
- agent.py load_db 重写：hostname-based worker 复用，Master writer_id 含入 idx load commands
- 新增 3 个多 hostname 单元测试覆盖 idx 分配场景
- 3 个网络测试 flaky 修复（poll loop 替代 sleep+assert）

---

## 2026-05-23 (4): load_db 增强 + 跨 DB QA 测试

**原因**: 连续 load_db 多个 DB 时 `_next_worker_id` 回退导致 worker ID 冲突；load_db 恢复后数据未标记依赖就绪

| 模块文档 | 主要变更 |
|----------|----------|
| CLAUDE.md | §load_db 完整流程增加 Phase 0 冲突检查、Phase 2 mark_data_ready + frozen 恢复、连续 load_db 说明 |

**代码变更摘要**：
- `agent.py` `load_db`: 新增 worker ID 冲突检查（重叠 → RuntimeError）；`_next_worker_id` 取 `max(当前, max(old)+1)` 不回退
- `database.cpp`: 构造函数检测 `_FROZEN` 标记恢复 `is_frozen_` 状态
- `master_agent.cpp`: `recorded_workers_` 从 `pair<hostname,worker_id>` 改为 `tuple<db_id,hostname,worker_id>`（per-DB 记录）；`restore_master_idx` + `rebuild_remote_idx` 新增 `mark_data_ready`
- 新增 6 个跨 DB e2e_tasks：cross_db_copy, cross_db_sum, add_alpha_property, alpha_cross_db_copy, gpu_cross_db_copy, triple_db_sum
- 新增 QA 测试 test_complex_scenario.py：2 进程协调器，覆盖多 DB、跨 DB 依赖、load_db 双 DB 迁移、动态属性、restart_failed_tasks、triple-DB 计算（12 个验证点）

---

## 2026-05-23 (3): 异步写入依赖调度重构

**原因**: `write_object` 异步写入时立即触发依赖满足，移除 `restart_failed_tasks` 中的 `drain_write_back` 同步阻塞

| 模块文档 | 主要变更 |
|----------|----------|
| CLAUDE.md | 重写 §restart_failed_tasks API（简化流程）、重写 §写入注册触发依赖满足（Worker/Master 端分离、线程安全） |
| docs/agent/module.md | WriteRegisterMessage 语义变更（增加 mark_data_ready + update_remote_idx） |

**代码变更摘要**：
- `on_write_register`: 收到 Worker WriteRegisterMessage 后立即 `mark_data_ready` + `update_remote_idx` + `schedule_tasks`
- `setup_write_context`: Master 端新增 `master_register_write_trampoline`，`write_object` 时同步触发 `mark_data_ready`
- `restart_failed_tasks`: 移除 `drain_write_back` + 手动依赖检查，简化为直接 `submit_task`
- `schedule_tasks`: 新增 `schedule_mutex_` 防止 WriteBackQueue 工作线程与 Python 线程并发导致重复 fail/persist

---

## 2026-05-23 (2): db_id UUID v4 + open_db 路径递增

**原因**: db_id 从 hash(base_path) 改为 UUID v4；open_db 检测已有 DB 时自动递增路径

| 模块文档 | 主要变更 |
|----------|----------|
| CLAUDE.md | 新增 §open_db vs load_db 路径检测表（递增路径）、§db_id 生成（UUID v4）、DataService db_paths_ register/unregister 行为 |
| docs/python-api/module.md | 新增 §open_db 路径检测（自动递增 `.1` `.2`... + WARN）、db_id UUID v4 说明 |
| docs/storage/module.md | Database 构造函数注释（UUID v4、析构 unregister）、DataService register_database 严格检查注释 |

---

## 2026-05-23 (1): load_db 文档同步

**原因**: load_db 功能实现完成后，同步更新所有相关模块文档

### 变更汇总

| 模块文档 | 主要变更 |
|----------|----------|
| storage/module.md | Database 构造函数新增 `existing_db_id`，DataService 新增 `has_database()`、`restore_entries()`、`DbPaths` struct |
| agent/module.md | MasterAgent 新增 `restore_master_idx()`、`send_idx_load_commands()`、`rebuild_remote_idx()`、hostname 映射、`on_idx_load_ack()` handler；WorkerAgent 新增 `on_idx_load_command()` handler；新增 load_db 恢复流程文档 |
| network/module.md | 消息类型 24→26 种；RegisterMessage 新增 `hostname`、`ip_address` 字段；新增 `IdxLoadCommandMessage`(type=25)、`IdxLoadAckMessage`(type=26) |
| python-api/module.md | FlyAgent 新增 `load_db()`、`wait_for_all_workers()`；`_deserialize_args` 增加 `has_database` 检查说明；新增 load_db 使用示例 |
| superpowers/plans/2026-05-22-* | DbMetaHeader/DbMeta 移除 `base_path`；Phase 3 改为 process worker；Phase 4 增加 `register_database` 步骤 |
| CLAUDE.md | §8 DataService (db_paths_, has_database)、§13 load_db 流程更新（process worker, base_path 移除） |

---

**日期**: 2026-05-21
**原因**: 文档与实现代码存在大量不一致，本次批量修正

---

## 一、变更汇总

| 模块 | 差异数 | 主要变更 |
|------|--------|----------|
| log | 18 | 架构从多实例改为单例，API完全重写 |
| network | 28 | 新增MasterClient，TransportLayer/Reactor签名变更 |
| task | 40 | WorkerInfo字段重设计，TaskScheduler签名变更 |
| storage | 22 | 写流程改为异步WriteBackQueue，DataReader实例化 |
| agent | 23 | WorkerAgentContext从指针改为回调模式 |
| core | 7 | 新增8个int+6个string配置项 |

---

## 二、详细变更

### 2.1 log/module.md — 架构完全重写

**旧设计（文档）**:
- 多实例模式：`CMMap<CMString, Logger>` 存储多个Logger
- `get_master()`, `get_worker(worker_id)` 分角色获取
- `init_master(path)`, `init_worker(worker_id, path)` 分角色初始化
- `debug(component, msg)` 两参数日志方法
- 日志格式：`[timestamp] [LEVEL] [component] msg`

**新设计（实际代码）**:
- 单例模式：`static Logger* instance_`
- `Logger& instance()` 统一获取
- `init(base_dir, worker_id)` 统一初始化
- `debug(msg)` 单参数日志方法（无component）
- 日志格式：`[timestamp] [LEVEL] msg`
- 日志rotation：`resolve_log_dir()` 创建版本化目录 + `.latest` symlink
- 宏定义：`DBG(msg)`, `INFO(msg)`, `WARN(msg)`, `ERR(msg)`
- Python导出：`init_log`, `shutdown_log`, `flush_log`, `set_log_level`, `DBG/INFO/WARN/ERR`

---

### 2.2 network/module.md

**新增类**:
- `MetadataClient`（原名 `MasterClient`） — 阻塞TCP客户端，查询Master数据位置
  - `query_data_location(host, port, object_name)` → `DataLocation`

**签名变更**:
- `TransportLayer::accept()` → **已移除**，改为 `stop_listening()`
- `TransportLayer::get_bound_port()` 返回 `int` 而非 `int32_t`
- `Reactor` 构造函数：`CMUniquePtr` 而非 `std::unique_ptr`
- `Reactor::set_io_pool()`：`CMSharedPtr` 而非 `IOThreadPool*`
- `Reactor::recv_buffers_`, `handlers_`：`CMUnorderedMap` 而非 `CMMap`
- `IOThreadPool` 构造函数接收 `thread_count`，`start()` 无参数
- `DataClient::request_data()` 返回 `std::tuple<bool, CMString, CMString>` 而非 `DataResponse`

**新增Reactor方法**:
- `on_connect(handler)`, `on_disconnect(handler)`, `on_error(handler)`
- `run_once(timeout_ms)`, `get_bound_port()`, `connect(host, port)`

**消息类型修正**:
- 22种枚举值，但仅17种有对应struct（DATABASE_FREEZE等5种无struct）

---

### 2.3 task/module.md

**DependencyGraph变更**:
- `mark_data_ready()` 返回 `void` 而非 `CMVector<uint64_t>`
- 新增 `get_pending_tasks()`, `is_task_ready()`
- `has_task()` 已移除

**WorkerInfo重设计**:
- 旧：`role` (string), `attributes`, `is_busy` (bool), `last_heartbeat` (double)
- 新：`address`, `port`, `capabilities`, `status` (WorkerStatus enum), `last_heartbeat` (uint64_t)
- WorkerStatus枚举：`IDLE=0, BUSY=1, DEAD=2`

**WorkerManager签名变更**:
- `register_worker(id, address, port, capabilities)` 而非 `register_worker(id, role, attributes)`
- 新增：`unregister_worker()`, `update_worker_status()`, `assign_task()`, `get_workers_with_capability()`
- 移除：`has_worker()`, `get_available_workers()`, `get_all_worker_ids()`, `update_attributes()`

**TaskScheduler变更**:
- 构造函数：`DependencyGraph*`, `WorkerManager*` 原始指针而非shared_ptr
- `submit_task()` 已移除（任务提交在MasterAgent层）
- `schedule_next()` → `ScheduleResult`
- `schedule_all_available()` → `CMVector<ScheduleResult>`
- 新增 `ScheduleResult` 结构：`{task_id, worker_id, scheduled}`

**MetadataManager变更**:
- TaskStatus枚举：`PENDING=0, RUNNING=1, COMPLETED=2, FAILED=3, CANCELLED=4`（无READY）
- `create_task(id, name, inputs, outputs, config)` 新增outputs/config参数
- `get_task()` 返回指针而非值
- 新增字段：`outputs`, `config`, `created_at`, `started_at`, `completed_at`, `error_message`, `assigned_worker_id`

**HeartbeatMonitor变更**:
- 构造函数：`WorkerManager*` 原始指针 + `timeout` 参数
- 默认timeout：30秒而非120秒
- `check_all_workers(uint64_t)` 而非 `check_all_workers(double)`
- `set_timeout(uint64_t)` 而非 `set_timeout(double)`

---

### 2.4 storage/module.md

**Database变更**:
- 构造函数：`writer_id` 类型 `uint64_t` 而非 `int`，新增 `host` 参数
- `flush()` 不再作为公共方法（异步WriteBackQueue）
- 新增：`load_meta()`, `set_db_id()`, `reset()`
- 写流程：异步入队 `WriteBackQueue`，非阻塞返回

**DataWriter变更**:
- 构造函数：11个参数而非2个
- 新增：`close()`, `total_bytes_written()`, `file_count()`
- `get_last_entry(object_name)` 返回指针，需object_name参数

**DataReader变更**:
- 所有方法从 `static` 改为实例方法
- 构造函数：`DataReader(base_path, data_path, worker_id)`
- 新增：`exists()`, `read_object<T>()`

**DataService变更**:
- `try_read_local()` 返回 `std::pair<bool, ReadResult>` 而非 `ReadResult`
- `lookup_remote_idx()` 返回 `RemoteObjectInfo` 结构而非输出参数
- 新增：`register_database()`, `unregister_database()`, `has_local_object()`, `has_remote_location()`
- 新增：`start_transfer_server()`, `stop_transfer_server()`, `drain_write_back()`
- 移除：`process_completions()`

**IndexEntry变更**:
- 版本：3而非2
- `block_count`：`int32_t` 而非 `int`
- `compression_type`：`int8_t` 而非 `CompressionType` 枚举
- 新增 `host` 字段

**Compressor变更**:
- 从静态工具类改为虚接口
- 实例方法：`compress()`, `decompress()`, `compress_chunk()`, `decompress_chunk()`
- 工厂方法：`create()`, `create_from_name()`

---

### 2.5 agent/module.md

**WorkerAgentContext重构**:
- 旧：`set(WorkerAgent*)` 指针存储 + `current()` 获取
- 新：`set(RecordWriteFunc, void* ctx)` 函数指针回调 + `record_write()` 调用回调
- 文件位置：`src/common/cpp/worker_context.h` 而非 `src/agent/cpp/`

**MasterAgent变更**:
- `get_port()`：`uint16_t` 而非 `int`
- `get_pending/running/completed_tasks()`：返回 `CMVector<uint64_t>` 而非 `int`
- 新增：`set_data_service()`, `get_connected_workers()`, `register_database()`, `is_db_frozen()`, `request_remote_data()`, `request_data_from_worker()`
- 移除：`on_data_query()`（改为 `on_data_ready()`）

**WorkerAgent变更**:
- `poll_task()`：返回 `bool` 而非 `void`
- `request_data_from_worker()`, `request_remote_data()`：返回 `ReadResult` 而非 `DataResponse`
- 新增：`get_worker_id()`, `set_executor()`, `begin_task()`, `record_write()`, `end_task()`, `register_write_with_master()`
- 消息处理器：移除 `conn_id` 参数
- 移除：`on_data_location()` 处理器

**TaskExecutor变更**:
- TaskExecStatus枚举新增 `TIMEOUT=2`
- 新增：`clear_exec_func()`, `is_running()`, `cancel()`

---

### 2.6 core/module.md

**新增int配置项**:
- `worker_mode`, `worker_id`, `compression_level`, `compression_threshold`, `compression_stream_chunk_size`, `dependency_update_mode`, `interactive`, `cli_master_port`

**新增string配置项**:
- `transport_type`, `compression_type`, `data_server_host`, `master_host`, `log_dir`, `script_path`

**值修正**:
- `large_file_threshold`：已修正为 `67108864`（64MB），新增 `large_file_threshold_kb`（65536，用户可配置 KB 单位）
- `database.cpp` 使用 `large_file_threshold_kb * 1024` 计算字节阈值

**Python导出修正**:
- 移除 `FLY_EXPORT_INIT()`（实际不存在）
- 新增：`mark_workers_launched`, `is_workers_launched`, `reset`
- `ex_core_get_config`：返回指针而非引用

---

## 三、文档修正状态

| 文档 | 状态 |
|------|------|
| log/module.md | 已修正 |
| storage/module.md | 已修正 |
| network/module.md | 已修正 |
| task/module.md | 已修正 |
| agent/module.md | 已修正 |
| core/module.md | 已修正 |
| architecture/overview.md | 已修正 |
| DEVELOPMENT_GUIDELINES.md | 无需修正 |
| superpowers/plans/*.md | 保留历史记录，不修正 |

---

## 四、代码修正建议

| 位置 | 问题 | 建议 |
|------|------|------|
| `config.cpp:66` | `large_file_threshold = 10485710` | **已修正**: 改为 64MB (`67108864`)，新增 `large_file_threshold_kb = 65536`，database.cpp 使用 `large_file_threshold_kb * 1024` |
| `master_client.h/cpp` | `MasterClient` 命名不准确 | **已修正**: 重命名为 `MetadataClient`，功能为元数据查询 |