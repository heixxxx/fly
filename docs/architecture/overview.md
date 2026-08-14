# Fly 分布式任务执行框架 — 整体架构概览

## 一、项目概述

**Fly** 是一个多机多进程分布式任务执行框架，采用 **C++ 核心 + Python 流程控制 + nanobind 桥接** 的混合架构。

### 核心特性

- Master 节点负责任务调度和数据元信息管理
- Worker 节点负责任务执行和数据存储
- 任务可在任意节点提交，支持递归任务提交
- 基于数据依赖的自动调度（依赖图 + 数据就绪检测）
- 数据传递依靠分布式文件存储系统 + Worker 间直连传输
- 三层降级读取 + 两层 LRU 缓存

### 技术栈

| 组件 | 技术选型 | 说明 |
|------|----------|------|
| C++ 标准 | C++20 | gcc12 编译器 |
| Python 绑定 | nanobind | 通过 FLY_EXPORT_* 宏封装 |
| 序列化 | bitsery | header-only, 版本化支持 |
| 构建 | Bazel + fly.sh | fly.sh 封装自动刷新 clangd |
| 测试 | gtest + pytest | C++ 单元测试 + Python 测试 |
| 压缩 | LZ4 / ZLIB / ZSTD | 可选压缩策略 |
| 网络 | TCP (epoll) | Transport 抽象，支持扩展 |

---

## 二、架构分层

```
┌──────────────────────────────────────────────────────────┐
│  Layer 5: Python 流程控制 (src/fly/)                      │
│  - @as_task 装饰器                                        │
│  - open_db, launch_workers, wait_tasks                   │
│  - MapReduceJob                                          │
├──────────────────────────────────────────────────────────┤
│  Layer 4: Agent 层 (src/agent/)                          │
│  - MasterAgent: 调度、元数据、备份                        │
│  - WorkerAgent: 任务执行、数据存储                        │
│  - TaskExecutor: Python 任务执行器                        │
├──────────────────────────────────────────────────────────┤
│  Layer 3: 任务系统层 (src/task/)                          │
│  - DependencyGraph: 依赖管理                              │
│  - TaskScheduler: FIFO 调度                               │
│  - WorkerManager: Worker 状态管理                         │
├──────────────────────────────────────────────────────────┤
│  Layer 2: 网络层 (src/network/)                          │
│  - Reactor: 单线程事件循环                                │
│  - Transport + EpollMultiplexer + ConnectionManager       │
│  - MessageProtocol + DataResponseProtocol (两段式)        │
│  - DataClientPool: keep-alive 连接池 + 并发限制的数据请求  │
├──────────────────────────────────────────────────────────┤
│  Layer 1: 存储层 (src/storage/)                          │
│  - Database: 统一存储接口                                 │
│  - DataWriter: 流式管线                                   │
│  - DataService: 统一索引 + DataServer                     │
│  - ObjectCache: 两层 LRU 缓存                            │
├──────────────────────────────────────────────────────────┤
│  Layer 0: 基础设施 (src/common, core, serialization,     │
│            export, log)                                   │
│  - CM* 类型别名, Config, Logger                          │
│  - FLY_SERIALIZE_*, FLY_EXPORT_* 宏                      │
└──────────────────────────────────────────────────────────┘
```

---

## 三、部署架构

```
┌─────────────────────────────────────────────────────────────────┐
│                         Master Node                              │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────────┐ │
│  │Task Scheduler│  │ Dependency  │  │ DataService             │ │
│  │(FIFO)       │  │ Graph       │  │ (local/remote idx)      │ │
│  └─────────────┘  └─────────────┘  └─────────────────────────┘ │
│         │                │                    │                 │
│         └────────────────┼────────────────────┘                 │
│                          │                                      │
│  ┌───────────────────────┴────────────────────┐                │
│  │ Reactor (epoll TCP Server, Port 8000)       │                │
│  │ + HeartbeatMonitor Thread                   │                │
│  └────────────────────────────────────────────┘                │
└───────────────────────────────┬─────────────────────────────────┘
                                │ TCP
          ┌─────────────────────┼─────────────────────┐
          │                     │                     │
          ▼                     ▼                     ▼
┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐
│   Worker 1      │  │   Worker 2      │  │   Worker N      │
│ ┌─────────────┐ │  │ ┌─────────────┐ │  │ ┌─────────────┐ │
│ │TaskExecutor│ │  │ │TaskExecutor│ │  │ │TaskExecutor│ │
│ └─────────────┘ │  │ └─────────────┘ │  │ └─────────────┘ │
│ ┌─────────────┐ │  │ ┌─────────────┐ │  │ ┌─────────────┐ │
│ │ObjectCache  │ │  │ │ObjectCache  │ │  │ │ObjectCache  │ │
│ │(low+high)   │ │  │ │(low+high)   │ │  │ │(low+high)   │ │
│ └─────────────┘ │  │ └─────────────┘ │  │ └─────────────┘ │
│ ┌─────────────┐ │  │ ┌─────────────┐ │  │ ┌─────────────┐ │
│ │Data Server  │ │  │ │Data Server  │ │  │ │Data Server  │ │
│ │(epoll+pool) │ │  │ │(epoll+pool) │ │  │ │(epoll+pool) │ │
│ └─────────────┘ │  │ └─────────────┘ │  │ └─────────────┘ │
└─────────────────┘  └─────────────────┘  └─────────────────┘
```

### 节点角色

| 角色 | 职责 |
|------|------|
| Master | 任务调度、依赖管理、心跳监控、数据索引管理 |
| Worker (hybrid) | 任务执行 + 数据存储 + 响应数据请求 |
| Worker (storage_only) | 仅数据存储，不执行计算任务 |

---

## 四、线程模型

### Master 线程

| 线程 | 职责 | 停止方式 |
|------|------|---------|
| Main Thread | 用户 Python 代码、submit_task、查询状态 | — |
| Reactor Thread | epoll 事件循环，消息收发和 handler 执行 | `reactor_->stop()` |
| Heartbeat Check Thread | 每 5s 检查 Worker 心跳超时 | CV notify + join |

### Worker 线程

| 线程 | 职责 | 停止方式 |
|------|------|---------|
| Main Thread | poll_task() 循环，执行任务，处理结果 | — |
| Reactor Thread | epoll 事件循环 (Master conn + Data Server) | `reactor_->stop()` |
| Data Server epoll | 接收数据请求 | stop() |
| Data Server send threads | 发送数据响应（线程池） | stop() |
| Heartbeat Thread | 每 10s 发送心跳 | CV notify + join |

---

## 五、核心数据流

### 5.1 任务生命周期

```
定义 → 提交 → 调度 → 执行 → 完成

[用户代码] @as_task 装饰器 → wrapper 函数
    ↓ 调用
[提交] wrapper() → TaskSubmitMessage → Master
    ↓
[调度] DependencyGraph 检查依赖 → TaskScheduler FIFO 匹配 → TaskAssignMessage → Worker
    ↓
[执行] Worker TaskExecutor → import module → pickle.loads → 执行原始函数
    ↓
[完成] TaskCompleteMessage → Master → mark_data_ready → schedule_tasks (触发下游)
```

### 5.2 数据读取流（三层降级 + 缓存）

```
read_object("key")
    │
    ├─ Layer 0: ObjectCache.high (反序列化对象)
    │     └── 命中 → 直接返回
    │
    ├─ Layer 1: ObjectCache.low (压缩字节)
    │     └── 命中 → 解压 + 反序列化
    │
    ├─ Layer 2: DataService.try_read_local(key)
    │     └── 内存索引 → DataReader → 直接文件读取
    │
    ├─ Layer 3: DataService.lookup_remote_idx(key)
    │     └── 缓存命中 → DataClientPool 直连目标 Worker
    │
    └─ Layer 4: request_remote_data(key) (最多 3 次重试)
          └── DataQuery → Master → DataLocation → DataClientPool 直连
```

### 5.3 数据写入流

```
write_object("key", obj)
    │
    ├─ 1. 调用线程序列化 + 压缩（compress_to_buffer 流式管线）
    ├─ 2. register_write → WriteRegisterMessage → Master do_write_register
    │      （provenance 校验 + mark_data_ready + update_remote_idx 带 size + schedule）
    └─ 3. WBQ 后台线程执行 write_record 磁盘写入
```

> 写入注册统一走 `WriteRegisterMessage` → `do_write_register` 单一入口（master 自写也走此路径，同步调用）。
> 携带压缩后 `size_bytes_` 用于数据 locality 调度亲和度打分。

---

## 六、消息协议

### 帧格式

```
┌──────────────┬──────────┬────────────────┐
│ 4 bytes      │ 1 byte   │ N bytes        │
│ total_len    │ msg_type │ payload        │
│ (big-endian) │ (uint8)  │ (bitsery 编码) │
└──────────────┴──────────┴────────────────┘
```

### 两段式 DataResponse 协议

```
┌──────────────┬──────────┬─────────────────┬─────────────┬───────────────┬─────────┐
│ 4 bytes      │ 1 byte   │ 4 bytes         │ 1 byte      │ small_fields  │ raw     │
│ total_len    │ type=12  │ small_fields_len│ has_raw     │ (bitsery)     │ payload │
└──────────────┴──────────┴─────────────────┴─────────────┴───────────────┴─────────┘
```

**优势**：大 payload 保持为原始字节，避免用户态拷贝。

### 消息类型总览（33 种 MessageType）

| 类型 | 方向 | 说明 |
|------|------|------|
| REGISTER | W→M | Worker 注册 |
| REGISTER_ACK | M→W | 注册确认 |
| HEARTBEAT | W→M | 心跳 |
| TASK_SUBMIT | 任意→M | 任务提交 |
| TASK_ASSIGN | M→W | 任务分配 |
| TASK_COMPLETE | W→M | 任务完成 |
| TASK_FAILED | W→M | 任务失败 |
| DATA_READY | W→M | 数据就绪 |
| DATA_QUERY | W→M | 数据位置查询 |
| DATA_LOCATION | M→W | 数据位置响应 |
| DATA_REQUEST | W→W | 数据请求 |
| DATA_RESPONSE | W→W | 数据响应（两段式） |
| SHUTDOWN | M→W | 关机广播 |
| DATABASE_FREEZE | W→M | 数据库冻结 |
| IDX_REQUEST | M→W | 请求 idx 内容 |
| IDX_RESPONSE | W→M | 返回 idx 内容 |
| CLEANUP_TASK | M→W | 清理任务 |
| CLEANUP_COMPLETE | W→M | 清理完成 |
| DB_PATH_REQUEST | W→M | DB 路径查询 |
| DB_PATH_RESPONSE | M→W | DB 路径响应 |
| WRITE_REGISTER | W→M | 写入注册 |
| WRITE_REGISTER_ACK | M→W | 写入注册确认 |
| BACKUP_TASK | M→W | 备份任务 |
| UPDATE_ATTRIBUTES | W→M | 属性更新 |
| WORKER_PROPERTY_UPDATE | W→M | Worker 属性更新 |
| OBJECT_REMOVED | W→M | 对象删除通知 |
| IDX_LOAD_COMMAND | M→W | 定向 idx 加载 |
| IDX_LOAD_ACK | W→M | idx 加载确认 |

---

## 七、目录结构

```
fly/
├── fly.sh                    # 构建脚本（必须使用）
├── src/                      # 源代码
│   ├── common/               # CM* 类型别名
│   ├── core/                 # Config, ProcessInfo
│   ├── serialization/        # FLY_SERIALIZE_* 宏
│   ├── export/               # FLY_EXPORT_* 宏
│   ├── storage/              # Database, DataService, DataServer, ObjectCache
│   ├── network/              # Reactor, Transport, MessageProtocol, DataClientPool
│   ├── task/                 # DependencyGraph, TaskScheduler, WorkerManager
│   ├── agent/                # MasterAgent, WorkerAgent, TaskExecutor
│   ├── log/                  # Logger
│   ├── main/                 # 程序入口
│   └── fly/                  # Python API
├── qa/                       # 集成测试
└── docs/                     # 设计文档
```

---

## 八、模块依赖关系

```
main → agent → task → network → storage → core → common
              ↓         ↓          ↓
           serialization        log
              ↑
            export → nanobind

依赖方向: 上层依赖下层，禁止反向依赖
- common: 无依赖（纯类型别名 + CMSharedPtr）
- core: 依赖 common
- serialization: 依赖 common, bitsery
- export: 依赖 nanobind
- storage: 依赖 core, serialization, export, common
- network: 依赖 core, serialization, export, common, log
- task: 依赖 network, storage, core, serialization, common
- agent: 依赖 task, network, storage, core, serialization, export, common, log
- fly/ (Python): 依赖所有 C++ 导出模块
```

---

## 九、设计决策记录

| 决策 | 原因 |
|------|------|
| nanobind 而非 pybind11 | 更小、更快、C++20 兼容 |
| bitsery 而非 protobuf | header-only、版本化支持、无代码生成 |
| CM 前缀容器别名 | 便于替换底层实现（如 absl） |
| headers 而非 C++20 Modules | Python 绑定生态不兼容 |
| 单线程 Reactor + IOThreadPool | 事件驱动 + I/O 异步，避免锁竞争 |
| DataClient 独立连接 | 每次读创建独立 socket，不走主 Reactor，多线程安全 |
| DataService 进程级单例 | Master 和 Worker 共享，仅更新触发源不同 |
| 三层降级读取 | 本地 → 缓存 → 远程，最大限度减少 Master 查询 |
| 两段式 wire 协议 | DataResponseProtocol 避免大 payload 的用户态拷贝 |
| ObjectCache 两层缓存 | low=压缩字节省 IO，high=反序列化对象省 CPU |
| FlyBufferPtr 共享所有权 | 零拷贝共享压缩字节，避免不必要的内存拷贝 |
| 进程模式 Worker | 独立 DataService 单例，避免线程模式的复杂性 |

---

## 十、待实现功能

| 优先级 | 功能 | 说明 |
|--------|------|------|
| 高 | Database Freeze 后处理 | Master 合并 idx → merged.idx + _META |
| 中 | SSH/Custom Worker 启动 | launch_ssh_workers, launch_custom_workers |
| 中 | Locality 优化调度 | 数据位置感知的任务分配 |
| 低 | 容错机制 | Worker 失联重调度、任务重试 |

---

*文档更新日期: 2026-06-17*
