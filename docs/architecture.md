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
| Python 绑定 | nanobind（经 FLY_EXPORT_* 宏封装） |
| 序列化 | bitsery (header-only, 版本化支持) |
| 构建系统 | Bazel + fly.sh（自动刷新 clangd） |
| 测试框架 | gtest + pytest |
| 压缩库 | LZ4 / ZLIB / ZSTD |
| 格式化库 | fmt (header-only) |
| 网络 | TCP (epoll)，Transport 抽象，支持扩展 |

### 架构分层

```
┌─────────────────────────────────────────────────────────────────┐
│                         Master Node                              │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────────┐ │
│  │ Task Scheduler│  │ Dependency  │  │ DataService             │ │
│  │ (FIFO+priority│  │ Graph       │  │ (local/remote idx)      │ │
│  │ +locality)   │  └─────────────┘  └─────────────────────────┘ │
│  └─────────────┘         │                    │                 │
│         └────────────────┼────────────────────┘                 │
│                          │                                      │
│  ┌───────────────────────┴────────────────────┐                │
│  │ Reactor (epoll TCP Server, Port 8000)       │                │
│  │ + HeartbeatMonitor Thread                   │                │
│  └─────────────────────────────────────────────┘                │
└───────────────────────────────┬─────────────────────────────────┘
                                │ TCP
          ┌─────────────────────┼─────────────────────┐
          │                     │                     │
          ▼                     ▼                     ▼
┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐
│   Worker 1      │  │   Worker 2      │  │   Worker N      │
│   (hybrid)      │  │   (hybrid)      │  │ (storage_only)  │
│ ┌─────────────┐ │  │ ┌─────────────┐ │  │ ┌─────────────┐ │
│ │Task Executor│ │  │ │Task Executor│ │  │ │（无计算任务， │ │
│ └─────────────┘ │  │ └─────────────┘ │  │ │仅数据面）    │ │
│ ┌─────────────┐ │  │ ┌─────────────┐ │  │ └─────────────┘ │
│ │ObjectCache  │ │  │ │ObjectCache  │ │  │ ┌─────────────┐ │
│ │(low+high)   │ │  │ │(low+high)   │ │  │ │ObjectCache  │ │
│ └─────────────┘ │  │ └─────────────┘ │  │ │(low+high)   │ │
│ ┌─────────────┐ │  │ ┌─────────────┐ │  │ └─────────────┘ │
│ │Data Server  │ │  │ │Data Server  │ │  │ ┌─────────────┐ │
│ │(epoll+pool) │ │  │ │(epoll+pool) │ │  │ │Data Server  │ │
│ └─────────────┘ │  │ └─────────────┘ │  │ │(epoll+pool) │ │
│                 │  │                 │  │ └─────────────┘ │
└─────────────────┘  └─────────────────┘  └─────────────────┘
```

---

## 二、启动与进程模型

### Binary启动方式

统一使用 `fly` binary启动，通过参数区分Master/Worker模式：

```bash
# Master模式：直接传入用户脚本（无flag）
fly user_tasks.py

# Worker模式：--worker 指定（--master-host/--master-port 指向 master）
fly --worker --worker-id 1 --master-host master --master-port 8000
fly --worker --worker-id 2 --master-host master --master-port 8000 --worker-role storage_only

# Master交互模式（可选）
fly -i user_tasks.py
```

### CLI参数说明

| 参数 | Master模式 | Worker模式 |
|------|-----------|-----------|
| positional arg | 用户Python脚本路径 | 不适用 |
| `--worker` | 不设置 | 设置即进入 Worker 模式 |
| `--worker-id N` | 不适用 | Worker ID（默认 0） |
| `--master-host HOST` | 不设置（自己就是Master） | Master 地址（默认 127.0.0.1） |
| `--master-port PORT` | Master 监听端口 | Master 端口（默认 0） |
| `--log-dir DIR` | 日志目录（默认 fly_log） | 同左 |
| `--worker-attributes` | 不适用 | worker 属性（capability 匹配） |
| `--host HOST` | 不适用 | 覆盖 hostname（用于多 host 测试） |
| `--config-file PATH` | 启动时加载配置文件 | 同左 |
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

# 设置共享参数（必须在启动worker前；set_int/set_str，无 kwargs 风格 set()）
config.set_int("heartbeat_timeout", 120)
config.set_int("heartbeat_interval", 5)
config.set_int("aggregation_threshold", 1048576)       # 1MB
config.set_int("large_file_threshold_kb", 65536)       # 64MB
config.set_int("block_size", 134217728)                # 128MB
config.set_int("track_writes", 1)                      # 启用写入跟踪
config.set_int("data_server_threads", 4)               # Data Server线程池大小
config.set_str("log_dir", "/path/to/logs")             # 所有进程共享的日志目录

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
- 默认策略：FIFO（同优先级内按 task_id 升序）
- 任务优先级（`priority`，默认 10）：`@as_task(priority=N)` 设置，数值越大越优先调度。`get_ready_tasks()` 按 `(priority desc, task_id asc)` 排序。head-of-line skip：高优先级 task 若暂无可匹配 worker（如缺 capability），跳过它继续调度低优先级（不阻塞）。默认 10 取中点值，可双向调节：<10 让路，>10 抢先。全链路透传（TaskMetadata 崩溃恢复 + TaskSubmitMessage 递归提交）。详见 [`priority-scheduling-design.md`](priority-scheduling-design.md)。
- 数据 locality 调度（Config `locality_scheduling_enabled`，默认 1 开启）：启用后 scheduler 按 worker 持有的输入数据总量（score）选亲和性最优的 idle worker。三阶段算法：① capability 完整匹配优先（强约束）；② locality 偏好（score 最大且不降低 capability 质量）；③ 兜底（allow_degrade）。**分层无环**：master 在 `schedule_tasks()` 入口按 task 依赖预计算 `locality_hint_`（POD，worker_id→持有字节数）注入 graph，scheduler 只消费此 hint，不接触 DataService（分层修复决策记录见 roadmap.md §三）。持久 score 缓冲区复用。
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
1. 调用线程序列化 + 压缩（`compress_to_buffer` 流式管线）。小对象优化：payload ≤ `compression_threshold`（默认 4KB）时跳过压缩走 raw passthrough，header 记 NONE，读取端透明处理
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

**等待 worker 注册（bsub/LSF 等慢调度场景）**：

`launch_workers()` 唤起后只登记占位符（expected worker），**不假设 worker 会在
任何固定时限内启动注册**——bsub 调度排队下 worker 可能分钟级才真正唤起。
需要等全部唤起的 worker 完成注册时用专用 API：

```python
from fly import wait_workers_registered, expect_workers

# 等 launch_workers 唤起的全部 worker 注册完成（默认窗口 = config
# 'worker_register_timeout'（默认 0=不假设时限，无限等），等待期间每 30s 打进度）
wait_workers_registered()

# 外部唤起（如 bsub 脚本直接跑 `fly --worker`）时手动登记占位符：
expect_workers([101, 102])          # 之后 bsub 提交对应 worker
wait_workers_registered(timeout=600)
```

worker 生命周期语义（用户确认，两阶段）：

**首次注册**（`worker_register_timeout`，默认 0=master 不等待不假设任何超时）：
- master 侧：唤起占位符无限期有效（worker 任意时刻注册都被接受）；显式设值
  才启用超时清理并作为 `wait_workers_registered()` 默认超时。
- worker 侧：首连失败按指数退避重试（首次间隔
  `worker_connect_retry_initial_ms`=500ms，×2 递增，单次上限 10s），窗口同键。

**断连重连**（`worker_reconnect_timeout`，默认 120s=2min，两侧对等）：
- 断连仅指**网络闪断**（master 挂=全群失败——worker 最多多活宽限窗口后
  干净退出）。
- worker 侧：断连后指数退避重连（同上参数），期间 **task 继续执行**，完成的
  TaskComplete/TaskFailed **缓冲**，重连注册确认后按序送达；宽限耗尽干净退出。
- master 侧：宽限内 **不判死**——task 保持 RUNNING、worker 状态保留（BUSY
  不被调度）、豁免心跳判死；重连注册保留 task 关联，迟到的上报经 assigned
  worker 校验后正常收敛。宽限超时判死：task 重排队 + pending frozen 清理 +
  **存储接管 / 数据全灭快速失败**（见下）。
- **存储接管（storage_takeover_enabled，默认关）**：判死后 master 显式驱动
  ——同 host 存活 storage_only worker 按 `recorded_workers_`（_DB_META 内存
  镜像，按 hostname 锚定）**只读加载**死 worker 的全部 writer idx（复用
  IdxLoad 链路；worker 侧 `restore_entries`，对半截事务按崩溃恢复语义丢弃）。
  ack → `rebuild_remote_idx_for_worker` → holder 追加 + `mark_data_ready`，
  等待 task 自动恢复调度。发起成功时全灭 fail 延迟至
  `storage_takeover_fail_timeout`（默认 60s，超时幂等重判兜底，防永久悬挂）；
  无 storage / 无 writer / 超 `storage_takeover_max_writers`（默认 64，防同
  host 连挂涌向单一 storage）→ 保持现状立即全灭 fail（流程 message
  AGENT::0003）。**安全红线**：只读加载，绝不以死 writer_id 写。
  **重复数据语义**：storage 已持有旧 backup 副本时，接管的同名 entry 与旧
  副本共存（`write_context_hash_` 等价的跳过、不同的一律加载），读路径按
  `entries.back()` 选最新——禁止按「对象已存在」跳过加载（backup 后源重写
  会数据回退）；restore 时对涉及对象失效 ObjectCache（防缓存旧字节绕过
  back() 选优）。
- **权威 remote_idx 保护**：master 的 remote_idx 是全集群唯一位置权威源，
  master 进程读失败**永不踢出**条目（worker 断连≠数据消失，重连后位置必须
  仍在）；worker 本地视图的踢副本保留（自愈，可 TIER3 刷新恢复）。
- `worker_reconnect_timeout=0` 为"断连即死"逃生口（旧语义）。
- **宽限超时判死提醒（AGENT::0006）**：宽限耗尽 worker 未重连 → WARN 级
  user message 附带手动重启命令（`fly --worker --worker-id N --master-host H
  --master-port P --log-dir D [--host HOSTNAME]`）。worker 侧重连上限与
  master 宽限同键对等（`worker_reconnect_timeout`），两侧同窗口收敛——
  worker 放弃重连自行退出、master 判死并提醒，无无限重试残留。
- **重复注册防护（先到先得）**：同 worker_id 的第二个注册（网络分区恢复
  的旧实例 vs 手动重启的新实例竞态）到达时，若该 id 已有活跃连接 → master
  回 `duplicate` RegisterAck，后到者自行干净退出——单实例保证，两份注册
  信息不会同时生效。

`load_db`/`merge_db` 内部的补 spawn 等待已改用此机制。

**Worker能力说明**：
- `role`（静态身份）：hybrid（默认，任务执行+数据存储）/ storage_only（存储
  worker）——**独立于 attributes**（可随时增减、参与调度匹配）：注册时设定、
  **不可变更**（无任何修改途径）；**调度决策不感知 storage_only**（idle 候选层
  过滤，scheduler 无 role 概念），它仍参与心跳判死/数据面/internal 数据 task
  （merge/backup）与 backup 目标。经 `launch_workers([{"role": "storage_only"}])`
- **自动补齐存储节点（auto_storage_nodes_enabled，默认关）**：master 周期
  检测（`auto_storage_check_interval` 节流，默认 30s，挂 heartbeat 循环）
  「有活 worker 但无活 storage_only」的 host，经该 host 任一活 hybrid worker
  发 `StorageSpawnRequest` 在本地 spawn storage worker。spawn 语义：**完全
  独立的进程**（posix_spawn `/proc/self/exe` 同版本 + SETSID 脱离进程树 +
  **fd 零继承**——枚举 `/proc/self/fd` 全部关进 file_actions，子进程只带
  stdio；不关会把 master 连接 fd 带进子进程，发起 worker 死后内核不发 FIN、
  master 永远看不到断连，实测卡死 summary/drain）+ Config 落盘传递
  （`.fly_config_autospawn_<pid>`，无共享文件系统假设）+ detached waitpid
  回收。worker_id 由 master 高基区（100000+）分配避开 Python launch 低位
  序列；spawn 占位（120s 超时）防周期内重复 + Ack 失败退避（3 次放弃该
  host）；storage 注册到达即占位/失败计数清零（外部 launcher 唤起的同样
  视为已覆盖）。LSF/bsub 环境下 spawn 侵占作业资源配额，须在允许的环境
  显式开启。
  或 CLI `--worker-role` 设定
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
│  - DataClientPool: keep-alive 连接池 + 并发限制的数据请求      │
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

| 线程 | 职责 | 停止方式 |
|------|------|---------|
| Main Thread (Python) | 用户脚本执行、任务提交 | — |
| Reactor Thread | epoll 事件循环，处理所有Worker消息 | `reactor_->stop()` |
| Heartbeat Thread | 每 5s 检查 Worker 心跳超时 | CV notify + join |

**Worker节点**：

| 线程 | 职责 | 停止方式 |
|------|------|---------|
| Main Thread (Python) | poll_task() 循环，执行任务 | — |
| Reactor Thread | epoll 事件循环 (Master conn + Data Server) | `reactor_->stop()` |
| Data Server epoll | 接收数据请求 | stop() |
| Data Server send threads | 发送数据响应（线程池，可配置） | stop() |
| Heartbeat Thread | 每 10s 发送心跳 | CV notify + join |

> 心跳周期为代码硬编码（master 检查 5s / worker 发送 10s，`master_agent.cpp` / `worker_agent.cpp`）；config 键 `heartbeat_interval=5` 当前无消费方。

**关键设计**：
- 单线程 Reactor（事件循环）+ handler lane 池：帧提取在 reactor 线程，handler 在
  `conn_id % handler_lanes`（默认 4）的专用串行 lane 执行——同连接消息严格保序，跨连接并行
- DataClientPool 使用独立 TCP socket（独立于主 Reactor，keep-alive 复用）
- DataServer 采用 epoll + send_thread_pool 模式

### 4.3 核心设计决策

| 决策 | 原因 |
|------|------|
| nanobind 而非 pybind11 | 更小、更快、C++20 兼容 |
| bitsery 而非 protobuf | header-only、版本化支持、无代码生成 |
| CM 前缀容器别名 | 便于替换底层实现（如 absl） |
| 单线程 Reactor + handler 串行 lane | 事件循环不被重 handler 阻塞；同连接保序、跨连接并行 |
| DataClientPool 独立连接 | keep-alive fd 复用，不走主 Reactor，多线程安全 |
| DataService 进程级单例 | Master 和 Worker 共享，仅更新触发源不同 |
| 三层降级读取 | 本地 → 缓存 → 远程，最大限度减少 Master 查询 |
| 两段式 wire 协议 | DataResponseProtocol 避免大 payload 的用户态拷贝 |
| ObjectCache 两层缓存 | low=压缩字节省 IO，high=反序列化对象省 CPU |
| FlyBufferPtr 共享所有权 | 零拷贝共享压缩字节，避免不必要的内存拷贝 |
| 进程模式 Worker | 独立 DataService 单例，避免线程模式的复杂性 |
| headers 而非 C++20 Modules | Python 绑定生态不兼容 |

### 4.4 模块依赖关系

```
main → agent → task → network → storage → core → common
              ↓         ↓          ↓
           serialization        log
              ↑
            export → nanobind
```

依赖方向：上层依赖下层，**禁止反向依赖**（BUILD 级无环，是 fly 对外宣称的核心工程优势）。

| 模块 | 依赖 |
|------|------|
| common | 无依赖（纯类型别名 + CMSharedPtr） |
| core | common |
| serialization | common, bitsery |
| export | nanobind |
| log | fmt |
| storage | core, serialization, export, common |
| network | core, serialization, export, common, log |
| task | network, storage, core, serialization, common |
| agent | task, network, storage, core, serialization, export, common, log |
| main | 全部 C++ 模块（链接入口） |
| fly/ (Python) | 所有 C++ 导出模块 |

---

## 五、数据流

### 5.0 任务生命周期（端到端）

```
定义 → 提交 → 调度 → 执行 → 完成

[用户代码] @as_task 装饰器 → wrapper 函数
    ↓ 调用
[提交] wrapper() → TaskSubmitMessage → Master（task 体内提交走 TaskSubmitAck 强语义）
    ↓
[调度] DependencyGraph 检查依赖 → TaskScheduler（priority + locality 三阶段匹配）
    → TaskAssignMessage → Worker
    ↓
[执行] Worker TaskExecutor → import module → pickle.loads → 执行原始函数
    ↓
[完成] TaskCompleteMessage → Master → mark_data_ready → schedule_tasks（触发下游）
```

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
       └─ 副本遍历顺序（lookup_all_remote_idx 统一排序，TIER2/TIER3 同源）：
          存活 storage_only > 存活 hybrid > 已死 holder（判死后条目保留的
          语义下排尾，避免每次读白费 connect 超时）；同级内按 net_probe
          带宽分降序。role 经 DataLocation 字段传播（TaskAssign 预取 +
          TIER3 应答），worker 回填本地 registry。

5. Tier 3: agent 层兜底回调
   └─ remote_compressed_read_handler(object_name)
       └─ Master 端：直接查本地 remote_idx（不走 reactor，避免 epoll 顺序不确定）
       └─ Worker 端：通过网络查询 Master
       └─ DataClientPool.request() 直连目标 Worker B
           └─ 单次尝试，返回 (found, can_still_produce)
           └─ can_still_produce=true 时 Python wait_obj 负责轮询重试
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
- 写入注册统一走 `WriteRegisterMessage` → `do_write_register` 单一入口（**master 自写也走此路径，同步调用**）；携带压缩后 size 用于 locality 调度亲和度打分
- Config.track_writes 启用时，记录每个任务写入的对象列表
- WorkerAgentContext 使用 std::function 回调模式（common/cpp/worker_context.h）

### 5.3 Database Freeze

```
任务调用 db.freeze()：

Worker端：
├─ 设置 is_frozen_ = true
├─ 在 base_path 创建 _FROZEN 标识文件
├─ 发送 DatabaseFreezeNotification（带 task_id）到 Master
├─ Master 回 DatabaseFreezeAckMessage（success / DB_ALREADY_FROZEN）
└─ 后续 write_object() 调用抛出异常

Master端（on_database_freeze_request）：
├─ stream 模式（dependency_update_mode==0）：即时置 frozen 并广播
├─ 非 stream 模式：进 pending_frozen_dbs_（db_path → task_id），
│   等 task 完成后 commit freeze，task 失败则 rollback
└─ db_instances_ / frozen_dbs_ 均以 db_path 为键（无 db_id，见 ADR 0002）
```

**注意**：合并 idx / merged.idx / _META 后处理未实现。

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

#### 自动备份（双层：worker suggest + master EWMA 聚合）

当 `auto_backup_enabled=1` 时，自动备份由两层协作触发：

**Worker 层（suggest 上报）**：worker TIER2 远程读后检查（`DataService::maybe_suggest_backup`）——
累积读流量（次数/字节）达阈值且 cooldown 已过，向 master 发 `WorkerBackupSuggestMessage` 上报增量，
然后 reset 清零重新累积（worker 不做时间衰减，累积值精确反映自上次 suggest 的增量）。

**Master 层（EWMA 聚合 + 判定）**：`on_worker_backup_suggest` 对多 worker 的 suggest 做 EWMA 时间衰减聚合
（按 suggest 到达频率衰减，不受单次传输时间影响 → 不惩罚大对象），然后 `evaluate_and_maybe_backup`：
- `score = cumulative / replicas`（副本数越多、单副本负载越低；cumulative 不 reset——backup 后 replicas++ → score 自然下降收敛）
- 双分数 OR：score_bytes 或 score_count 任一超阈值即视为热点
- 副本上限 `max_backup_replicas`（含原始）；大文件 + 异常高分可突破上限至 `max + backup_extra_slots`
- 每次 suggest 至多触发一份 backup（async，BackupComplete 后 replicas 才增长，score/replicas 反馈自然收敛）
- 备份目标选择：`select_backup_worker` 三级 key——① host 全新（host 故障域隔离，最高优先）→ ② storage_only 优先（不跑用户 task，进程可靠、数据面资源稳定）→ ③ 名下副本字节最轻（`get_worker_bytes_batch` 磁盘水位，防副本向少数存储节点倾斜）；host 全冲突时 best-effort 回退到无副本的 worker（回退层内同样按 ②③ 排序）

> 旧设计（master 在 DataQuery 路径统计读次数 + 后台定时衰减）已移除——worker 缓存对象位置后不再查 master，
> master 统计存在盲区；双层设计让真正执行远程读的 worker 负责上报。

**相关配置**：

| 配置键 | 默认值 | 说明 |
|--------|--------|------|
| `auto_backup_enabled` | 0 | 自动备份开关 |
| `worker_suggest_count_threshold` | 100 | worker 累积读次数达此值触发 suggest |
| `worker_suggest_bytes_threshold` | 1GB | worker 累积传输字节达此值触发 suggest |
| `worker_suggest_cooldown` | 60 | worker 两次 suggest 最小间隔（秒） |
| `master_ewma_decay_per_sec` | 1 | master EWMA 每秒衰减百分比（1 = 1%/s） |
| `backup_count_threshold` | 1000 | 每副本读次数达此值判定热点 |
| `backup_bytes_threshold` | 10GB | 每副本传输字节达此值判定热点 |
| `max_backup_replicas` | 3 | 正常副本上限（含原始） |
| `backup_large_object_threshold` | 1GB | 大文件判定阈值（可触发例外突破上限） |
| `backup_high_score_threshold` | 100GB | 大文件 score_bytes 超此值触发例外 |
| `backup_extra_slots` | 2 | 例外情况下超出 max_backup_replicas 的额外副本数 |

> 旧键 `backup_threshold` / `backup_replicas` / `backup_decay_interval` / `backup_decay_factor` 仍在 Config 默认表中
> 但新判定路径不消费；旧的 `evaluate_auto_backup` / `decay_after_backup` / `decay_remote_access` API 亦无生产调用方（仅测试引用）。

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
│ (帧长度, big-endian) │ 消息类型(uint8) │ (bitsery 编码) │
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

> 完整枚举见 `src/network/cpp/message_types.h`（值 1-57，其中 8/15/16 已退役空号，现役 54 种）。下表列主干消息。

| 消息类型 | 方向 | 说明 |
|---------|------|------|
| RegisterMessage | W→M | Worker 注册 |
| RegisterAckMessage | M→W | 注册确认（duplicate_=true 时后到者自行退出） |
| HeartbeatMessage | W→M | 心跳 |
| TaskSubmitMessage | 任意→M | 任务提交 |
| TaskSubmitAckMessage | M→W | task 体内提交转发的确认（request_id 匹配，带回 master 分配的 task_id） |
| TaskAssignMessage | M→W | 任务分配 |
| TaskCompleteMessage | W→M | 任务完成（含 written_objects 带 size） |
| TaskFailedMessage | W→M | 任务失败 |
| WriteRegisterMessage | W→M | 数据写入注册（placement 登记 + provenance + size，统一入口） |
| WriteRegisterAckMessage | M→W | 写入注册确认（拒绝时带 error_type） |
| DataQueryMessage | W→M | 数据位置查询 |
| DataLocationMessage | M→W | 数据位置响应 |
| DataRequestMessage | W→W | 数据请求 |
| DataResponseMessage | W→W | 数据响应（两段式） |
| BackupRequestMessage | W→M | 备份请求（master 收到后向备份目标发 `TaskAssignMessage(__backup_object)`） |
| WorkerBackupSuggestMessage | W→M | 上报 TIER2 读流量增量（auto_backup 双层设计的 worker 层，master EWMA 聚合后判定 backup） |
| StorageSpawnRequest/ACK | M→W / W→M | master 请求 worker 本地唤起 storage_only 节点（自动补齐） |
| WorkerProbe/ProbeAck | M→W / W→M | 疑似重复注册时的既有连接活性探测 |
| CleanupTaskMessage | M→W | 清理任务 |
| CleanupCompleteMessage | W→M | 清理完成 |
| UpdateAttributesMessage | W→M | 属性更新 |
| DatabaseFreezeNotification | W→M | 数据库冻结通知 |
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
│   ├── main/                # 程序入口
│   │   └── cpp/main.cpp     # fly binary 入口（setup_sys_path + import 各 _fly_* 模块）
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
  - DataClientPool keep-alive 连接池 + 并发限制
  - 54 种消息类型（值 1-57，8/15/16 退役空号）
- **Layer 3**：任务系统层（DependencyGraph, TaskScheduler, WorkerManager）
- **Layer 4**：Agent 层（MasterAgent, WorkerAgent, TaskExecutor）
  - WorkerAgentContext std::function 回调
  - 失败任务持久化 + restart_failed_tasks
  - load_db 按 hostname 分配 worker
  - 动态 Worker 属性管理
- **Layer 5**：Python API（@as_task, open_db, launch_workers, wait_tasks）
  - MapReduce 框架

**测试覆盖**：
- C++ 单元测试 + Python 测试
- QA 集成测试（含 backup + load_db 多 host 分布式测试）

### 尚未实现（⏳）

- **SSH Worker 启动**：launch_ssh_workers 接口设计完成，未实现（F1 降级，待多机测试环境）
- **自定义 Worker 启动**：launch_custom_workers 接口设计完成，未实现
- **Database Freeze 后处理**：idx 合并、_META 生成（F2 降级，非阻塞需求）

> 2026-08-16 修正过期项：~~Locality 调度~~（早已实现且默认开启，见 roadmap §一/§三）；~~Worker 失败恢复~~（已实现：断连宽限 + task 重新入队 + 数据全灭快速失败，见 §3.4）；~~Worker role~~（已实现 2026-08-15，见 §3.5 role 小节）。

---

*文档更新日期: 2026-08-16*
