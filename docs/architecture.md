# Fly 分布式任务框架 — 架构与使用指南

## 一、概述

### 项目目标

构建一个多机多进程分布式任务执行框架，支持：
- Master节点负责任务调度和数据元信息管理
- Worker节点负责具体任务执行和数据存储
- 任务可在任意节点提交，支持递归任务提交
- 任务调度需满足数据依赖准备完毕
- 数据传递依靠分布式文件存储系统

### 技术栈

| 组件 | 技术选型 |
|------|----------|
| C++ 标准 | C++20 |
| 编译器 | gcc12 |
| Python 绑定 | nanobind |
| 序列化 | bitsery (header-only, 版本化支持) |
| 构建系统 | Bazel + fly.sh |
| 测试框架 | gtest + pytest |
| 压缩库 | LZ4 / ZLIB / ZSTD |

### 架构分层

```
┌─────────────────────────────────────────────────────────────────┐
│                         Master Node                              │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────────┐ │
│  │ Task Scheduler│  │ Task        │  │ Storage Metadata       │ │
│  │ (FIFO+Locality)│  │ Manager    │  │ (Data blocks, replicas)│ │
│  └─────────────┘  └─────────────┘  └─────────────────────────┘ │
│         │                │                    │                 │
│         └────────────────┼────────────────────┘                 │
│                          │                                      │
│                   ┌──────┴──────┐                               │
│                   │ Message Hub │  ← TCP Server (Port 8000)    │
│                   └─────────────┘                               │
└───────────────────────────────┬─────────────────────────────────┘
                                │
          ┌─────────────────────┼─────────────────────┐
          │                     │                     │
          ▼                     ▼                     ▼
┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐
│   Worker 1      │  │   Worker 2      │  │   Worker 3      │
│ ┌─────────────┐ │  │ ┌─────────────┐ │  │ ┌─────────────┐ │
│ │Task Executor│ │  │ │Task Executor│ │  │ │ (Storage     │ │
│ └─────────────┘ │  │ └─────────────┘ │  │ │  Only Mode)  │ │
│ ┌─────────────┐ │  │ ┌─────────────┐ │  │ └─────────────┘ │
│ │Data Storage │ │  │ │Data Storage │ │  │ ┌─────────────┐ │
│ │(Aggregator) │ │  │ │(Aggregator) │ │  │ │Data Storage │ │
│ └─────────────┘ │  │ └─────────────┘ │  │ └─────────────┘ │
└─────────────────┘  └─────────────────┘  └─────────────────┘
```

---

## 二、启动与进程模型

### Binary启动方式

统一使用 `fly` binary启动，通过参数区分Master/Worker模式：

```bash
# Master模式：直接传入用户脚本（无flag）
fly user_tasks.py

# Worker模式：使用--worker_mode区分
fly --worker_mode --master master:8000 --role hybrid
fly --worker_mode --master master:8000 --role storage_only

# Master交互模式（可选）
fly -i user_tasks.py
```

### CLI参数说明

| 参数 | Master模式 | Worker模式 |
|------|-----------|-----------|
| positional arg | 用户Python脚本路径 | 不适用 |
| `--worker_mode` | 不设置 | 必须设置，标识Worker模式 |
| `--master` | 不设置（自己就是Master） | Master地址 |
| `--role` | 不适用 | hybrid / storage_only |
| `-i` | 交互模式，执行后进入Python REPL | 不适用 |

### Master启动流程

1. C++层初始化Master TCP Server
2. 导出Master Python接口（nanobind）
3. 执行用户Python脚本
4. Python脚本中调用业务任务

### Worker启动流程

1. C++层初始化Worker TCP Client + Storage
2. 导出Python接口
3. 连接Master，注册
4. 进入等待任务循环

---

## 三、使用方式

### 3.1 配置管理

Config单例模式，必须在启动worker前设置参数：

```python
from fly import get_config

# 获取全局单例Config（无需创建实例）
config = get_config()

# 设置参数（必须在启动worker前）
config.set(
    master_port=8000,
    heartbeat_timeout=120,
    heartbeat_interval=5,
    backup_threshold=100,
    aggregation_threshold=1048576,      # 1MB
    large_file_threshold=67108864,      # 64MB（更新）
    block_size=134217728,               # 128MB
    track_writes=1,                     # 启用写入跟踪，记录每个任务写入的对象列表
    data_server_threads=2,              # Data Server线程池大小，默认1
)

# 再次调用get_config()返回同一个实例
config2 = get_config()  # config2 == config
```

**注意**：启动worker后再调用 `config.set()` 会抛出异常：
```python
config.set(heartbeat_timeout=60)  # RuntimeError: Config must be set before workers are launched
```

### 3.2 任务定义与提交

使用 `@as_task` 装饰器将普通函数包装为任务：

```python
from fly import as_task, task_name

# 简单任务定义
@as_task(inputs=lambda db, name: [f"input/{name}"])
def process_data(db, name):
    raw = db.read_object(f"input/{name}")
    result = cpp_algorithm(raw)
    db.write_object(f"output/{name}.result", result)

# 直接调用即可提交任务（异步，无返回值）
process_data(db, "a.csv")

# 使用@task_name自定义任务名
@as_task(inputs=lambda db, name: [f"input/{name}"])
@task_name("data_processor")
def custom_process(db, name):
    ...
```

**递归任务**：任务可以递归提交任务：

```python
@as_task(inputs=lambda db, name: [f"input/{name}"])
def parent_task(db, name):
    child_task(db, name)  # 异步调用，无返回值

@as_task(inputs=lambda db, name: [f"output/{name}.result"])  # 依赖子任务输出
def next_task(db, name):
    result = db.read_object(f"output/{name}.result")
    ...
```

**任务调度策略**：
- 默认策略：FIFO
- Worker选择：数据locality优先，尽量调度到数据所在的Worker
- 核心约束：Worker同一时刻最多执行一个任务。Master仅向 `is_busy=false` 的Worker派发任务

### 3.3 数据存储

**Database创建**：支持共享路径和本地路径双路径设计：

```python
from fly import Database

# 仅共享路径
db_a = Database("/data/project_a")

# 共享路径 + 本地路径（高性能写入）
db_b = Database("/data/project_b", data_path="/ssd/local_b")
```

| 路径 | 说明 | 必填 |
|------|------|------|
| `base_path` | 共享存储路径，所有Master/Worker可访问。freeze后聚合idx写入此路径 | 是 |
| `data_path` | 本地磁盘路径，可选。启用时write_object写入此路径，read_object优先查找此路径 | 否 |

**对象删除**：

```python
db.remove_object("object_name")

# Master 端需额外广播通知所有 Worker
master.broadcast_object_removed(db.get_db_id(), "object_name")
```

**删除流程**：
- `db.remove_object()` 删除本地索引条目
- Worker 端通过 `WorkerAgentContext` 自动发送 `ObjectRemovedMessage` 到 Master
- Master 收到后通过 `DataService.remove_remote_index()` 清理，并广播给其他 Worker
- `freeze()` 时从磁盘聚合文件中删除对象（占位符实现）

**写入与读取**：

```python
# 写入对象
db.write_object(f"output/{name}.result", result)

# 读取对象
data = db.read_object(f"input/{name}")

# 冻结数据库（标记为只读）
db.freeze()

# 多Database支持（轻量级）
db_a = Database("/data/project_a")
db_b = Database("/data/project_b", data_path="/ssd/local_b")
```

**Database Freeze**：
- `db.freeze()` 后，所有 `write_object()` 调用抛出异常
- `db.read_object()` 正常工作
- 冻结时在 `base_path` 创建标识文件 `_FROZEN`
- 已通过 `db.remove_object()` 删除的对象会在 freeze 时从磁盘文件中移除（占位符实现）
- **注意**：后处理（idx合并、_META生成）尚未实现

**写注册协议**：
- 任务调用 `write_object` 时记录写入的对象名
- 任务完成后，通过 `TaskCompleteMessage` 携带写入对象列表发送到Master
- Master 更新DataService索引

### 3.4 Worker管理

**本地Worker启动**：

```python
from fly import master

# 启动本地Workers（非阻塞，立即返回）
master.launch_local_workers(
    workers=[
        {"role": "hybrid"},
        {"role": "storage_only"},
    ],
)
```

**SSH Worker启动**（尚未实现）：

```python
# SSH方式启动（设计完成，尚未实现）
master.launch_ssh_workers(
    workers=[
        {"host": "192.168.1.10", "role": "hybrid"},
        {"host": "192.168.1.11", "role": "hybrid"},
    ],
    ssh_user="root",
    ssh_key="/path/to/key",
)
```

**自定义Worker启动**（尚未实现）：

```python
# 自定义方式启动（设计完成，尚未实现）
master.launch_custom_workers(
    workers=[
        {"role": "hybrid"},
    ],
    submit_command="bsub -q normal -P my_project",
)
```

**Worker能力说明**：
- `role` 字段：设计完成，但尚未在调度逻辑中使用
- `RegisterMessage.role` 已存在，但WorkerInfo中不存储此字段
- **动态能力**: Worker 可在运行时通过 `WorkerPropertyUpdateMessage` 动态设置/移除能力（GPU/CPU等），调度器实时匹配任务所需能力
- **持久化失败任务**: 不可调度任务会持久化到 `log_dir/failed_tasks.bin`，用户可调用 `restart_failed_tasks()` 修复问题后重新提交

---

## 四、架构分层

### 4.1 五层架构

```
┌─────────────────────────────────────────────────────────────────┐
│  Layer 5: Python 流程控制层                                      │
│  - @as_task 装饰器                                               │
│  - 任务定义与提交                                                │
│  - Python 主循环                                                 │
├─────────────────────────────────────────────────────────────────┤
│  Layer 4: Agent 层 (Master/Worker)                              │
│  - MasterAgent: 任务调度、元数据管理                             │
│  - WorkerAgent: 任务执行、数据存储                               │
├─────────────────────────────────────────────────────────────────┤
│  Layer 3: 任务系统层                                             │
│  - DependencyGraph: 任务依赖管理                                 │
│  - TaskScheduler: 任务调度器                                     │
│  - WorkerManager: Worker 状态管理                                │
├─────────────────────────────────────────────────────────────────┤
│  Layer 2: 网络层                                                 │
│  - Reactor: 单线程事件循环                                       │
│  - TransportLayer: TCP/UDP/RDMA 抽象                             │
│  - MessageProtocol: 二进制帧协议                                 │
├─────────────────────────────────────────────────────────────────┤
│  Layer 1: 存储层                                                 │
│  - Database: 统一存储接口                                        │
│  - DataWriter: 单线程写入聚合器                                  │
│  - DataReader: 数据读取器（实例方法）                            │
│  - DataService: 统一内存索引（local_idx + remote_idx）          │
├─────────────────────────────────────────────────────────────────┤
│  Layer 0: 基础设施层                                             │
│  - Config: 全局配置管理                                          │
│  - Serializer: bitsery 序列化（FLY_SERIALIZE_* 宏封装）          │
│  - Export: nanobind 绑定（FLY_EXPORT_* 宏封装）                  │
│  - Common: CMString, CMVector 等类型别名                         │
└─────────────────────────────────────────────────────────────────┘
```

### 4.2 线程模型

**Master节点**：

| 线程 | 职责 |
|------|------|
| Main Thread (C++) | Reactor 事件循环，处理所有Worker消息 |
| Heartbeat Thread | 心跳检测，超时Worker标记 |
| Scheduler Thread | 定期备份检查、任务调度 |

**Worker节点**：

| 线程 | 职责 |
|------|------|
| Main Thread (C++) | Reactor 事件循环，处理Master消息 |
| Heartbeat Thread | 心跳发送 |
| Data Server IOThreadPool | 响应其他Worker的数据请求（默认1线程，可配置） |
| Task Execution Thread (Python) | 唯一涉及GIL的线程，执行用户任务 |

**关键设计**：
- Master 不需要消息处理线程池（所有操作为快速C++操作）
- Worker Data Server采用线程池模式，支持高并发读请求
- Task Execution Thread 使用 `task_slot_`（而非队列）传递任务（Master保证仅向空闲Worker派发）

### 4.3 核心设计决策

1. **Python主进程**：用户脚本执行、任务定义、装饰器
2. **C++20底层实现**：存储、通信、消息处理、调度
3. **Reactor模式**：Main Thread事件循环 + 后台线程（心跳/调度/数据服务）
4. **唯一GIL线程**：Worker的Python任务执行线程
5. **任务单slot传递**：Master不向忙碌Worker派发任务，task_slot_而非task_queue_
6. **动态创建**：Database轻量级，StorageManager按需创建writer
7. **Database Freeze**：db.freeze()触发只读冻结，Master后台合并idx，Worker数据不搬迁
8. **双路径存储**：base_path共享路径+data_path本地路径，写入走本地、读取优先本地
9. **宏封装**：
   - FLY_SERIALIZE_* 宏封装bitsery序列化（支持未来替换）
   - FLY_EXPORT_* 宏封装nanobind绑定（支持未来替换）
10. **传输层抽象**：TransportLayer接口支持未来替换TCP为UDP/RDMA
11. **Data Server线程池**：Worker数据服务采用线程池，默认单线程，可配置
12. **DataService统一索引**：local_idx（本地写入）、remote_idx（远程缓存）、worker_registry（Worker注册信息）

---

## 五、数据流

### 5.1 读取流程（三层降级）

```
Worker A 读取 object_name:

1. 优先查询本地索引（DataService.local_idx）
   └─ 找到 → DataReader.read_from_entries() → 返回数据

2. 本地未找到，查询远程索引缓存（DataService.remote_idx）
   └─ 找到缓存 → DataClient.request_data() 直连目标 Worker B
       └─ Worker B 响应 → 返回数据

3. 缓存未找到，查询 Master
   └─ request_remote_data(object_name) → Master 查询 DataService
       └─ 返回目标 Worker 地址
       └─ DataClient.request_data() 直连目标 Worker B
           └─ 最多重试3次
           └─ 成功后更新 remote_idx 缓存
```

**关键点**：
- 所有读取路径统一经过DataService
- DataClient 使用独立TCP socket（独立于主Reactor）
- 每次请求创建独立连接，避免多线程读冲突

### 5.2 写入流程

```
Worker A 写入 object_name:

1. 调用 db.write_object(object_name, data)
   └─ 检查 Database 是否冻结

2. 序列化数据（bitsery）

3. 判断大小：
   └─ 小于 large_file_threshold (64MB) → 聚合存储
   └─ 大于 threshold → 分块存储

4. 写入 data_path（若设置）或 base_path

5. 更新本地索引（DataService.local_idx）

6. 向 Master 发送 DataReadyMessage
   └─ Master 更新 DataService索引

7. 若任务完成：
   └─ 发送 TaskCompleteMessage（携带 written_objects）
   └─ Master 更新远程索引（DataService.remote_idx）
```

**写注册协议**：
- Config.track_writes 启用时，记录每个任务写入的对象列表
- WorkerAgentContext 使用C函数指针回调模式（非WorkerAgent*指针）

### 5.3 Database Freeze

```
任务调用 db.freeze():

Worker端：
├─ 设置 is_frozen_ = true
├─ 在 base_path 创建 _FROZEN 标识文件
├─ 发送 DatabaseFreezeMessage 到 Master
└─ 后续 write_object() 调用抛出异常

Master端（后台任务）：
├─ 收到 DatabaseFreezeMessage
├─ 记录 db_id + Worker 列表
├─ 依次向相关 Worker 发送 IdxRequestMessage
├─ 收集所有 IdxResponseMessage
├─ 合并 idx 条目
├─ 将聚合 idx 写入 base_path/merged.idx
├─ 收集 Worker 信息
├─ 将数据库元信息写入 base_path/_META
└─ 后处理完成
```

**注意**：idx合并、_META生成等后处理尚未实现。

---

## 六、通信协议

### 6.1 帧格式

```
┌────────────────┬─────────┬─────────────────┐
│ 4 bytes length │ 1 byte  │   payload       │
│ (帧长度)        │ 消息类型  │ (序列化数据)     │
└────────────────┴─────────┴─────────────────┘
```

### 6.2 消息类型

| 消息类型 | 方向 | 处理状态 |
|---------|------|---------|
| RegisterMessage | Worker → Master | ✅ 已实现 |
| RegisterAckMessage | Master → Worker | ✅ 已实现 |
| HeartbeatMessage | Worker → Master | ✅ 已实现 |
| TaskSubmitMessage | 任意 → Master | ✅ 已实现 |
| TaskAssignMessage | Master → Worker | ✅ 已实现 |
| TaskCompleteMessage | Worker → Master | ✅ 已实现 |
| TaskFailedMessage | Worker → Master | ✅ 已实现 |
| DataReadyMessage | Worker → Master | ✅ 已实现 |
| DataQueryMessage | 任意 → Master | ✅ 已实现 |
| DataLocationMessage | Master → Worker | ✅ 已实现 |
| DataRequestMessage | Worker → Worker | ✅ 已实现 |
| DataResponseMessage | Worker → Worker | ✅ 已实现 |
| BackupTaskMessage | Master → Worker | ✅ 已实现 |
| CleanupTaskMessage | Master → Worker | ✅ 已实现 |
| CleanupCompleteMessage | Worker → Master | ✅ 已实现 |
| UpdateAttributesMessage | Worker → Master | ✅ 已实现 |
| DatabaseFreezeMessage | Worker → Master | ✅ 已实现 |
| IdxRequestMessage | Master → Worker | ✅ 已实现 |
| IdxResponseMessage | Worker → Master | ✅ 已实现 |
| ShutdownMessage | Master → Worker | ✅ 已实现 |
| DBPathRequestMessage | Worker → Master | ✅ 已实现 |
| DBPathResponseMessage | Master → Worker | ✅ 已实现 |
| WorkerPropertyUpdateMessage | Worker → Master | ✅ 已实现 |
| ObjectRemovedMessage | Worker → Master | ✅ 已实现 |

**TransportLayer更新**：
- 移除 `accept()` 方法
- 新增 `stop_listening()` 方法

---

## 七、模块目录结构

```
fly/
├── fly.sh                    # 构建脚本（必须使用！）
├── BUILD                     # 顶层 BUILD（自动生成）
├── .bazelrc                  # Bazel 配置
├── WORKSPACE                 # Bazel 工作区
├── MODULE.bazel              # Bazel 模块定义
│
├── src/                      # 源代码
│   ├── common/               # 公共类型定义
│   │   └── cpp/common_types.h  # CMString, CMVector, CMMap 等
│   │
│   ├── core/                 # 核心基础模块
│   │   └── cpp/config.h/cpp  # 配置管理
│   │
│   ├── serialization/        # 序列化模块
│   │   └── cpp/serialization_macros.h  # FLY_SERIALIZE, FLY_ENCODE (bitsery 后端)
│   │
│   ├── export/               # 导出宏定义
│   │   └── cpp/export_macros.h  # FLY_EXPORT_* 宏
│   │
│   ├── storage/              # 存储层 (Layer 1)
│   │   ├── cpp/
│   │   │   ├── database.h/cpp
│   │   │   ├── data_writer.h/cpp
│   │   │   ├── data_reader.h/cpp
│   │   │   ├── data_service.h/cpp  # 统一内存索引 (local/remote idx)
│   │   │   └── storage_manager.h/cpp
│   │   ├── export/storage_export.cpp
│   │   └── tests/
│   │
│   ├── network/              # 网络层 (Layer 2)
│   │   ├── cpp/
│   │   │   ├── reactor.h/cpp
│   │   │   ├── transport.h/cpp
│   │   │   ├── tcp_transport.cpp
│   │   │   ├── message_protocol.h/cpp
│   │   │   └── message_types.h
│   │   ├── export/network_export.cpp
│   │   └── tests/
│   │
│   ├── task/                 # 任务系统层 (Layer 3)
│   │   ├── cpp/
│   │   │   ├── dependency_graph.h/cpp
│   │   │   ├── worker_manager.h/cpp
│   │   │   ├── task_scheduler.h/cpp
│   │   │   ├── task_manager.h/cpp  # (原 metadata_manager)
│   │   │   └── heartbeat_monitor.h/cpp
│   │   └── tests/
│   │
│   ├── agent/                # Agent 层 (Layer 4)
│   │   ├── cpp/
│   │   │   ├── master_agent.h/cpp
│   │   │   ├── worker_agent.h/cpp
│   │   │   ├── task_executor.h/cpp
│   │   │   └── worker_agent_context.h  # C函数指针回调模式
│   │   ├── export/agent_export.cpp
│   │   └── tests/
│   │
│   ├── log/                 # 日志模块
│   │   ├── cpp/logger.h/cpp
│   │   └── export/log_export.cpp
│   │
│   └── py/                  # Python 高层 API (Layer 5)
│       ├── __init__.py
│       ├── task.py          # @as_task 装饰器
│       ├── master.py        # Master 类包装
│       └── config.py        # Config 包装
│
├── qa/                       # 项目级集成测试
├── docs/                     # 设计文档
└── scripts/                  # 辅助脚本
```

---

## 八、实现状态

### 已完成（✅）

- **Layer 0**：基础设施层（WORKSPACE, BUILD, 宏定义）
- **Layer 1**：存储层（Database, DataService, StorageManager）
  - DataReader 实例方法（非静态）
  - DataService 统一索引（local_idx + remote_idx + worker_registry）
  - 异步 WriteBackQueue
- **Layer 2**：网络层（Reactor, TCP, 消息协议）
  - 23种消息类型全部定义
  - TransportLayer 抽象（移除 accept()，新增 stop_listening()）
  - Worker 数据传输（独立 DataClient 连接）
- **Layer 3**：任务系统层（DependencyGraph, 调度器）
  - TaskManager（原 MetadataManager）
  - TaskScheduler
  - WorkerManager
  - HeartbeatMonitor
- **Layer 4**：Agent 层（MasterAgent, WorkerAgent）
  - WorkerAgentContext：C函数指针回调模式
  - TaskExecutor：任务执行器
  - Worker动态能力管理：`set_worker_property`, `remove_worker_property`, `get_worker_properties`
  - 失败任务持久化：`FailedTaskRecord`, `restart_failed_tasks()` API
- **Layer 5**：Python API（@as_task 装饰器，配置管理）
  - FlyAgent 抽象基类：统一接口，Master/Worker 实现
  - Worker 属性管理：`set_worker_property`, `remove_worker_property`, `get_worker_properties`
  - 失败任务重启：`restart_failed_tasks()`，三阶段数据可用性检查

**测试覆盖**：
- 32 Bazel targets pass（1 data_service_test + 31 unit）
- QA + E2E 测试

### 尚未实现（⏳）

- **SSH Worker 启动**：`launch_ssh_workers` 接口设计完成，未实现
- **自定义 Worker 启动**：`launch_custom_workers` 接口设计完成，未实现
- **Database Freeze 后处理**：
  - idx 合并（merged.idx）
  - _META 元信息生成
  - Worker 自动启动恢复
- **Locality 调度**：数据locality优先调度（设计完成，未实现）
- **数据副本**：自动备份策略（BackupManager 设计完成，未实现）
- **Worker 失败恢复**：任务重新调度（设计完成，未实现）
- **Worker role 调度**：role-based 任务分配（RegisterMessage.role 存在但未使用）

---

*文档更新日期: 2026-05-21*