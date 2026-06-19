# Fly 分布式任务框架 — 架构与使用指南

## 一、概述

### 项目目标

构建一个多机多进程分布式任务执行框架，支持：
- Master节点负责任务调度和数据元信息管理
- Worker节点负责具体任务执行和数据存储
- 任务可在任意节点提交，支持递归任务提交
- 任务调度需满足数据依赖准备完毕
- 数据传递依靠分布式文件存储系统 + Worker 间直连传输

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
| 格式化库 | fmt (header-only) |

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
│ ┌─────────────┐ │  │ ┌─────────────┐ │  │ ┌─────────────┐ │
│ │Data Server  │ │  │ │Data Server  │ │  │ │Data Server  │ │
│ │(epoll+pool) │ │  │ │(epoll+pool) │ │  │ │(epoll+pool) │ │
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
| `--host` | 不适用 | 覆盖 hostname（用于多 host 测试） |

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

### 进程模式

`launch_workers()` 始终使用 **process 模式**（子进程 Worker，独立 DataService 单例）。thread 模式已移除。

---

## 三、使用方式

### 3.1 配置管理

Config单例模式管理所有进程共享的配置，ProcessInfo单例管理进程私有数据：

```python
from fly import get_config
from fly.runtime import get_agent

# 获取全局单例Config（所有进程共享，master在启动worker前通过config文件同步）
config = get_config()

# 设置共享参数（必须在启动worker前）
config.set(
    heartbeat_timeout=120,
    heartbeat_interval=5,
    backup_threshold=100,
    aggregation_threshold=1048576,      # 1MB
    large_file_threshold=67108864,      # 64MB
    block_size=134217728,               # 128MB
    track_writes=1,                     # 启用写入跟踪
    data_server_threads=2,              # Data Server线程池大小
    log_dir="/path/to/logs",            # 所有进程共享的日志目录
)

# 再次调用get_config()返回同一个实例
config2 = get_config()  # config2 == config
```

**注意**：启动worker后再调用 `config.set()` 会抛出异常。

**Config vs ProcessInfo**：
- `Config`：所有进程共享的数据（heartbeat_timeout, backup_threshold, log_dir 等），master 在启动 worker 前通过 config 文件同步
- `ProcessInfo`：进程私有数据（worker_mode, worker_id, master_host/port, hostname 等），不同步
- `--host` CLI 参数通过 `ProcessInfo::set_hostname()` 覆盖自动检测的 hostname

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

@as_task(inputs=lambda db, name: [f"output/{name}.result"])
def next_task(db, name):
    result = db.read_object(f"output/{name}.result")
    ...
```

**任务调度策略**：
- 默认策略：FIFO
- Worker选择：数据locality优先，尽量调度到数据所在的Worker
- 核心约束：Worker同一时刻最多执行一个任务

### 3.3 数据存储

**Database创建**：支持共享路径和本地路径双路径设计：

```python
from fly import open_db

# 仅共享路径
db_a = open_db("/data/project_a")

# 共享路径 + 本地路径（高性能写入）
db_b = open_db("/data/project_b", data_path="/ssd/local_b")
```

| 路径 | 说明 | 必填 |
|------|------|------|
| `base_path` | 共享存储路径，所有Master/Worker可访问 | 是 |
| `data_path` | 本地磁盘路径，写入走此路径 | 否 |

**对象删除**：

```python
db.remove_object("object_name")

# Master 端需额外广播通知所有 Worker
master.broadcast_object_removed(db.get_db_id(), "object_name")
```

**写入与读取**：

```python
# 写入对象
db.write_object(f"output/{name}.result", result)

# 读取对象
data = db.read_object(f"input/{name}")

# 冻结数据库（标记为只读）
db.freeze()
```

**读缓存分层**（`src/storage/cpp/object_cache.h`）：

read_object 经两层 LRU 缓存加速，进程级单例：

| 层 | 存储内容 | 命中收益 |
|----|---------|---------|
| **low** | 压缩字节 (`FlyBufferPtr` shared_ptr，零拷贝共享) | 省磁盘/远程 IO |
| **high** | 反序列化对象 (`std::any` 持 `CMSharedPtr<T>`) | 省反序列化 |

- 淘汰：LFU score = read_count/age，30s 保护期，1.5× 硬限制
- 失效：remove_object 触发 cache.remove（双层清理）
- 命中统计：`ObjectCache::Stats` 提供 per-tier hits/misses/puts/evictions 计数

**写入流程**：
1. 调用线程序列化 + 压缩（`compress_to_buffer` 流式管线）
2. WBQ 后台线程仅执行 `write_record` 磁盘写入
3. `write_object` 开始时即触发依赖满足（无需等异步落盘完成）

### 3.4 Worker管理

**本地Worker启动**：

```python
from fly import launch_workers

# 启动本地Workers（非阻塞，立即返回）
launch_workers([
    {"role": "hybrid"},
    {"role": "storage_only"},
])
```

**等待任务完成**：

```python
from fly import wait_tasks

# 等待所有任务完成
wait_tasks(timeout=30.0)  # 返回 True/False
```

**Worker能力说明**：
- `role` 字段：hybrid（任务执行+数据存储）/ storage_only（仅数据存储）
- **动态能力**: Worker 可在运行时动态设置/移除能力（GPU/CPU等），调度器实时匹配
- **持久化失败任务**: 不可调度任务会持久化到 `log_dir/failed_tasks.bin`

---

## 四、架构分层

### 4.1 六层架构

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
│  - TaskExecutor: 任务执行器                                      │
├─────────────────────────────────────────────────────────────────┤
│  Layer 3: 任务系统层                                             │
│  - DependencyGraph: 任务依赖管理                                 │
│  - TaskScheduler: 任务调度器                                     │
│  - WorkerManager: Worker 状态管理                                │
├─────────────────────────────────────────────────────────────────┤
│  Layer 2: 网络层                                                 │
│  - Reactor: 单线程事件循环                                       │
│  - Transport + EpollMultiplexer + ConnectionManager              │
│  - MessageProtocol + DataResponseProtocol (两段式)               │
│  - DataClientPool: 并发限制的数据请求池                          │
├─────────────────────────────────────────────────────────────────┤
│  Layer 1: 存储层                                                 │
│  - Database: 统一存储接口                                        │
│  - DataWriter: 流式管线 (compress_to_buffer + write_record)     │
│  - DataReader: 数据读取器                                        │
│  - DataService: 统一内存索引 + DataServer (epoll+线程池)        │
│  - ObjectCache: 两层 LRU 读缓存 (low=压缩字节, high=反序列化)  │
├─────────────────────────────────────────────────────────────────┤
│  Layer 0: 基础设施层                                             │
│  - Config: 全局共享配置管理                                      │
│  - ProcessInfo: 进程私有数据                                     │
│  - Serializer: bitsery 序列化 (FLY_SERIALIZE_* 宏封装)          │
│  - Export: nanobind 绑定 (FLY_EXPORT_* 宏封装)                  │
│  - Common: CM* 类型别名 (CMSharedPtr, CMString, CMVector...)    │
└─────────────────────────────────────────────────────────────────┘
```

### 4.2 线程模型

**Master节点**：

| 线程 | 职责 |
|------|------|
| Main Thread (Python) | 用户脚本执行、任务提交 |
| Reactor Thread | epoll 事件循环，处理所有Worker消息 |
| Heartbeat Thread | 心跳检测，超时Worker标记 |

**Worker节点**：

| 线程 | 职责 |
|------|------|
| Main Thread (Python) | poll_task() 循环，执行任务 |
| Reactor Thread | epoll 事件循环 (Master conn + Data Server) |
| Data Server epoll | 接收数据请求 |
| Data Server send threads | 发送数据响应（线程池，可配置） |
| Heartbeat Thread | 心跳发送 |

**关键设计**：
- 单线程 Reactor + I/O 线程池，避免锁竞争
- DataClient 使用独立 TCP socket（独立于主 Reactor）
- DataServer 采用 epoll + send_thread_pool 模式

### 4.3 核心设计决策

| 决策 | 原因 |
|------|------|
| nanobind 而非 pybind11 | 更小、更快、C++20 兼容 |
| bitsery 而非 protobuf | header-only、版本化支持、无代码生成 |
| CM 前缀容器别名 | 便于替换底层实现（如 absl） |
| 单线程 Reactor + IOThreadPool | 事件驱动 + I/O 异步，避免锁竞争 |
| DataClient 独立连接 | 每次读创建独立 socket，不走主 Reactor，多线程安全 |
| DataService 进程级单例 | Master 和 Worker 共享，仅更新触发源不同 |
| 三层降级读取 | 本地 → 缓存 → 远程，最大限度减少 Master 查询 |
| 两段式 wire 协议 | DataResponseProtocol 避免大 payload 的用户态拷贝 |
| ObjectCache 两层缓存 | low=压缩字节省 IO，high=反序列化对象省 CPU |
| FlyBufferPtr 共享所有权 | 零拷贝共享压缩字节，避免不必要的内存拷贝 |
| 进程模式 Worker | 独立 DataService 单例，避免线程模式的复杂性 |

---

## 五、数据流

### 5.1 读取流程（三层降级 + 缓存）

```
Worker A 读取 object_name:

1. 查询 ObjectCache.high (反序列化对象)
   └─ 命中 → 直接返回，省反序列化

2. 查询 ObjectCache.low (压缩字节)
   └─ 命中 → 解压 + 反序列化 → 返回

3. 查询本地索引（DataService.local_idx）
   └─ 找到 → DataReader.read_from_entries() → 返回数据

4. 查询远程索引缓存（DataService.remote_idx）
   └─ 找到 → DataClientPool.request() 直连目标 Worker B
       └─ Worker B 响应 → 返回数据

5. 查询 Master
   └─ request_remote_data(object_name) → Master 查询 DataService
       └─ 返回目标 Worker 地址
       └─ DataClientPool.request() 直连目标 Worker B
           └─ DATA_NOT_READY 时 50ms 重试，最多等待 30s
           └─ 成功后更新 remote_idx 缓存
```

### 5.2 写入流程

```
Worker A 写入 object_name:

1. 调用 db.write_object(object_name, data)
   └─ 检查 Database 是否冻结

2. 调用线程序列化 + 压缩（流式管线）

3. 立即填充 low cache（put_low）
   └─ 远程读可直接命中，无需等待落盘

4. 注册写入（commit_write）
   └─ on_write_started → local_idx 标记 INCOMPLETE
   └─ register_write → WriteRegisterMessage → Master
   └─ Master 标记数据就绪 → 调度依赖任务
   └─ 此时远程读已可从 cache 命中
   └─ 失败时回滚：cache.remove + on_write_failed

5. WBQ 后台线程执行 write_record 磁盘写入

6. 完成回调：on_write_completed → local_idx 标记 COMPLETE
   └─ 任务完成时发送 TaskCompleteMessage
   └─ Master 更新远程索引
```

**写注册协议**：
- Config.track_writes 启用时，记录每个任务写入的对象列表
- WorkerAgentContext 使用C函数指针回调模式

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
├─ 将数据库元信息写入 base_path/_META
└─ 后处理完成
```

**注意**：idx合并、_META生成等后处理尚未实现。

### 5.4 数据备份 (Backup)

#### 手动备份

```
Worker A 写入 object_name (backup=True):
1. 正常写入流程完成
2. Master 检测 backup_threshold，选择另一个 host 上的 Worker B
3. Master 向 Worker B 发送 TaskAssignMessage(__backup_object)
4. Worker B 从 Worker A 的 DataServer 拉取压缩数据（零解压落盘）
5. Worker B 写入本地 idx + data，发送 TaskComplete
6. Master 更新 remote_idx（两个 worker 都有该对象）
```

#### 自动备份（访问频率触发）

当 `auto_backup_enabled=1` 时，Master 在处理跨 Worker 读取请求时自动追踪访问频率，超过阈值后自动触发备份。

**相关配置**：

| 配置键 | 默认值 | 说明 |
|--------|--------|------|
| `auto_backup_enabled` | 0 | 自动备份开关 |
| `backup_threshold` | 100 | 触发自动备份的跨 Worker 读取次数 |
| `backup_replicas` | 2 | 目标备份数 |
| `backup_decay_interval` | 300 | 衰减检查间隔（秒） |
| `backup_decay_factor` | 50 | 衰减因子百分比 |

### 5.5 MapReduce 框架

Fly 提供基于 `@as_task` 的四阶段 MapReduce 管道：**Partition → Process → Merge → Finalize**。

```
MapReduceJob(db, output_name="result")
    │
    ▼ Phase 1: Partition (可跳过)
    input_data → partition_fn → N 个分区
    │
    ▼ Phase 2: Process
    N 个并行 @as_task，每个处理一个分区
    │
    ▼ Phase 3: Merge
    summary: 多阶段树形合并 (fan_in=min(N,8))
    full:    单阶段合并
    │
    ▼ Phase 4: Finalize (可选)
    finalize_fn 处理合并结果 → 持久化到 output_name
    │
    ▼ Cleanup
    移除所有 __mr__{job_id}__* 临时对象
```

### 5.6 load_db 流程

```
load_db(path) 恢复已有数据库：

Phase 1: Master 注册 db paths
├─ 读取 _DB_META（db_id, WorkerInfo 列表）
├─ 创建临时 Database，注册 db_id 到 DataService
└─ Master 不加载任何 idx 到 local_idx

Phase 2: 按 hostname 分配 worker
├─ 从 _DB_META 中提取 hostname → writer_ids 映射
├─ 检查现有 worker 的 hostname 匹配情况
├─ 对缺少 worker 的 hostname，spawn 新 worker 并传 --host
└─ 等待新 worker 连接

Phase 3: 定向 idx 加载
├─ 每个 hostname 的 writer_ids 发送给对应 hostname 的 worker
├─ Worker 加载 idx 到 local_idx，发送 IdxLoadAck
└─ Master 收到 ack 后从共享文件系统读取 idx，更新 remote_idx
```

---

## 六、通信协议

### 6.1 帧格式

```
┌────────────────┬─────────┬─────────────────┐
│ 4 bytes length │ 1 byte  │   payload       │
│ (帧长度)        │ 消息类型  │ (序列化数据)     │
└────────────────┴─────────┴─────────────────┘
```

### 6.2 两段式 DataResponse 协议

```
┌──────────────┬──────────┬─────────────────┬─────────────┬───────────────┬─────────┐
│ 4 bytes      │ 1 byte   │ 4 bytes         │ 1 byte      │ small_fields  │ raw     │
│ total_len    │ type=12  │ small_fields_len│ has_raw     │ (bitsery)     │ payload │
└──────────────┴──────────┴─────────────────┴─────────────┴───────────────┴─────────┘
```

**优势**：大 payload 保持为原始字节，避免用户态拷贝。

### 6.3 消息类型

| 消息类型 | 方向 | 说明 |
|---------|------|------|
| RegisterMessage | W→M | Worker 注册 |
| RegisterAckMessage | M→W | 注册确认 |
| HeartbeatMessage | W→M | 心跳 |
| TaskSubmitMessage | 任意→M | 任务提交 |
| TaskAssignMessage | M→W | 任务分配 |
| TaskCompleteMessage | W→M | 任务完成 |
| TaskFailedMessage | W→M | 任务失败 |
| DataReadyMessage | W→M | 数据就绪 |
| DataQueryMessage | W→M | 数据位置查询 |
| DataLocationMessage | M→W | 数据位置响应 |
| DataRequestMessage | W→W | 数据请求 |
| DataResponseMessage | W→W | 数据响应（两段式） |
| BackupTaskMessage | M→W | 备份任务 |
| CleanupTaskMessage | M→W | 清理任务 |
| CleanupCompleteMessage | W→M | 清理完成 |
| UpdateAttributesMessage | W→M | 属性更新 |
| DatabaseFreezeMessage | W→M | 数据库冻结 |
| IdxRequestMessage | M→W | 请求 idx 内容 |
| IdxResponseMessage | W→M | 返回 idx 内容 |
| ShutdownMessage | M→W | 关机广播 |
| DBPathRequestMessage | W→M | DB 路径查询 |
| DBPathResponseMessage | M→W | DB 路径响应 |
| WorkerPropertyUpdateMessage | W→M | Worker 属性更新 |
| ObjectRemovedMessage | W→M | 对象删除通知 |
| IdxLoadCommandMessage | M→W | 定向 idx 加载 |
| IdxLoadAckMessage | W→M | idx 加载确认 |

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
│   │   └── cpp/common_types.h  # CM* 类型别名
│   │
│   ├── core/                 # 核心基础模块
│   │   └── cpp/config.h/cpp  # 配置管理
│   │
│   ├── serialization/        # 序列化模块
│   │   └── cpp/serialization_macros.h  # FLY_SERIALIZE, FLY_ENCODE
│   │
│   ├── export/               # 导出宏定义
│   │   └── cpp/export_macros.h  # FLY_EXPORT_* 宏
│   │
│   ├── storage/              # 存储层 (Layer 1)
│   │   ├── cpp/
│   │   │   ├── database.h/cpp
│   │   │   ├── data_writer.h/cpp
│   │   │   ├── data_reader.h/cpp
│   │   │   ├── data_service.h/cpp
│   │   │   ├── data_server.h/cpp     # epoll+线程池数据服务
│   │   │   ├── object_cache.h        # 两层 LRU 读缓存
│   │   │   └── storage_manager.h/cpp
│   │   ├── export/storage_export.cpp
│   │   └── tests/
│   │
│   ├── network/              # 网络层 (Layer 2)
│   │   ├── cpp/
│   │   │   ├── transport_interface.h
│   │   │   ├── tcp_socket.h/cpp
│   │   │   ├── epoll_multiplexer.h/cpp
│   │   │   ├── connection_manager.h
│   │   │   ├── tcp_connection_manager.h/cpp
│   │   │   ├── reactor.h/cpp
│   │   │   ├── message_protocol.h/cpp    # MessageProtocol + DataResponseProtocol
│   │   │   ├── message_types.h
│   │   │   ├── data_client.h/cpp
│   │   │   └── data_client_pool.h/cpp    # 并发限制的数据请求池
│   │   ├── export/network_export.cpp
│   │   └── tests/
│   │
│   ├── task/                 # 任务系统层 (Layer 3)
│   │   ├── cpp/
│   │   │   ├── dependency_graph.h/cpp
│   │   │   ├── worker_manager.h/cpp
│   │   │   ├── task_scheduler.h/cpp
│   │   │   ├── task_manager.h/cpp
│   │   │   └── heartbeat_monitor.h/cpp
│   │   └── tests/
│   │
│   ├── agent/                # Agent 层 (Layer 4)
│   │   ├── cpp/
│   │   │   ├── master_agent.h/cpp
│   │   │   ├── worker_agent.h/cpp
│   │   │   └── task_executor.h/cpp
│   │   ├── export/agent_export.cpp
│   │   └── tests/
│   │
│   ├── log/                 # 日志模块
│   │   ├── cpp/logger.h/cpp
│   │   └── export/log_export.cpp
│   │
│   └── fly/                 # Python API (Layer 5)
│       ├── __init__.py      # 顶层导出
│       ├── runtime.py       # Agent 生命周期管理
│       ├── mapreduce.py     # MapReduce 框架
│       └── py/
│
├── qa/                       # 项目级集成测试
├── docs/                     # 设计文档
└── scripts/                  # 辅助脚本
```

---

## 八、实现状态

### 已完成（✅）

- **Layer 0**：基础设施层（WORKSPACE, BUILD, 宏定义, Config, Logger）
- **Layer 1**：存储层（Database, DataService, DataServer, ObjectCache）
  - DataWriter 流式管线（compress_to_buffer + write_record）
  - DataReader 实例方法
  - DataService 统一索引（local_idx + remote_idx + worker_registry）
  - ObjectCache 两层 LRU 缓存（low=压缩字节, high=反序列化对象）
  - DataServer epoll + send_thread_pool
  - FlyBufferPtr 零拷贝共享
- **Layer 2**：网络层（Reactor, TCP, 消息协议）
  - Transport + EpollMultiplexer + ConnectionManager 抽象
  - DataResponseProtocol 两段式传输
  - DataClientPool 并发限制
  - 33 种消息类型
- **Layer 3**：任务系统层（DependencyGraph, TaskScheduler, WorkerManager）
- **Layer 4**：Agent 层（MasterAgent, WorkerAgent, TaskExecutor）
  - WorkerAgentContext C函数指针回调
  - 失败任务持久化 + restart_failed_tasks
  - load_db 按 hostname 分配 worker
  - 动态 Worker 属性管理
- **Layer 5**：Python API（@as_task, open_db, launch_workers, wait_tasks）
  - MapReduce 框架

**测试覆盖**：
- C++ 单元测试 + Python 测试
- QA 集成测试（含 backup + load_db 多 host 分布式测试）

### 尚未实现（⏳）

- **SSH Worker 启动**：launch_ssh_workers 接口设计完成，未实现
- **自定义 Worker 启动**：launch_custom_workers 接口设计完成，未实现
- **Database Freeze 后处理**：idx 合并、_META 生成
- **Locality 调度**：数据位置感知的任务分配
- **Worker 失败恢复**：任务重新调度
- **Worker role 调度**：role-based 任务分配

---

*文档更新日期: 2026-06-17*
