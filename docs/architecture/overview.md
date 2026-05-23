# Fly 分布式任务执行框架 — 整体架构设计

## 一、项目概述

**Fly** 是一个多机多进程分布式任务执行框架，采用 **C++ 核心 + Python 流程控制 + nanobind 桥接** 的混合架构。

### 核心特性

- Master 节点负责任务调度和数据元信息管理
- Worker 节点负责任务执行和数据存储
- 任务可在任意节点提交，支持递归任务提交
- 基于数据依赖的自动调度（依赖图 + 数据就绪检测）
- 数据传递依靠分布式文件存储系统 + Worker 间直连传输
- 三层降级读取策略（本地 → 缓存 → 全程远程）

### 技术栈

| 组件 | 技术选型 | 说明 |
|------|----------|------|
| C++ 标准 | C++20 | gcc12 编译器 |
| Python 绑定 | nanobind | 通过 FLY_EXPORT_* 宏封装 |
| 序列化 | bitsery | header-only, 版本化支持 |
| 构建 | Bazel + fly.sh | fly.sh 封装自动刷新 clangd |
| 测试 | gtest + pytest | C++ 单元测试 + Python 测试 |
| 压缩 | LZ4 / ZLIB / ZSTD | 可选压缩策略 |
| 网络 | TCP (epoll) | 通过 TransportLayer 抽象，支持扩展 |

---

## 二、架构分层

```
┌──────────────────────────────────────────────────────────┐
│  Python 流程控制 (src/fly/) + 各模块 py/                  │
│  ┌─────────┐ ┌──────────┐ ┌─────────┐ ┌─────────┐      │
│  │ fly/    │ │ agent/py │ │ task/py │ │storage/py│ ...  │
│  │main.py  │ │agent.py  │ │task.py  │ │database │      │
│  │runtime  │ │executor  │ │         │ │  .py    │      │
│  └────┬────┘ └────┬─────┘ └────┬────┘ └────┬────┘      │
├───────┼───────────┼────────────┼───────────┼────────────┤
│  nanobind 导出层 (export/)                                │
├───────┼───────────┼─────────┼──────────┼─────────┼──────┤
│  Layer 4: Agent 层 (src/agent/)                          │
│  ┌──────────┐ ┌──────────┐ ┌───────────┐               │
│  │MasterAgent│ │WorkerAgent│ │TaskExecutor│               │
│  └─────┬─────┘ └─────┬────┘ └─────┬─────┘               │
├────────┼─────────────┼────────────┼──────────────────────┤
│  Layer 3: 任务系统层 (src/task/)                          │
│  ┌────────────────┐ ┌───────────────┐ ┌───────────────┐  │
│  │DependencyGraph │ │TaskScheduler  │ │WorkerManager  │  │
│  └───────┬────────┘ └───────┬───────┘ └───────┬───────┘  │
├──────────┼──────────────────┼─────────────────┼──────────┤
│  Layer 2: 网络层 (src/network/)                          │
│  ┌───────┐ ┌─────────┐ ┌────────────────┐ ┌──────────┐  │
│  │Reactor│ │Transport│ │MessageProtocol │ │IOThreadPool│ │
│  └───┬───┘ └────┬────┘ └───────┬────────┘ └─────┬────┘  │
├──────┼──────────┼──────────────┼────────────────┼────────┤
│  Layer 1: 存储层 (src/storage/)                          │
│  ┌──────────┐ ┌───────────┐ ┌──────────┐ ┌───────────┐  │
│  │Database  │ │DataService│ │DataWriter│ │DataReader │  │
│  └────┬─────┘ └─────┬─────┘ └────┬─────┘ └─────┬─────┘  │
├──────┼──────────────┼────────────┼──────────────┼────────┤
│  Layer 0: 基础设施 (src/common, core, serialization,     │
│            export, log)                                   │
│  ┌────────────┐ ┌──────┐ ┌──────────────┐ ┌──────────┐  │
│  │common_types│ │Config│ │serialization │ │export_mac│  │
│  └────────────┘ └──────┘ └──────────────┘ └──────────┘  │
└──────────────────────────────────────────────────────────┘
```

### 分层职责

| Layer | 名称 | 核心产出 | 测试数 |
|-------|------|----------|--------|
| Layer 0 | 基础设施 | WORKSPACE, BUILD, 宏定义, Config, Logger | 5 |
| Layer 1 | 存储层 | Database, DataService, DataWriter, DataReader | 45 |
| Layer 2 | 网络层 | Reactor, TCP, 消息协议, IOThreadPool | 35 |
| Layer 3 | 任务系统层 | DependencyGraph, TaskScheduler, WorkerManager | 28 |
| Layer 4 | Agent 层 | MasterAgent, WorkerAgent, TaskExecutor | 48 |
| Layer 5 | Python API | @as_task, Database, Master/Worker 封装 | E2E |

---

## 三、部署架构

```
┌─────────────────────────────────────────────────────────────────┐
│                         Master Node                              │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────────┐ │
│  │Task Scheduler│  │ Metadata   │  │ DataService             │ │
│  │(FIFO+pending)│  │ Manager    │  │ (local/remote idx)      │ │
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
│ │DataService  │ │  │ │DataService  │ │  │ │DataService  │ │
│ │IOThreadPool │ │  │ │IOThreadPool │ │  │ │IOThreadPool │ │
│ └─────────────┘ │  │ └─────────────┘ │  │ └─────────────┘ │
│ ┌─────────────┐ │  │ ┌─────────────┐ │  │ ┌─────────────┐ │
│ │Data Server  │ │  │ │Data Server  │ │  │ │Data Server  │ │
│ │(listen)     │ │  │ │(listen)     │ │  │ │(listen)     │ │
│ └─────────────┘ │  │ └─────────────┘ │  │ └─────────────┘ │
└─────────────────┘  └─────────────────┘  └─────────────────┘
```

### 节点角色

| 角色 | 职责 |
|------|------|
| Master | 任务调度、依赖管理、心跳监控、数据索引管理、DB 冻结后处理 |
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
| IOThreadPool | DataService 异步文件 I/O（数据传输） | pool.stop() |
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

### 5.2 数据读取流（三层降级）

```
read_object("key")
    │
    ├─ Layer 1: DataService.try_read_local(key)
    │     └── 内存索引 → DataReader → 直接文件读取
    │
    ├─ Layer 2: DataService.lookup_remote_idx(key)
    │     └── 缓存命中 → DataClient 直连目标 Worker
    │
    └─ Layer 3: request_remote_data(key) (最多 3 次重试)
          └── DataQuery → Master → DataLocation → DataClient 直连
```

### 5.3 数据写入流

```
write_object("key", obj)
    │
    ├─ 1. on_write_started → DataService 创建 incomplete LocalObjectInfo
    ├─ 2. register_write_with_master → WriteRegisterMessage → Master ACK
    ├─ 3. DataWriter::write (落盘)
    ├─ 4. on_write_completed → DataService 设置 COMPLETE
    └─ 5. flush() → on_flush → 通知 CV (唤醒等待的读取者)
```

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

### 消息类型总览（22 种）

| 类型 | 枚举值 | 方向 | 说明 |
|------|--------|------|------|
| REGISTER | 1 | W→M | Worker 注册 |
| REGISTER_ACK | 2 | M→W | 注册确认 |
| HEARTBEAT | 3 | W→M | 心跳 |
| TASK_SUBMIT | 4 | 任意→M | 任务提交 |
| TASK_ASSIGN | 5 | M→W | 任务分配 |
| TASK_COMPLETE | 6 | W→M | 任务完成 |
| TASK_FAILED | 7 | W→M | 任务失败 |
| DATA_READY | 8 | W→M | 数据就绪 |
| DATA_QUERY | 9 | W→M | 数据位置查询 |
| DATA_LOCATION | 10 | M→W | 数据位置响应 |
| DATA_REQUEST | 11 | W→W | 数据请求 |
| DATA_RESPONSE | 12 | W→W | 数据响应 |
| SHUTDOWN | 13 | M→W | 关机广播 |
| DATABASE_FREEZE | 14 | W→M | 数据库冻结 |
| IDX_REQUEST | 15 | M→W | 请求 idx 内容 |
| IDX_RESPONSE | 16 | W→M | 返回 idx 内容 |
| CLEANUP_TASK | 17 | M→W | 清理任务 |
| CLEANUP_COMPLETE | 18 | W→M | 清理完成 |
| DB_PATH_REQUEST | 19 | W→M | DB 路径查询 |
| DB_PATH_RESPONSE | 20 | M→W | DB 路径响应 |
| WRITE_REGISTER | 21 | W→M | 写入注册 |
| WRITE_REGISTER_ACK | 22 | M→W | 写入注册确认 |

---

## 七、目录结构

```
fly/
├── fly.sh                    # 构建脚本（必须使用）
├── BUILD                     # 顶层 BUILD
├── .bazelrc                  # Bazel 配置
├── WORKSPACE                 # Bazel 工作区
├── MODULE.bazel              # Bazel 模块定义
│
├── src/                      # 源代码
│   ├── common/               # 公共类型定义 (CMString, CMMap 等)
│   ├── core/                 # 核心基础模块 (Config)
│   ├── serialization/        # 序列化宏 (FLY_SERIALIZE, FLY_ENCODE)
│   ├── export/               # 导出宏定义 (FLY_EXPORT_*)
│   ├── storage/              # 存储层 (Database, DataService, DataWriter/Reader)
│   ├── network/              # 网络层 (Reactor, TCP, 消息协议)
│   ├── task/                 # 任务系统层 (调度, 依赖图, 元数据)
│   ├── agent/                # Agent 层 (MasterAgent, WorkerAgent)
│   ├── log/                  # 日志模块
│   ├── main/                 # 程序入口 (main.cpp)
│   └── fly/                  # Python API 包
│       ├── __init__.py       # 顶层导出 (open_db, as_task 等)
│       ├── agent.py          # Master/Worker Python 封装
│       ├── database.py       # _Database (三层读取)
│       ├── task.py           # @as_task 装饰器
│       ├── executor.py       # Worker 执行器
│       ├── runtime.py        # 运行时配置
│       ├── config.py         # Config Python 封装
│       └── main.py           # 初始化入口
│
├── qa/                       # 项目级集成测试
├── docs/                     # 设计文档
│   ├── architecture/         # 架构文档
│   ├── common/               # 公共模块文档
│   ├── core/                 # 核心模块文档
│   ├── serialization/        # 序列化模块文档
│   ├── export/               # 导出模块文档
│   ├── storage/              # 存储模块文档
│   ├── network/              # 网络模块文档
│   ├── task/                 # 任务系统文档
│   ├── agent/                # Agent 模块文档
│   ├── log/                  # 日志模块文档
│   └── python-api/           # Python API 文档
└── scripts/                  # 辅助脚本
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
- common: 无依赖（纯类型别名）
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
| Write Registration 协议 | 写前注册防止写入已冻结 DB，支持并发读取等待 |

---

## 十、待实现功能

| 优先级 | 功能 | 说明 |
|--------|------|------|
| 高 | Database Freeze 后处理 | Master 合并 idx → merged.idx + _META |
| 高 | 跨 Worker 数据读取 E2E 测试 | 完整三层流程端到端验证 |
| 中 | SSH/Custom Worker 启动 | launch_ssh_workers, launch_custom_workers |
| 中 | Locality 优化调度 | 数据位置感知的任务分配 |
| 低 | 数据副本策略 | backup=True + BackupManager |
| 低 | 容错机制 | Worker 失联重调度、任务重试 |
