# Fly 分布式任务框架 — 进度总结与路线图

**日期**: 2026-05-17 (v3)
**分支**: main
**总测试**: 37 Bazel targets (35 unit + 2 QA) = **全部通过** ✅

---

## 一、已完成功能总结

### Layer 0: 基础设施 ✅

| 组件 | 文件 | 说明 |
|------|------|------|
| 构建系统 | WORKSPACE, BUILD, .bazelrc, fly.sh | Bazel + fly.sh 封装，自动刷新 compile_commands.json |
| 类型别名 | `src/common/cpp/common_types.h` | CMString, CMVector, CMMap, CMUnorderedMap |
| 序列化宏 | `src/serialization/cpp/serialization_macros.h` | bitsery 后端，FLY_SERIALIZE / FLY_ENCODE / FLY_DECODE |
| 导出宏 | `src/export/cpp/export_macros.h` | nanobind 导出宏封装 |
| Config | `src/core/cpp/config.h/cpp` | 全局单例配置，workers launched 后不可变 |

### Layer 1: 存储层 ✅

| 组件 | 文件 | 说明 |
|------|------|------|
| Database | `src/storage/cpp/database.h/cpp` | 统一存储接口，2阶段写入 (register→write→flush)，写时通知 DataService |
| DataWriter | `src/storage/cpp/data_writer.h/cpp` | 单线程写入聚合器，小文件聚合 + 大文件分块，`get_last_entry()` 暴露索引 |
| DataReader | `src/storage/cpp/data_reader.h/cpp` | 数据读取，`read_from_entries()` 直接按 IndexEntry 读取 |
| **DataService** | `src/storage/cpp/data_service.h/cpp` | **统一内存索引：local_idx (含 incomplete/complete 生命周期) + remote_idx + worker_registry + IOThreadPool 数据传输线程池** |
| StorageManager | `src/storage/cpp/storage_manager.h/cpp` | Database 生命周期管理，单例 |
| LocalIndex | `src/storage/cpp/local_index.h/cpp` | 本地索引持久化 (.idx 文件) |
| Compression | `src/storage/cpp/compressor.h/cpp` | LZ4 / ZLIB / ZSTD 压缩实现 |
| Python 导出 | `src/storage/export/storage_export.cpp` | EXStgDatabase, EXStgDataService, EXStgIndexEntry 等 |

### Layer 2: 网络层 ✅

| 组件 | 文件 | 说明 |
|------|------|------|
| Reactor | `src/network/cpp/reactor.h/cpp` | 单线程事件循环，TCP server/client |
| TransportLayer | `src/network/cpp/transport.h/cpp` | 传输层抽象接口 |
| TCPTransport | `src/network/cpp/tcp_transport.cpp` | POSIX TCP 实现 (epoll) |
| MessageProtocol | `src/network/cpp/message_protocol.h/cpp` | 二进制帧协议 (4字节长度头 + payload) |
| MessageTypes | `src/network/cpp/message_types.h` | 22 种消息类型定义 (含 WRITE_REGISTER/ACK) |
| IOThreadPool | `src/network/cpp/io_thread_pool.h/cpp` | 通用线程池，submit + completion 回调模式 |
| DataClient | `src/network/cpp/data_client.h/cpp` | 阻塞 TCP 数据客户端，独立连接，不走主 Reactor |

### Layer 3: 任务系统层 ✅

| 组件 | 文件 | 说明 |
|------|------|------|
| DependencyGraph | `src/task/cpp/dependency_graph.h/cpp` | 任务依赖管理，mark_data_ready 触发就绪 |
| WorkerManager | `src/task/cpp/worker_manager.h/cpp` | Worker 注册/状态管理 |
| TaskScheduler | `src/task/cpp/task_scheduler.h/cpp` | FIFO 调度，依赖就绪检测 |
| MetadataManager | `src/task/cpp/metadata_manager.h/cpp` | 任务元数据 (仅 task lifecycle，数据位置已迁移至 DataService) |
| HeartbeatMonitor | `src/task/cpp/heartbeat_monitor.h/cpp` | 心跳监控，超时检测 |

### Layer 4: Agent 层 ✅

| 组件 | 文件 | 说明 |
|------|------|------|
| MasterAgent | `src/agent/cpp/master_agent.h/cpp` | Master 节点，write register 验证 (frozen check + ACK)，fatal error 处理 |
| WorkerAgent | `src/agent/cpp/worker_agent.h/cpp` | Worker 节点，write register 协议 (send→poll ACK)，on_data_request 委托 DataService IOThreadPool |
| TaskExecutor | `src/agent/cpp/task_executor.h/cpp` | 任务执行器，支持 Python callable |

### Layer 5: Python API + 数据传输 ✅

| 组件 | 文件 | 说明 |
|------|------|------|
| _Database | `src/fly/database.py` | 三层读取流程：local DataService → remote_idx 缓存 → 全程远程 (3次重试) |
| Master/Worker | `src/fly/agent.py` | Python 高层 Master/Worker 封装 |
| Runtime | `src/fly/runtime.py` | 运行时配置 (master/worker mode) |
| Task | `src/fly/task.py` | @as_task 装饰器 |
| Executor | `src/fly/executor.py` | Worker 执行器 (import module + pickle args + 执行) |

---

## 二、DataService 架构

### 2.1 设计目标

DataService 统一管理节点 (Master/Worker) 的数据索引信息，分为 local_idx 和 remote_idx 两种，消除原有的 MetadataManager 数据位置管理与任务元数据的职责混淆。

### 2.2 数据结构

```
DataService (单例, Master 和 Worker 通用)
├── local_idx:  object_name → {db_id, IndexEntry[], flushed}
│   触发更新:
│     - 本地 write_object 完成 → on_object_written()
│     - flush() 完成 → on_flush(), 标记 flushed=true
│
├── remote_idx: object_name → {worker_id, host, port}
│   触发更新:
│     - Master: 收到 DataReadyMessage / TaskCompleteMessage
│     - Worker: 远程读取成功后缓存
│
└── worker_registry: worker_id → {host, port}
    触发更新:
      - 收到 RegisterMessage
```

### 2.3 读取流程 (read_object)

所有读取路径统一经过 DataService：

```
Python: db.read_object("key")
  │
  ├─ 1. DataService.try_read_local(key)
  │     └── 查内存 local_idx → 找到 + flushed
  │           └── DataReader.read_from_entries()
  │                 ├── 单条目 → read_object_data(entry) → 解析 ObjectHeader (含 py_name)
  │                 └── 多条目 → 排序拼接, 从首块 ObjectHeader 提取 py_name
  │
  ├─ 2. DataService.lookup_remote_idx(key)
  │     └── 有缓存 → WorkerAgent.request_data_from_worker(host, port, key)
  │
  ├─ 3. 全程远程 (最多 3 次重试):
  │     WorkerAgent.request_remote_data(key)
  │
  └─ 4. 3 次均失败 → raise RuntimeError

C++ 模板: Database::read_object<T>(key)
  └── fly::DataService::instance().try_read_local(key) → FLY_DECODE_FROM_BYTES
```

### 2.4 数据传输架构

**Worker B (服务端 — 响应数据请求)**:
1. Reactor 收到 `DataRequestMessage` → `on_data_request` handler
2. `DataService.submit_transfer(conn_id, object_name)` — 非阻塞入队，Reactor 线程立即释放
3. IOThreadPool 工作线程: `try_read_local` → 文件 I/O
4. `process_completions()` 回到 Reactor 线程 → `reactor_->send(conn_id, DataResponseMessage)`

**Worker A (客户端 — 发起数据请求)**:
- `DataClient::request_data(host, port, object_name)` — 创建独立阻塞 TCP socket
- connect → send DataRequestMessage → recv DataResponseMessage → close
- 完全独立于主 Reactor，避免多线程读冲突

**线程池配置**: `Config::get_int("data_server_threads")` (默认 1)

### 2.5 设计决策

| 决策 | 理由 |
|------|------|
| local_idx + remote_idx 不分模式 | Master 和 Worker 读写模式一致，仅更新触发源不同 |
| Worker 远程读取后更新 remote_idx | 避免重复查 Master，后续同对象直接 Worker→Worker |
| remote_idx 缓存失败静默降级 | 缓存过期/Worker 下线时，catch 异常 → 走全程远程 |
| MetadataManager 仅保留 task 元数据 | 任务生命周期与数据位置是不同关注点，解耦独立演进 |
| Database 构造时注册 DataService | 确保 DataService 知道 db_id→paths 映射 |
| DataService 持有 IOThreadPool 处理传输 | 文件 I/O 不阻塞 Reactor 线程，心跳和任务分配不受影响 |
| DataClient 独立阻塞连接 | 每次读创建独立连接，不走主 Reactor，多线程读无冲突 |
| completion 回调在 Reactor 线程执行 | 发送响应复用 Reactor 的 transport，避免跨线程 send |

---

## 三、Write Registration 协议 (新增 ✅)

### 3.1 设计目标

Worker 写入数据前，先向 Master 注册写意图，Master 验证 DB 状态（是否冻结），防止写入已冻结的 DB。同时支持并发读取场景：写入中的对象创建 incomplete IndexEntry，读取请求等待写入完成后获取数据。

### 3.2 协议流程

```
Worker (write_object)
  │
  ├─ 1. on_write_started(db_id, object_name)
  │     └── DataService 创建 incomplete LocalObjectInfo (INCOMPLETE 状态)
  │
  ├─ 2. register_write_with_master(db_id, object_name)
  │     └── 发送 WriteRegisterMessage → Master
  │         Master 检查 is_db_frozen()
  │         ├── 未冻结 → ACK(success=true)
  │         └── 已冻结 → ACK(success=false, error_type=WRITE_TO_FROZEN_DB)
  │     Worker poll ACK:
  │     ├── ACK success → 继续
  │     ├── ACK fail → 抛出 WriteRegistrationError
  │     └── 超时 → 抛出 WriteRegistrationError (TIMEOUT)
  │
  ├─ 3. DataWriter::write (落盘)
  │
  ├─ 4. on_write_completed(db_id, object_name, entries)
  │     └── DataService 设置 COMPLETE 状态，填充 IndexEntry
  │
  ├─ 5. Database::flush()
  │     └── on_flush(db_id) → DataService 通知 CV (唤醒等待的读取者)
  │
  └─ 6. 错误路径 (任一步失败):
        on_write_failed(db_id, object_name)
        └── DataService 设置 FAILED 状态，通知 CV
        └── 重新抛出异常 → TaskExecutor → TaskFailedMessage(error_type) → Master
            Master 检查 fatal error type → 广播 ShutdownMessage
```

### 3.3 并发读取 (incomplete IndexEntry)

```
Worker A (读取) 发起 DataRequest → Worker B (正在写入)
  │
  Worker B DataService:
  └── submit_transfer() → try_read_local_or_wait(object_name, timeout)
      ├── COMPLETE → 立即返回数据
      ├── INCOMPLETE → wait on CV → COMPLETE/FAILED/timeout
      └── 不存在 → 返回 not found
```

### 3.4 关键类型

| 类型 | 文件 | 说明 |
|------|------|------|
| TaskErrorType | `src/common/cpp/error_types.h` | UNKNOWN=0, EXECUTION_ERROR=1, WRITE_TO_FROZEN_DB=2, WRITE_REGISTRATION_FAILED=3, WRITE_REGISTRATION_TIMEOUT=4 |
| WriteRegistrationError | `src/agent/cpp/worker_context.h` | 携带 TaskErrorType 的异常类 |
| CompletionState | `src/storage/cpp/data_service.h` | INCOMPLETE / COMPLETE / FAILED |
| LocalObjectInfo | `src/storage/cpp/data_service.h` | shared_ptr, 含 condition_variable + cv_mutex |
| WriteRegisterMessage/ACK | `src/network/cpp/message_types.h` | 消息类型 21/22 |

### 3.5 设计决策

| 决策 | 理由 |
|------|------|
| TaskErrorType in common/ | 避免 circular dependency (network→agent↔storage) |
| Thread-local last_error_type_ | C++ exception 跨 nanobind boundary 到 Python 后丢失类型信息，thread-local 保存 error type |
| on_flush notifies CV | write_completed 设置 COMPLETE，flush 设置 flushed。CV predicate 等待 COMPLETE+flushed，on_flush 必须 notify |
| LocalObjectInfo 为 shared_ptr | condition_variable 不可移动，shared_ptr 保证地址稳定 |
| Master fatal error 不调 stop() | 只设 flag + 广播 shutdown，避免 detached thread 调 stop() 的崩溃风险 |

---

## 四、测试状态

| 测试类型 | 数量 | 状态 |
|---------|------|------|
| C++ 单元测试 (Bazel) | 35 targets | ✅ 全部通过 |
| QA 集成测试 | 2 targets | ✅ 全部通过 |
| E2E 端到端测试 | 4+5 cases | ✅ 全部通过 |

### 新增测试

| 测试 | 覆盖 |
|------|------|
| data_service_test (12 cases) | try_read_local, on_object_written/flush, remote_idx CRUD, worker_registry, 多对象读写, typed objects |
| write_registration_test (10 cases) | DataService incomplete entry 生命周期, wait-for-completion, concurrent waiters, TaskErrorType 枚举值, Database 2阶段写入 |
| write_register_network_test (5 cases) | Master+Worker write register ACK, frozen DB 拒绝, fatal error 传播, WriteRegisterAckMessage/TaskFailedMessage error_type 字段 |

---

## 五、已修复代码问题

| # | 问题 | 修复 | 日期 |
|---|------|------|------|
| C1 | `read_from_entries` 未设置 `py_name`，C++ 导出对象通过 DataService 本地读取后反序列化失败 | 单条目委托 `read_object_data`；多条目从首块 ObjectHeader 提取 `py_name` | 2026-05-17 |
| C2 | 模板 `read_object<T>` 绕过 DataService，直接操作 DataReader | 改为调用 `fly::DataService::instance().try_read_local()` | 2026-05-17 |
| C3 | Python `except Exception` 无 `as e`，丢失错误详情 | 改为 `except Exception as e`，日志记录具体错误 | 2026-05-17 |
| C4/C5 | `on_data_request` 的 `catch(...)` 静默吞掉异常 | 拆分为 `catch(const std::exception&)` + `catch(...)`，分别记录日志 | 2026-05-17 |
| C6 | `DbPathRequestMessage`/`DbPathResponseMessage` 复用 `DATA_QUERY`/`DATA_LOCATION` 消息类型 | 分配独立类型 `DB_PATH_REQUEST (19)` / `DB_PATH_RESPONSE (20)` | 2026-05-17 |
| C7 | `on_db_path_response` 空操作，Worker 无 `request_db_path` 功能 | 实现完整流程：`request_db_path()` → 发送请求 → 轮询响应 → 创建 Database 实例 | 2026-05-17 |
| **E1** | `on_data_request` 在 Reactor 线程同步执行文件 I/O，阻塞心跳/任务分配 | DataService IOThreadPool 异步处理: `submit_transfer` 非阻塞入队 → 线程池文件 I/O → completion 回调在 Reactor 线程发送 | 2026-05-17 |
| **E2** | Worker A 通过 `reactor_->connect` 连接 Worker B，多线程读共享同一 epoll | DataClient 独立阻塞 TCP 连接，完全不走主 Reactor，每次请求创建独立 socket | 2026-05-17 |

---

## 六、待实施功能 (TODO)

### TODO-1: Database Freeze 后处理 (高优先级)
- [ ] Master 收到 DatabaseFreezeMessage 后，向相关 Worker 发送 IdxRequestMessage
- [ ] Worker 返回本地 idx 内容 (IdxResponseMessage)
- [ ] Master 合并所有 Worker 的 idx 条目，写入 base_path/merged.idx
- [ ] Master 收集 Worker 信息，写入 base_path/_META
- [ ] read_object 优先使用 merged.idx
- [ ] 已冻结 Database 的加载恢复 (load_meta + merged.idx)

### TODO-2: SSH / Custom Worker 启动 (中优先级)
- [ ] `master.launch_ssh_workers(workers, ssh_user, ssh_key)`
- [ ] `master.launch_custom_workers(workers, submit_command)`
- [ ] `master.wait_for_workers(worker_ids)`

### TODO-3: Locality 优化调度 (中优先级)

**待实现**:
- [ ] TaskScheduler 计算每个 ready 任务在每个 Worker 上的 locality_score
- [ ] 优先分配 locality_score 最高的任务
- [ ] DataService 提供查询接口: 给定 object_name 列表，返回每个 Worker 持有的数据量
- [ ] 调度策略可配置 (FIFO / Locality / Hybrid)

### TODO-4: 数据副本策略 (低优先级)

**待实现**:
- [ ] write_object backup=True 参数支持
- [ ] BackupManager 组件 (Master 侧)
- [ ] 备份任务调度策略

### TODO-5: 容错机制 (低优先级)

**待实现**:
- [ ] Worker 失联后任务重新调度
- [ ] 失联 Worker 标记 + 清理任务
- [ ] 任务重试机制 (可恢复错误)

---

## 七、技术栈

| 组件 | 技术选型 |
|------|----------|
| C++ 标准 | C++20 |
| 编译器 | gcc12 |
| Python 绑定 | nanobind |
| 序列化 | bitsery (header-only, 版本化支持) |
| 构建 | Bazel + fly.sh |
| 测试 | gtest + pytest |
| 压缩 | LZ4 / ZLIB / ZSTD |
