# Fly 架构深度审查报告

> 审查日期: 2026-06-12（2026-06-13 复核验证）
> 审查范围: C++ 核心层、Python API/绑定层、网络通信与分布式架构
> 状态标记: `[待修复]` / `[需验证]` / `[建议]` / `[已记录]` / `[经验证无风险]` / `[经验证无需修复]` / `[误报]`

---

## 一、架构分层违规

### 1.1 storage → network 跨层依赖 [误报]

- **严重程度**: ~~高~~ → 误报
- **文件**: `src/storage/cpp/BUILD:19`, `src/storage/cpp/data_service.h:12`, `src/storage/cpp/database.cpp:4`
- **原描述**: storage 反向依赖 network，network 运行时需要 storage 的 DataService。
- **验证结论**: 依赖方向正确。`network/BUILD` deps 仅有 `common`、`log`、`serialization`，**零依赖 storage**。network 是底层基础设施（与 log/core 同层），storage 是上层消费者。实际层次为 `common → log → serialization → core → network → storage → task → agent`。storage 使用 `IOThreadPool`、`DataClient` 是消费底层组件的正常行为，不构成跨层违规。

### 1.2 export_macros 依赖 storage [建议]

- **严重程度**: 中
- **文件**: `src/export/cpp/BUILD`, `src/export/cpp/export_macros.h:9`
- **问题**: export 宏库（基础设施工具）反向依赖了具体业务模块 storage。`export_macros.h` 中 `#include <storage/cpp/decompressing_streambuf.h>`。
- **建议**: 由使用方引入 storage 依赖，宏库本身只依赖 serialization。

### 1.3 task BUILD 依赖声明不完整 [建议]

- **严重程度**: 中
- **文件**: `src/task/cpp/BUILD`
- **问题**: deps 只有 `//src/common/cpp:fly_common_types`，但实际代码中 HeartbeatMonitor、TaskScheduler 等间接使用了 log 和 core。
- **建议**: 补全 BUILD 中对 log 和 core 的依赖声明。

---

## 二、并发安全缺陷

### 2.1 Logger singleton 内存安全 [已修复]

- **严重程度**: ~~高~~ → 已修复
- **文件**: `src/log/cpp/logger.h`, `src/log/cpp/logger.cpp`
- **修复**: `instance()` 返回 `CMSharedPtr<Logger>`，`CMMakeShared<Logger>` + public 构造，`init()` 原地配置，`shutdown()` 只关文件不释放对象。消除 `instance_ptr()` 重复接口和裸引用解引用。

### 2.2 transfer_callback_ 无锁并发访问 [经验证无风险]

- **严重程度**: ~~高~~ → 无风险
- **文件**: `src/storage/cpp/data_service.cpp:917 vs 982`
- **验证结论**: `start_transfer_server()` 仅在 `MasterAgent::start()`/`WorkerAgent::start()` 中调用（单线程启动阶段），`submit_transfer()` 仅在运行阶段 IOThreadPool 中调用。写入先于读取，通过 `transfer_pool_->start()` 的线程同步保证 happens-before。不存在并发读写场景。

### 2.3 drain_thread_.detach() [已修复]

- **严重程度**: ~~高~~ → 已修复
- **文件**: `src/agent/cpp/master_agent.cpp`
- **原问题**: `drain_thread_.detach()` 使线程脱离生命周期管理，SIGTERM 关闭时 pending tasks 可能写入不完整。
- **修复方案**: 改为 `drain_thread_.joinable()` 同步等待。SIGTERM 路径：drain_thread 只 persist + 清理非 reactor 资源；reactor_thread lambda 在 run() 退出后 join drain_thread，再清理 reactor/transfer_server。正常 stop() 路径不变。

### 2.4 graceful_exit shutdown_callback [经验证无风险]

- **严重程度**: ~~高~~ → 无风险
- **文件**: `src/core/cpp/graceful_exit.cpp:15-17`
- **验证结论**: `register_shutdown_callback` 只在 `MasterAgent::start()`/`WorkerAgent::start()` 中调用一次（单线程启动阶段），`graceful_exit()` 在运行/关闭时调用。写先于读，经典 "write-then-read" 模式，不构成实际 race。`exit_initiated.exchange(true)` 保证 graceful_exit 只执行一次。

### 2.5 WorkerAgentContext thread_local 全局状态 [建议]

- **严重程度**: 高
- **文件**: `src/common/cpp/worker_context.h:13-120`
- **问题**: 所有成员变量都是 `static inline thread_local`，本质上是线程级全局状态容器。`clear()` 方法将 function 设为 nullptr，但在任务执行期间被意外调用会导致静默丢失回调。`is_active()` 仅检查 `record_write_func_` 不为空，无法判断其他回调的完整性。
- **建议**: 改为实例对象，通过 TaskExecutor 传递而非依赖 thread_local。

### 2.6 Config set_int/set_str 非线程安全 [建议]

- **严重程度**: 中
- **文件**: `src/core/cpp/config.cpp:16-20`
- **问题**: 修改 `int_values_` / `str_values_` 无锁保护，仅通过 `workers_launched_` 标志做运行时检查。
- **建议**: 添加 mutex 或改为 `std::atomic` / concurrent hashmap。

### 2.7 DataService 单一 mutex 粒度 [建议]

- **严重程度**: 中
- **文件**: `src/storage/cpp/data_service.h:253`
- **问题**: 一个 `mutable std::mutex mutex_` 保护 `local_idx_`、`remote_idx_`、`worker_registry_`、`db_paths_` 等所有共享数据。高并发场景下成为性能瓶颈，且某些方法内部先锁 mutex_ 再锁 cv_mutex_，存在死锁风险。
- **建议**: 按数据域拆分锁（如 idx_mutex_, registry_mutex_）。

### 2.8 MasterAgent 多锁嵌套风险 [已记录]

- **严重程度**: 中
- **文件**: `src/agent/cpp/master_agent.h:119-191`
- **问题**: `schedule_tasks()` 方法中可能同时持有 `schedule_mutex_` + `task_args_mutex_` + `workers_mutex_`，嵌套加锁顺序需仔细维护。
- **建议**: 文档化锁获取顺序，考虑使用层次锁（hierarchical locking）或 `std::scoped_lock` 多锁。

---

## 三、网络通信问题

### 3.1 HandlerThreadPool 定义但未使用 [待修复]

- **严重程度**: 高
- **文件**: `src/network/cpp/reactor.cpp:101-103`
- **问题**: `HandlerThreadPool` 已定义但 `handle_event` 中没有 `handler_pool_->submit()` 调用。所有 handler 在 reactor 线程中同步执行。如果某个 handler 阻塞（如 `schedule_tasks`），整个 reactor 停止处理所有连接。
- **建议**: 将耗时 handler（schedule_tasks, on_task_complete 广播等）提交到 HandlerThreadPool 执行。

### 3.2 非阻塞 connect 未处理 EINPROGRESS [待修复]

- **严重程度**: 中（原标高，实际影响被高估）
- **文件**: `src/network/cpp/tcp_transport.cpp:92-110`
- **问题**: 非阻塞 `connect()` 返回 EINPROGRESS 后，仅注册 EPOLLIN，未注册 EPOLLOUT 确认连接完成。标准模式应：connect 后注册 EPOLLOUT → 等 EPOLLOUT 触发 → `getsockopt(SO_ERROR)` 确认连接成功 → 切换 EPOLLIN。
- **实际影响评估**: 不构成数据丢失。`send()` 方法自带 `poll(POLLOUT, 30000)` 兜底——连接握手未完成时 `::send()` 返回 EAGAIN，`poll` 等待握手完成后继续发送。连接失败由 reactor 的 `EPOLLERR|EPOLLHUP` 路径检测（`tcp_transport.cpp:244-261`），触发 `on_disconnect` → `initiate_shutdown`。真正缺陷是 **连接状态无显式通知**：`connect()` 立即返回 conn_id，调用方无法区分"已连接"和"正在连接"，失败只能通过 send 返回 -1 或异步 ERROR 事件发现。
- **建议**: 在 `connect()` 内同步 `poll(POLLOUT, 5000)` + `getsockopt(SO_ERROR)` 确认连接成功后再返回 conn_id，失败则直接 throw。

### 3.3 recv_buffers_ 无大小限制 [经验证可接受]

- **严重程度**: ~~高~~ → 无风险
- **验证结论**: 内网部署不存在恶意节点。大消息（如 DataResponse 的压缩数据）可能合法占用大量 buffer，设置上限会误杀正常传输。帧结构校验（MessageType 范围 + total_len 有效性）已足以检测异常数据。

### 3.4 序列化无数据大小验证 [已修复]

- **严重程度**: ~~高~~ → 已修复
- **文件**: `src/network/cpp/message_protocol.h`, `src/network/cpp/message_types.h`
- **修复方案**: 添加 `is_valid_message_type()` 校验函数（枚举范围 1-33），`MessageProtocol::decode` 和 `get_type` 中校验帧头 type 字段。异常帧（type 不在有效范围、total_len < 1）直接跳过。bitsery 的 `CheckDataErrors=false` 保留，因为大消息（如压缩数据）的容器大小可能合法地很大。

### 3.5 Worker 无 Master 重连机制 [经验证无需修复]

- **严重程度**: ~~高~~ → 无需修复
- **文件**: `src/agent/cpp/worker_agent.cpp:393-398`
- **验证结论**: 正确的设计决策。Master 是唯一调度源（DependencyGraph、WorkerManager、TaskManager 全在 Master 内存中），Master 丢失 = 全部调度状态丢失。即使 Worker 重连成功，新 Master 也不知道此 Worker 的 in-progress task，会导致重复执行或状态不一致。已有持久化恢复机制：Master 在 SIGTERM 路径 `persist_pending_tasks`，重启后从文件恢复，Worker 重启后重新注册。进程级重启（由 Python `main.py` / supervisor fork 新进程）是正确的恢复路径，而非 Worker 自行重连。

### 3.6 Reactor send 可阻塞 30 秒 [待修复]

- **严重程度**: 高
- **文件**: `src/network/cpp/tcp_transport.cpp:128-153`
- **问题**: 非阻塞 socket 的 send 在 EAGAIN 时使用 `poll(POLLOUT, 30000)` 同步等待。reactor 线程的 send 操作会阻塞最多 30 秒。
- **建议**: 改为异步发送（send queue + EPOLLOUT 触发实际发送），或大幅缩短超时（如 100ms）后断开连接。

### 3.7 消息无协议版本号 [建议]

- **严重程度**: 中
- **文件**: `src/network/cpp/message_types.h`
- **问题**: 无协议版本号字段，不支持向后兼容的协议升级。修改消息结构后新旧版本无法互通。
- **建议**: 在 MessageHeader 中添加 `uint8_t version` 字段。

### 3.8 MessageHeader 冗余字段 [建议]

- **严重程度**: 中
- **文件**: `src/network/cpp/message_types.h:46-53`
- **问题**: `message_id` 和 `timestamp` 在所有消息中定义但从未被设置，浪费序列化带宽。
- **建议**: 移除未使用的字段，或按需设置。

### 3.9 get_type() 默认值可能导致误判 [建议]

- **严重程度**: 中
- **文件**: `src/network/cpp/message_protocol.h:61-72`
- **问题**: buffer 不完整时 `get_type()` 返回 `MessageType::REGISTER` 作为默认值，可能误判消息类型。
- **建议**: 返回 `std::optional<MessageType>` 或添加 `UNKNOWN` 哨兵类型。

### 3.10 重试无退避 [建议]

- **严重程度**: 中
- **文件**: `src/agent/cpp/worker_agent.cpp:514-545`
- **问题**: `request_remote_data` 有 3 次重试但间隔为 0，连续重试可能加剧过载。
- **建议**: 添加指数退避间隔。

### 3.11 DataClientPool release 不验证 fd 有效性 [建议]

- **严重程度**: 中
- **文件**: `src/network/cpp/data_client_pool.cpp:122-141`
- **问题**: `release()` 未用 `getsockopt(SO_ERROR)` 检查 fd 是否仍然有效，坏 fd 可能被放回池中。
- **建议**: release 前检查连接状态。

---

## 四、Python 层问题

### 4.1 shared_from_this() bad_weak_ptr [经验证无风险]

- **严重程度**: ~~高~~ → 无风险
- **文件**: `src/agent/export/agent_export.cpp:150,257`
- **验证结论**: DataService 已继承 `std::enable_shared_from_this`（`data_service.h:67`），singleton 通过 `CMMakeShared<Creator_>()` 创建（`data_service.cpp:27-30`）。`shared_from_this()` 操作的是有效的 shared_ptr 控制块，不会抛 `bad_weak_ptr`。此问题在 ISSUES.md X-2 中已修复。

### 4.2 Worker.poll_task_blocking 后不可达死代码 [已修复]

- **严重程度**: ~~高~~ → 已清理
- **文件**: `src/agent/py/agent.py:500-516`
- **验证结论**: 503 行 `return` 后是 `set_worker_property` 等正常方法定义，死代码已不存在。review 描述的复制粘贴问题在当前代码中已清理。

### 4.3 solver 绑定绕过 FLY_EXPORT_* 宏 [建议]

- **严重程度**: 中
- **文件**: `src/solver/export/solver_export.cpp`（15 处: 53,67,95,127,145,194,198,203,208,214,219,236,242,260,281）
- **问题**: solver 是唯一不使用 `FLY_EXPORT_*` 宏的模块，其余 7 个模块全部使用宏。
- **建议**: 统一改为 `FLY_EXPORT_FUNCTION` 宏。

### 4.4 solver 多个导出函数未在 Python __all__ 列出 [建议]

- **严重程度**: 低
- **文件**: `src/solver/py/__init__.py`
- **问题**: `ex_slv_extract_subdomain_matrix_oras`、7 个 `ex_slv_vec_*` 向量运算函数、`EXSlvSparseMatrix` 类在 C++ 中导出但未纳入 Python `__all__`。
- **建议**: 补全 `__all__` 列表或确认是否为有意隐藏。

### 4.5 TaskScheduler 暴露裸指针到 Python [建议]

- **严重程度**: 中
- **文件**: `src/task/export/task_export.cpp:83`
- **问题**: `FLY_EXPORT_INIT(fly::DependencyGraph*, fly::WorkerManager*)` 暴露裸指针，Python 侧需手动管理生命周期。
- **建议**: 改为 shared_ptr 或引用绑定。

### 4.6 main.py 无单元测试 [建议]

- **严重程度**: 中
- **文件**: `src/fly/main.py`
- **问题**: 204 行代码包含 worker 启动、master 启动、IO 重定向、SIGTERM 处理等关键逻辑，无单元测试覆盖。
- **建议**: 添加单元测试，至少覆盖启动参数解析、mode 判断、cleanup 逻辑。

### 4.7 测试文件硬编码 bazel-bin 路径 [建议]

- **严重程度**: 中
- **文件**: `src/test/py/test_tasks.py:4-14`, `src/fly/tests/test_main.py:8-13`, `src/fly/tests/test_executor.py:11-18`
- **问题**: `sys.path.insert(0, '../../../bazel-bin/...')` 在 build/ layout 下不工作。
- **建议**: 使用环境变量或动态检测路径。

### 4.8 ras_graph.py 零类型注解 [建议]

- **严重程度**: 中
- **文件**: `src/solver/py/ras_graph.py`
- **问题**: 809 行复杂算法代码无任何函数类型注解。
- **建议**: 为公共 API（`solve_ras_graph`, `get_ras_graph_solution`）添加类型注解。

### 4.9 main.py _cleanup 裸 except:pass [建议]

- **严重程度**: 中
- **文件**: `src/fly/main.py:29,38,47,54,75`
- **问题**: 5 处 `except Exception: pass` 静默吞掉所有异常，无日志输出，排障困难。
- **建议**: 至少添加 `import logging; logging.debug(...)` 记录被忽略的异常。

### 4.10 测试框架未使用 pytest fixture [建议]

- **严重程度**: 中
- **文件**: `src/fly/tests/test_main.py` 等
- **问题**: 所有测试用裸 `assert` + 手动 `_run_all()`，缺少 fixture、参数化、setup/teardown 生命周期管理。
- **建议**: 迁移至 pytest fixture 模式。

---

## 五、性能瓶颈

### 5.1 消息编解码路径冗余内存拷贝 [建议]

- **严重程度**: 中
- **文件**: `src/network/cpp/message_protocol.h:14-58`, `src/network/cpp/reactor.cpp:148,170-194`
- **发送路径**: 2 次拷贝（bitsery 序列化 → frame copy）
- **接收路径**: 5-6 次拷贝（recv += 拼接 → temp = buffer 比较 → substr → erase shift → FLY_DECODE input 拷贝）
- **建议**: encode 时直接在 frame 上序列化；recv_buffer 改为预分配 + 偏移量管理；dispatch_message 使用偏移量避免 buffer 拷贝；decode 使用 string_view 避免_substr。

### 5.2 Master 单点瓶颈 [已记录]

- **严重程度**: 中
- **问题**: 所有任务调度、数据位置查询、写注册经过 Master 单一 reactor 线程。Worker 数量增长时 Master 成为不可避免的中心瓶颈。
- **建议**: 短期通过 HandlerThreadPool 将耗时操作异步化；长期考虑分布式调度（如分片 Master）。

### 5.3 无消息背压/流控 [建议]

- **严重程度**: 中
- **问题**: Worker 向 Master 发送 DataReady/TaskComplete 时无流控机制，Master 处理慢时消息在 recv_buffers_ 堆积。
- **建议**: 添加 per-connection 发送窗口或 credit-based 流控。

### 5.4 DataResponse 大消息无流式分片 [建议]

- **严重程度**: 中
- **文件**: `src/network/cpp/message_types.h:117-129`
- **问题**: `DataResponseMessage.compressed_data` 使用 `CMString` 承载压缩数据，大对象（默认 256MB）整体序列化传输，无流式分片。
- **建议**: 添加 DATA_CHUNK / DATA_CHUNK_ACK 消息类型支持分片传输。

### 5.5 conn_send_mutex_map_mutex_ 全局锁 [建议]

- **严重程度**: 低
- **文件**: `src/network/cpp/reactor.h:95`
- **问题**: 所有连接的 send 操作在获取 per-conn mutex 前都要先竞争这个全局锁。
- **建议**: 改用 `std::array` 或 `CMUnorderedMap<conn_id, unique_ptr<mutex>>` + `std::shared_mutex`。

---

## 六、资源管理问题

### 6.1 solver export 裸 new + nanobind 返回 [建议]

- **严重程度**: 中
- **文件**: `src/solver/export/solver_export.cpp:42,173`
- **问题**: `return new fly::SubdomainSolver(A)` 和 `auto* m = new EXSlvSparseMatrix()` 使用 `new` 创建对象通过 nanobind 返回。依赖 nanobind 所有权转移，转换失败时泄漏。
- **建议**: 使用 `CMMakeUnique` 或 nanobind 的 `rv_policy::take_ownership` 确保语义清晰。

---

## 六B、非惯用代码模式（Trick Code）

### 6B.1 单例模式不统一 [已修复]

- **严重程度**: 中
- **问题**: 项目中单例类使用了不同写法，现已统一为 `CMSharedPtr<T> instance()` 模式。

| 类 | 写法 | 状态 |
|---|---|---|
| `Config` | Meyers' singleton (`static Config config`) | ✅ 不变 |
| `ProcessInfo` | Meyers' singleton (`static ProcessInfo info`) | ✅ 不变 |
| `Logger` | `CMSharedPtr<Logger> instance()` — `CMMakeShared` + public 构造 | ✅ 已修复 |
| `DataService` | `CMSharedPtr<DataService> instance()` — `CMMakeShared` + public 构造 | ✅ 已修复 |

- **修复**: Logger 和 DataService 的 `instance()` 统一返回 `CMSharedPtr`，消除 `instance_ptr()` 重复接口，消除裸引用解引用。

### 6B.2 DataService Creator_ 友元 hack 绕过私有构造 [已修复]

- **严重程度**: 中
- **文件**: `src/storage/cpp/data_service.h`, `src/storage/cpp/data_service.cpp`
- **修复**: 构造函数改 public，删除 `Creator_` 结构体和 `friend` 声明（全项目唯一一处 friend），直接 `CMMakeShared<DataService>()`。

### 6B.3 agent_export 从引用调 shared_from_this() [已修复]

- **严重程度**: 中
- **文件**: `src/agent/export/agent_export.cpp`
- **修复**: 整条 `shared_from_this` / `weak_ptr<DataService>` 链路已删除。Agent 不再持有 `CMWeakPtr<DataService> data_service_`，不再有 `set_data_service()` 方法。回调注册（`set_remote_compressed_read_handler` / `set_direct_compressed_read_handler`）移到各 Agent 的 `start()` 方法中，直接用 `DataService::instance()` 获取。Python 侧删除 `set_data_service()` 调用。

---

## 七、头文件与代码质量问题

### 7.1 头文件膨胀 [已记录]

- **严重程度**: 低
- **文件**: `src/agent/cpp/master_agent.h`（20 个 include，跨 6 模块）、`src/storage/cpp/database.h`（14 个 include，跨 3 模块）
- **建议**: 使用前向声明减少头文件包含，或拆分为接口头和实现头。

### 7.2 .cpp 文件自包含使用相对路径 [已记录]

- **严重程度**: 低
- **文件**: `src/core/cpp/config.cpp:1`, `src/network/cpp/tcp_transport.cpp:1`, `src/core/cpp/process_info.cpp:1`
- **问题**: 使用 `"config.h"` 而非 `<core/cpp/config.h>`，不符合项目规范。

### 7.3 glob 通配 BUILD 中的 srcs [已记录]

- **严重程度**: 低
- **文件**: 所有 `cpp/BUILD`（solver 除外）
- **问题**: `glob(["*.cpp"])` 可能意外纳入不相关文件。solver 使用显式声明更安全。

---

## 八、测试覆盖盲区

| 模块 | 文件 | 问题 | 优先级 |
|------|------|------|--------|
| fly/main.py | `src/fly/main.py` | 204行关键逻辑无单元测试 | 中 |
| fly/runtime.py | `src/fly/runtime.py` | Agent 工厂模式无独立测试 | 中 |
| solver/py/ras_graph.py | `src/solver/py/ras_graph.py` | 809行复杂算法无独立测试 | 中 |
| storage/py/temp_store.py | `src/storage/py/temp_store.py` | disk spillover 逻辑未独立测试 | 中 |
| fly/mapreduce.py | `src/fly/mapreduce.py` | MapReduceJob 逻辑仅 QA 覆盖 | 中 |
| ReadCache 并发 | `src/storage/py/read_cache.py` | threading.Lock 无并发单元测试 | 低 |
| wait_obj | `src/task/py/task.py` | 轮询策略、can_still_produce 无独立测试 | 低 |

---

## 九、架构优势

1. **分层清晰**: 6 层模块化架构（common → serialization/log → core → network → storage → task → agent → solver），BUILD 级无循环依赖（DAG）
2. **BUILD 结构统一**: 所有模块遵循 `cc_library + cc_shared_library` 双目标模式，dynamic_deps 链正确
3. **智能指针全面**: `CMSharedPtr/CMUniquePtr/CMWeakPtr` 覆盖几乎所有所有权场景
4. **RAII 完整**: 线程、连接、文件等系统资源都有 RAII 包装器（PooledConnection, WriteBackQueue, IOThreadPool, Database）
5. **Worker-to-Worker 直传**: 数据不经 Master 中转，避免数据瓶颈
6. **宏体系精良**: `FLY_SERIALIZE_*` / `FLY_EXPORT_*` 宏统一封装，模式一致
7. **Include 合规率 99.1%**: 353 处跨模块 include 中仅 3 处违规
8. **QA 覆盖面广**: 67 个 QA 测试文件覆盖绝大多数功能场景
9. **GIL 管理正确**: C++ 回调 Python 时正确使用 `gil_scoped_acquire`
10. **异常传递完整**: C++ → Python 异常链完整保留

---

## 十、修复优先级建议

### P0 — 立即修复（正确性/稳定性风险）

> 原 8 项全部解决。当前无 P0。

### P1 — 尽快修复（性能/可靠性）

1. HandlerThreadPool 未使用 → 启用异步 handler（3.1）
2. Reactor send 阻塞 30s → 异步发送或缩短超时（3.6）

> 注：3.1 和 3.6 耦合——启用 HandlerThreadPool 后 handler 不再在 reactor 线程执行，send 阻塞不再阻塞 reactor，建议一并修复。

### P2 — 计划改进（代码质量/可维护性）
3. 消息编解码内存拷贝优化（5.1）
4. 无背压/流控（5.3）
5. DataResponse 分片传输（5.4）
6. 测试框架迁移 pytest（4.10）
7. 类型注解补全（4.8）
8. DataService 锁粒度拆分（2.7）
9. WorkerAgentContext 去全局状态（2.5）
