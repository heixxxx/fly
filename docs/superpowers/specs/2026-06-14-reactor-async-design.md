# Reactor 异步化改造设计

> 状态: 设计中
> 创建日期: 2026-06-14
> 更新日期: 2026-06-14（追加 DataService 独立网络层设计）
> 关联文档: docs/ARCHITECTURE_REVIEW.md 3.1/3.6

---

## 一、问题背景

### 当前架构

Reactor 单线程循环：`epoll → recv → decode → handler → send`。

所有消息 handler 在 reactor 线程同步执行。handler 中如果调用 `reactor_->send()`，会直接调用 `transport_->send()`——该方法在 EAGAIN 时使用 `poll(POLLOUT, 30000)` 同步等待，**最多阻塞 reactor 线程 30 秒**。

此外，DataService 的 transfer completion 通过 `IOThreadPool::process_completions()` 在 reactor 线程执行，`DataResponseMessage`（可能 256MB）的 `transport_->send()` 也阻塞 reactor。

### 影响

- 某个 handler 阻塞（send 等待、大消息发送）→ 整个 reactor 停止处理所有连接的所有消息
- Heartbeat 超时、任务分配延迟、连接建立延迟

---

## 二、当前数据传输架构分析

### 两条独立的数据通道

| 路径 | 用途 | 连接方式 | 线程 |
|------|------|---------|------|
| **DataClient / MetadataClient** | Worker 主动拉取远程数据（read_object 触发） | 独立同步 TCP fd，connect→req→resp→close | 任务执行线程（阻塞等待） |
| **reactor + IOThreadPool** | Worker 响应他人的 DATA_REQUEST（被拉取方） | reactor 的 transport 长连接 | IO 线程读数据，reactor 线程发响应 |

### read_object 远程读取完整流程

```
Worker A 执行任务，调用 db.read_object("db:key")
  │
  ▼
Database::read_object_compressed → DataService::read_raw_compressed
  │
  ├─ 1. try_read_local_raw → 本地有数据？→ 直接返回
  │
  ├─ 2. 本地没有 → 查 remote_idx_ → 找到 host/port？
  │     │
  │     ▼ direct_compressed_read_handler_（注册在 WorkerAgent::start）
  │     DataClientPool::request(host, port, object_name)
  │     —— 独立同步 TCP fd，不经 reactor
  │     —— 在任务执行线程同步阻塞等待
  │     —— 连接目标端口是 data_server_port_（reactor transport listen 端口）
  │
  └─ 3. remote_idx_ 也没位置 → remote_compressed_read_handler_
        │
        ▼ WorkerAgent::request_remote_data(object_name)
        │
        ├─ MetadataClient::query_data_location(master_host, master_port, name)
        │  —— 独立同步 TCP fd，不经 reactor
        │
        └─ DataClient::request_compressed_data(host, port, name)
           —— 独立同步 TCP fd（同上）
```

### DATA_REQUEST 服务端处理（被拉取方）

```
Worker B 的 DataClient 连到 Worker A 的 data_server_port_
  → reactor transport epoll 检测到新连接 / 数据
  → reactor 线程解码 DataRequestMessage
  → on_data_request: DataService::submit_transfer(conn_id, ...)
  → IOThreadPool worker 线程：读取本地已压缩数据
  → completion 放入 completions_ 队列
  → reactor 下一轮循环: process_completions() 在 reactor 线程执行
  → transfer callback: reactor_->send(DataResponseMessage)
  → transport_->send() 可能阻塞 reactor 线程
```

### 连接生命周期模型

| 连接类型 | 生命周期 | 何时断开 |
|---------|---------|---------|
| Master ↔ Worker 长连接 | 进程级（永久） | 仅 worker crash 或整体退出 |
| Worker → Master 长连接 | 进程级（永久） | 仅 master crash 或退出 |
| Worker ↔ Worker 数据传输连接 | 临时（池化复用） | 可能中途主动断开（池满淘汰/超时） |

**关键**：reactor 管理的连接都是长连接（Master↔Worker）。DataClientPool 的临时连接目标端口虽然指向 reactor transport listen 端口，但 DataClient/MetadataClient 完全使用自己的同步 TCP fd，不经 reactor epoll。

### 当前问题

DataClient 连接的目标端口是 `data_server_port_`——即 reactor 的 `transport->listen` 端口。这意味着：

1. DataClient 发的 DataRequestMessage 经过 reactor transport epoll
2. reactor 解码后调 `on_data_request` → `submit_transfer`
3. IO 线程完成后，completion 在 reactor 线程调 `reactor_->send` 发回 DataResponseMessage
4. DataClient 在自己的同步 fd 上 recv 这个响应

**reactor transport 同时承载了控制消息和数据传输消息**。DataResponseMessage（大消息）经过 reactor transport 发送，阻塞 reactor 线程。

---

## 三、消息分类

### 分类原则

- **SEQUENTIAL**：handler 修改 DependencyGraph / WorkerManager / DataService idx / 触发调度 / 触发条件变量。必须串行处理。
- **PARALLEL**：handler 只读查询、I/O 密集操作、不修改调度状态。可并行处理。

### Master 侧

| 消息类型 | handler 操作 | 分类 |
|---------|-------------|------|
| REGISTER | 注册 worker、更新 graph/manager/idx、发 RegisterAck | SEQUENTIAL |
| HEARTBEAT | 更新心跳、清理超时 worker | SEQUENTIAL |
| TASK_COMPLETE | 移除 graph 节点、调度新任务、广播 TaskAssign | SEQUENTIAL |
| TASK_FAILED | 移除 graph 节点、持久化失败记录、调度 | SEQUENTIAL |
| DATA_READY | 更新 remote_idx、评估备份、可能触发备份 | SEQUENTIAL |
| TASK_SUBMIT | 创建任务、加入 graph、调度 | SEQUENTIAL |
| WRITE_REGISTER | 注册写、评估 frozen、调度依赖 | SEQUENTIAL |
| WORKER_PROPERTY_UPDATE | 更新 worker 属性 | SEQUENTIAL |
| OBJECT_REMOVED | 移除 idx、广播通知、调度 | SEQUENTIAL |
| REMOVE_REQUEST | graph_->mark_data_removed + 调度 | SEQUENTIAL |
| BACKUP_REQUEST | 选择 backup worker、发 TaskAssign | SEQUENTIAL |
| BACKUP_COMPLETE | 更新 idx、调度 | SEQUENTIAL |
| IDX_LOAD_ACK | 恢复 idx、调度依赖任务 | SEQUENTIAL |
| DATA_QUERY | 查 remote_idx → 回 DataLocation（只读，备份评估有独立锁保护） | PARALLEL |
| DATA_REQUEST | submit_transfer（非阻塞投递到 IOThreadPool） | PARALLEL |
| DB_PATH_REQUEST | 查 db_paths_（只读） | PARALLEL |
| DATABASE_FREEZE | 转发广播 | PARALLEL |

### Worker 侧

| 消息类型 | handler 操作 | 分类 |
|---------|-------------|------|
| REGISTER_ACK | 设置 registered 标志、触发条件变量 | SEQUENTIAL |
| TASK_ASSIGN | 执行任务、发回结果 | SEQUENTIAL |
| SHUTDOWN | initiate_shutdown | SEQUENTIAL |
| DB_PATH_RESPONSE | 设置 db 路径、触发条件变量 | SEQUENTIAL |
| WRITE_REGISTER_ACK | 设置写注册结果、触发条件变量 | SEQUENTIAL |
| OBJECT_REMOVED | 清理本地 idx | SEQUENTIAL |
| DATABASE_FREEZE | 标记 DB frozen | SEQUENTIAL |
| REMOVE_ACK | 设置删除结果、触发条件变量 | SEQUENTIAL |
| REMOVE_COMMAND | 删除本地数据、回 ack | SEQUENTIAL |
| HEARTBEAT_ACK | 更新心跳时间戳 | SEQUENTIAL |
| BACKUP_ASSIGN | 执行备份任务 | SEQUENTIAL |
| DATA_REQUEST | submit_transfer（非阻塞投递到 IOThreadPool） | PARALLEL |
| IDX_LOAD_COMMAND | 文件 I/O（load idx） | PARALLEL |

### 未使用的消息类型

CLEANUP_TASK / CLEANUP_COMPLETE / IDX_REQUEST / IDX_RESPONSE / DATA_LOCATION / DATA_RESPONSE — 这些消息类型已定义但无 handler 注册或仅由 DataClient/MetadataClient 在独立 TCP fd 上收发（不经 reactor）。

---

## 四、方案对比

### 方案 A：单线程串行队列（最简单）

Reactor 仅做 epoll + recv，解码后把消息投递到一个串行队列。一个专用线程从队列消费，按序执行所有 handler。

- **优点**：最简单，零并发风险，recv 不再阻塞
- **缺点**：DATA_REQUEST/IDX_LOAD 等 I/O 密集消息会阻塞整个队列；无法利用并行性

### 方案 B：分类多队列（推荐）

Reactor 仅做 epoll + recv + 消息解码。根据消息类型路由到不同处理队列。回发消息走独立的 SendQueue 异步发送。

- **优点**：recv 永不阻塞；顺序消息保序；I/O 密集消息不阻塞调度；发送完全异步
- **缺点**：需要异步发送队列 + 连接生命周期管理

### 方案 C：Actor 模型（过度设计）

每个 conn_id 一个 actor，消息按 conn 路由到各自 mailbox。

- **优点**：天然连接级隔离
- **缺点**：跨 conn 的共享状态仍需同步；改动巨大；Worker 只有 1 个 conn 无并行收益

### 推荐：方案 B

---

## 五、方案 B 详细设计

### 5.1 Reactor 新职责

Reactor 线程**仅做三件事**：epoll → recv 拼接 → 解码+路由。不再直接调用任何 handler。

```
Reactor 线程循环:
  1. transport_->poll(10ms) → 获取事件列表
  2. CONNECT/DISCONNECT/ERROR → 直接处理（仅操作 recv_buffers_ 和 callback）
  3. DATA → 拼接到 recv_buffers_[conn_id]
  4. 循环解码完整帧:
     - 取 MessageType
     - 查路由表 → submit 到顺序队列 or 并行队列
     - 消息 payload 以值拷贝方式投递（脱离 buffer 生命周期）
  5. 不再调用 process_completions()（IOThreadPool completion 改为线程内直接执行）
```

### 5.2 消息路由表

```cpp
enum class MessageCategory : uint8_t {
    SEQUENTIAL,
    PARALLEL,
};

static constexpr MessageCategory category_of(MessageType type) {
    switch (type) {
        // SEQUENTIAL: 修改 DependencyGraph / WorkerManager / DataService idx / 调度
        case MessageType::REGISTER:
        case MessageType::HEARTBEAT:
        case MessageType::REGISTER_ACK:
        case MessageType::HEARTBEAT_ACK:
        case MessageType::TASK_SUBMIT:
        case MessageType::TASK_ASSIGN:
        case MessageType::TASK_COMPLETE:
        case MessageType::TASK_FAILED:
        case MessageType::DATA_READY:
        case MessageType::WRITE_REGISTER:
        case MessageType::WRITE_REGISTER_ACK:
        case MessageType::WORKER_PROPERTY_UPDATE:
        case MessageType::OBJECT_REMOVED:
        case MessageType::BACKUP_REQUEST:
        case MessageType::BACKUP_ASSIGN:
        case MessageType::BACKUP_COMPLETE:
        case MessageType::IDX_LOAD_ACK:
        case MessageType::REMOVE_REQUEST:
        case MessageType::REMOVE_ACK:
        case MessageType::REMOVE_COMMAND:
        case MessageType::DATABASE_FREEZE:
        case MessageType::SHUTDOWN:
            return MessageCategory::SEQUENTIAL;

        // PARALLEL: 查询 / I/O 密集 / 不操作调度状态
        case MessageType::DATA_REQUEST:
        case MessageType::DATA_QUERY:
        case MessageType::DB_PATH_REQUEST:
        case MessageType::DB_PATH_RESPONSE:
        case MessageType::IDX_LOAD_COMMAND:
            return MessageCategory::PARALLEL;

        default:
            return MessageCategory::SEQUENTIAL;
    }
}
```

### 5.3 处理队列

#### 顺序队列（单线程消费）

- `std::queue<Task>` + mutex + condition_variable
- 1 个专用线程串行执行
- 保证消息按到达顺序处理

#### 并行队列（线程池消费）

- 复用现有 HandlerThreadPool（已定义在 reactor.h，当前未使用）
- N 个线程（默认 2-4）
- 同一类型的并行消息之间无顺序保证

### 5.4 异步发送 SendQueue

```cpp
class SendQueue {
    struct PendingSend {
        uint64_t conn_id;
        CMString frame;          // 已编码的完整帧
    };

    std::queue<PendingSend> queue_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<bool> running_{false};
    std::thread send_thread_;

    void send_loop() {
        while (running_) {
            PendingSend task;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [&] { return !running_ || !queue_.empty(); });
                if (!running_ && queue_.empty()) return;
                task = std::move(queue_.front());
                queue_.pop();
            }
            transport_->send(task.conn_id, task.frame);
        }
    }

public:
    void submit(uint64_t conn_id, CMString frame) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push({conn_id, std::move(frame)});
        }
        cv_.notify_one();
    }
};
```

Reactor 暴露 `post_send()` 接口替代当前的同步 `send()`：

```cpp
template<typename T>
void post_send(uint64_t conn_id, const T& msg) {
    CMString frame = MessageProtocol::encode(msg);
    send_queue_->submit(conn_id, std::move(frame));
}
```

**关键变化**：当前 `reactor_->send()` 是同步调用 `transport_->send()`（可能阻塞 30 秒）。改为 `reactor_->post_send()` 后，编码后立即入队返回，实际发送在独立线程执行。

### 5.5 SendQueue 连接生命周期处理

由于 reactor 管理的连接都是**长连接**（Master↔Worker）：

- **不需要 conn 有效性预检查**——长连接不会中途主动断开
- Worker crash 时 reactor 的 EPOLLERR/HUP 触发 DISCONNECT，`remove_send_mutex()` 清理该 conn 资源
- SendQueue 对已断开的 conn_id 发送时，`transport_->send()` 返回 -1，记录 WARN 日志并丢弃

### 5.6 SendQueue 数据量分析

SendQueue **只处理控制消息**（全部轻量级元数据）：

| 消息 | 大小 |
|------|------|
| RegisterMessage / RegisterAckMessage | 元数据 + attributes 列表 |
| HeartbeatMessage / HeartbeatAckMessage | running_tasks 列表 |
| TaskAssignMessage | 任务参数（CMVector，通常几个元素） |
| TaskCompleteMessage / TaskFailedMessage | 元数据 + written_objects 列表 |
| DataReadyMessage / WriteRegisterMessage / WriteRegisterAckMessage | 元数据 |
| DataLocationMessage / DataQueryMessage | 元数据 |
| DbPathRequestMessage / DbPathResponseMessage | 元数据 |
| ShutdownMessage | 空 |
| ObjectRemovedMessage / RemoveCommandMessage / RemoveAckMessage | object_name |
| DatabaseFreezeNotification | db_id |
| BackupRequestMessage / BackupAssignMessage / BackupCompleteMessage | 元数据 |
| IdxLoadCommandMessage / IdxLoadAckMessage | 元数据 + writer_ids 列表 |

**DataResponseMessage（可能 256MB）不走 SendQueue**——见 5.7。

### 5.7 DataResponseMessage 发送路径

当前 DataResponseMessage 在 reactor 线程通过 `process_completions()` → transfer callback → `reactor_->send()` 发送，阻塞 reactor。

**待确认方案**（设计悬而未决）：

当前 DataService transfer server 有自己的 IOThreadPool 做异步数据读取。但 completion 回到 reactor 线程执行发送。数据传输的发送路径需要改造为不阻塞 reactor。

可能的方案：
1. **IOThreadPool completion 不再回到 reactor 线程**——worker 线程完成 task 后直接在自己的线程执行 completion callback（发送 DataResponseMessage），移除 `process_completions()` 机制
2. **DataService transfer server 完全独立**——有自己的 listen socket、accept/recv/send 线程池，完全不经 reactor transport

此部分需要进一步确认当前 DataService 的 transfer server 是否已有独立的线程池进行消息的收发与处理。

### 5.8 IOThreadPool process_completions() 改造

当前 `IOThreadPool` 的两阶段执行模型：
```
worker 线程: task() → completion 放入 completions_ 队列
reactor 线程: process_completions() → 执行 completion
```

改造后（如果方案 1）：
```
worker 线程: task() → 直接执行 completion（在自己的线程）
```

移除 `process_completions()` 和 reactor 中的 `io_pool_->process_completions()` 调用。

### 5.9 组件关系图

```
                    ┌──────────────────────────────────────────┐
                    │               Reactor 线程                │
                    │  epoll → recv → decode → route           │
                    │  CONNECT/DISCONNECT/ERROR 直接处理        │
                    │  (仅操作 recv_buffers_ + callback)        │
                    └──────┬──────────────────┬─────────────────┘
                           │                  │
                    SEQUENTIAL            PARALLEL
                           │                  │
                           ▼                  ▼
                   ┌──────────────┐   ┌──────────────────┐
                   │ 顺序处理线程  │   │ 并行线程池 (2-4)  │
                   │ (1 thread)   │   │ HandlerThreadPool │
                   │              │   │                  │
                   │ 调度状态修改  │   │ DATA_REQUEST     │
                   │ DependencyGraph│   │ DATA_QUERY       │
                   │ WorkerManager │   │ DB_PATH_REQUEST  │
                   │ DataService idx│   │ IDX_LOAD_COMMAND │
                   └──────┬───────┘   └────────┬─────────┘
                          │                    │
                          └────────┬───────────┘
                                   │ post_send()
                                   ▼
                          ┌──────────────────┐
                          │   SendQueue      │
                          │ (1 send thread)  │
                          │ FIFO 保序        │
                          │ 仅控制消息       │
                          │ transport_->send │
                          └──────────────────┘
```

---

## 六、不改动的部分

- 消息格式、序列化、FLY_SERIALIZE 全部不变
- `register_handler<T>()` API 不变（内部自动查路由表决定投递到哪个队列）
- `connect()` / `on_connect()` / `on_disconnect()` / `on_error()` API 不变
- DataClientPool / DataClient / MetadataClient（Worker 间主动拉取数据的同步 TCP 客户端）不变
- IOThreadPool / transfer server 的数据读取机制不变

---

## 七、关闭流程

```
stop():
  1. reactor 线程停止 poll
  2. 顺序队列 shutdown（等待剩余任务完成）
  3. 并行队列 shutdown（等待剩余任务完成）
  4. SendQueue shutdown（flush 剩余消息后退出）
  5. reactor 线程 join
```

保证关闭时不丢消息——队列中的 pending 任务和 pending send 都会执行完。

---

## 八、DataService 独立网络层

### 8.1 设计目标

DataService 拥有**独立的监听端口和消息收发处理线程**，完全脱离 reactor transport。所有数据请求与回复由 DataService 自己处理，避免大数据传输阻塞 reactor。

### 8.2 当前问题

当前 DataService 的 "transfer server" 只是 IOThreadPool（异步读取已压缩数据，无二次压缩），没有自己的网络监听层：

```
Worker::start()
  transport->listen("0.0.0.0", 0) → 端口 P（reactor transport，控制+数据共用）
  data_server_port_ = P
  DataService::start_transfer_server() → 只启动 IOThreadPool，无 listen socket
```

DataClient 连接到端口 P（reactor transport），DATA_REQUEST 经过 reactor epoll：
- reactor 解码 DATA_REQUEST → on_data_request → submit_transfer
- IO 线程读取已压缩数据
- completion 在 reactor 线程执行 → `reactor_->send(DataResponse)` → 阻塞 reactor

remote_idx_ 记录的端口也是 P（reactor 端口），不是独立的 DataService 端口。

### 8.3 改造方案

#### 端口拆分

```
Worker/Master 启动:
  transport->listen(host, port_C)       → 控制端口 C（给 reactor，仅控制消息）
  DataService::start_data_server(host, port_D) → 数据端口 D（独立 listen + accept 线程）

  data_server_port_ = D                  → 注册给 Master 的是 D
  remote_idx_ 记录 host:D                → DataClient 连接到 D
```

#### DataService 网络层架构

```
DataService::start_data_server(host, port):
  1. 创建独立 listen socket（SOCK_STREAM | SOCK_NONBLOCK | SOCK_REUSEADDR）
  2. bind + listen
  3. 启动 accept 线程（1 个）
  4. 启动 IO 处理线程池（已有 IOThreadPool，复用或扩展）

DataService accept 线程循环:
  while running:
    fd = accept(listen_fd)
    if fd < 0: continue
    submit 到 IO 处理线程池:
      1. recv DataRequestMessage（帧解码）
      2. 读取本地已压缩数据（现有 submit_transfer 逻辑）
      3. 编码 DataResponseMessage
      4. send DataResponseMessage（直接在 IO 线程，不经 reactor）
      5. close fd（短连接，与 DataClient 的 connect→req→resp→close 模式一致）
```

**关键**：DataService 的 accept/recv/send 完全在自己的线程中，不经过 reactor transport。DataResponseMessage 的大数据发送在 IO 线程执行，不阻塞任何其他线程。

#### DataClient 侧不变

DataClient / MetadataClient 当前已经是独立同步 TCP fd（connect → send req → recv resp → close）。改造后连接的目标端口从 P（reactor）变为 D（DataService），客户端代码逻辑不变。

MetadataClient 连接的是 Master 的控制端口 C（查询 DataLocation），返回的 data_host/data_port 是 D。DataClient 连接的是 D。

#### remote_idx_ 端口变更

```cpp
// Master::on_worker_register
DataService::register_worker(worker_id, msg.data_server_host_, msg.data_server_port_);
//                                              ↑ msg.data_server_port_ 现在是 DataService 端口 D

// Master::on_data_ready
DataService::update_remote_idx(msg.object_name_, msg.worker_id_, addr.host_, addr.port_);
//                                                                          ↑ addr.port_ 是 D
```

所有 `update_remote_idx` 和 `register_worker` 记录的端口自动变为 D，因为传入的 `data_server_port_` 值变了。不需要改 DataService 的 register/update 逻辑。

#### Master 侧

Master 是特殊的 Worker。Master 也需要 DataService 独立监听：

```cpp
// MasterAgent::start()
transport->listen(host_, port_);                    // 控制端口 C
data_server_port_ = port_;                          // ← 当前错误：C 不是数据端口

// 改造后:
transport->listen(host_, port_);                    // 控制端口 C
DataService::start_data_server(host_, 0);           // 数据端口 D（端口 0 = 内核分配）
data_server_port_ = DataService::get_data_port();   // ← 记录 D
DataService::register_worker(0, host_, data_server_port_);  // Master 自己也注册到 remote_idx
```

### 8.4 reactor 侧变化

改造后，DATA_REQUEST 和 DATA_QUERY 不再经过 reactor：

- **DATA_REQUEST**：从 reactor 的 handler 注册中**移除**。DataClient 直接连 DataService 端口 D。
- **DATA_QUERY**：MetadataClient 连 Master 控制端口 C 查询 DataLocation。当前 MetadataClient 用独立 TCP fd 同步查询，不经 reactor。但 Master 侧的 on_data_query handler 当前注册在 reactor 上，通过 DataClient 连接进来。

  等等——MetadataClient 连接的是 Master 的 reactor transport 端口（控制端口 C）。这意味着 DATA_QUERY 仍然经过 reactor。DATA_QUERY 是轻量级消息（只读查询），归为 PARALLEL 即可。

- **DATA_RESPONSE**：不再作为 reactor 消息类型。DataService 直接在自己的 fd 上发送，不经 reactor transport。

### 8.5 消息路由调整

改造后 reactor 不再需要处理 DATA_REQUEST：

| 消息 | 改造前 | 改造后 |
|------|--------|--------|
| DATA_REQUEST | reactor PARALLEL handler → submit_transfer | **移出 reactor**， DataService 自己 accept+处理 |
| DATA_RESPONSE | reactor completion → send | **移出 reactor**，DataService IO 线程直接 send |
| DATA_QUERY | reactor handler | 仍在 reactor（PARALLEL），MetadataClient 连控制端口查询 |

### 8.6 IOThreadPool process_completions() 移除

DataService 独立后，transfer completion 不再需要回到 reactor 线程：

- IO 线程完成数据读取后，直接在**自己的线程**编码并发送 DataResponseMessage
- 移除 `reactor::run()` 中的 `io_pool_->process_completions()` 调用
- 移除 transfer callback 中的 `reactor_->send()` 调用

IOThreadPool 的两阶段模型（task + completion）可以简化为单阶段（task 直接完成所有工作）。

### 8.7 组件关系图（完整版）

```
Worker 进程
├── Reactor Transport（端口 C：控制消息）
│   ├── Reactor 线程: epoll → recv → decode → route
│   │   ├── CONNECT/DISCONNECT/ERROR → 直接处理
│   │   ├── SEQUENTIAL 消息 → 顺序队列 → 1 个消费线程
│   │   └── PARALLEL 消息 → 并行队列 → 线程池（2-4）
│   │                    └── post_send() → SendQueue → transport->send
│   │
│   └── 连接: Master ↔ Worker 长连接
│
├── DataService Data Server（端口 D：数据消息）
│   ├── Accept 线程: accept → submit 到 IO 线程池
│   ├── IO 线程池（N 线程）:
│   │   ├── recv DataRequestMessage
│   │   ├── 读取本地已压缩数据
│   │   ├── 编码 DataResponseMessage
│   │   └── send DataResponseMessage（直接在 IO 线程）
│   │
│   └── 连接: DataClient 短连接（connect → req → resp → close）
│
└── DataClientPool（主动拉取远程数据）
    ├── MetadataClient → Master 控制端口 C: DataQuery → DataLocation
    └── DataClient → 目标 Worker 数据端口 D: DataRequest → DataResponse
```

### 8.8 Master 进程

Master 是特殊 Worker，同样的双端口模型：

```
Master 进程
├── Reactor Transport（端口 C：控制消息）
│   ├── Reactor 线程: 同上
│   └── 连接: 所有 Worker 长连接
│
└── DataService Data Server（端口 D：数据消息）
    ├── Accept 线程 + IO 线程池
    └── Master 自己的数据也被请求时（如 backup 读取），通过端口 D 响应
```

---

## 九、影响范围

### 需要修改的文件

| 文件 | 改动 |
|------|------|
| `src/network/cpp/reactor.h` | 新增 MessageCategory / category_of() / SendQueue / 顺序队列，改造 dispatch_message，移除 io_pool 相关 |
| `src/network/cpp/reactor.cpp` | 实现 SendQueue、顺序队列消费线程、路由逻辑，移除 process_completions 调用 |
| `src/network/cpp/io_thread_pool.h/cpp` | completion 改为线程内直接执行（或 DataService 内部自管理） |
| `src/storage/cpp/data_service.h` | 新增 data_server listen/accept 逻辑，start_data_server()，get_data_port()，移除 transfer callback 回 reactor 机制 |
| `src/storage/cpp/data_service.cpp` | 实现 accept 线程 + IO 线程 recv/send，端口管理 |
| `src/storage/cpp/BUILD` | 添加 network 依赖（DataService 需要网络层）或抽取独立模块 |
| `src/agent/cpp/master_agent.cpp` | 启动 DataService data server，移除 DATA_REQUEST handler 注册，移除 transfer callback 中 reactor->send |
| `src/agent/cpp/master_agent.h` | data_server_port_ 改为 DataService 端口 |
| `src/agent/cpp/worker_agent.cpp` | 同 Master，启动 DataService data server，移除 DATA_REQUEST handler |
| `src/agent/cpp/worker_agent.h` | data_server_port_ 改为 DataService 端口 |

### 不需要修改的文件

- 消息定义 `message_types.h`（消息结构不变）
- 序列化 `serialization_macros.h` / `message_protocol.h`
- DataClient / MetadataClient / DataClientPool（连接逻辑不变，目标端口自动变为 D）
- 所有 `.py` 文件
- WorkerAgentContext / TaskExecutor

---

## 十、实施顺序

建议分三个阶段，每阶段独立可测试：

### 阶段 1：DataService 独立网络层

1. DataService 新增 `start_data_server(host, port)` — 独立 listen socket + accept 线程
2. DataService accept 线程接收连接，IO 线程池处理 recv DataRequest → 读取已压缩数据 → send DataResponse → close fd
3. Master/Worker::start() 中启动 DataService data server，`data_server_port_` 改为 DataService 端口
4. 移除 reactor 上 DATA_REQUEST handler 注册
5. 移除 transfer callback 中 `reactor_->send()`，改为 IO 线程直接 send
6. 移除 `io_pool_->process_completions()` 调用
7. 验证：DataClient 连接 DataService 端口，数据请求/响应正常，reactor 不再处理 DATA_REQUEST

### 阶段 2：Reactor 异步化（消息分类 + 多队列）

1. 新增 `MessageCategory` + `category_of()` 路由表
2. Reactor `dispatch_message` 改为路由到顺序队列 / 并行队列
3. 启用 HandlerThreadPool（已有定义，当前未使用）作为并行队列
4. 新增顺序队列（单线程消费）
5. 验证：所有消息正常处理，顺序消息保序，reactor 不再直接执行 handler

### 阶段 3：异步发送 SendQueue

1. 新增 `SendQueue` 组件
2. Reactor 新增 `post_send()` 接口
3. Master/Worker 中所有 `reactor_->send()` 改为 `reactor_->post_send()`
4. 验证：控制消息异步发送，不阻塞 handler 线程

---

## 十一、DataServer/DataClientPool 线程池设计（最终方案）

> 更新日期: 2026-06-14
> 替代第八节中的 DataServer accept+IO 线程池设计

### 11.1 设计目标

解决 DataServer 连接处理中的线程占用问题。之前方案每个连接独占一个 IO 线程，并发连接数受限于线程数。新方案用 epoll 多路复用消除这一限制。

### 11.2 两类线程池

#### 服务线程（DataServer 侧）

- **池化**：启动时预创建固定数量的服务线程，常驻运行直到 stop。不随请求创建/销毁。
- **数量**：默认 2 个，通过 config `data_server_threads` 修改
- **职责**：处理其他 Worker 向本 Worker 的数据请求
- **连接模型**：长连接——不主动断开，被动等待请求端断开
- **机制**：
  - epoll 监听 listen_fd + 所有已 accept 的 client_fd
  - EPOLLIN 事件 → 从对应 fd recv 请求 → `try_read_local_raw` → send 响应
  - 一个服务线程可以同时管理数百个连接
  - 连接空闲时（等待 DATA_NOT_READY 重试），不占线程——epoll 只在有数据可读时唤醒

- **BUSY 机制**：
  - 保护场景：大对象 `send` 阻塞导致服务线程被占满（如 256MB 数据发送耗时长）
  - 触发条件：当所有服务线程都在处理 send 阶段时，新连接立即收到 BUSY 响应
  - 实现方式：服务线程进入 send 阶段时计数 `active_sends++`，accept 新连接时检查 `active_sends == thread_count` 则回复 BUSY
  - 请求端收到 BUSY 后短退避重试

#### 请求线程（DataClientPool 侧）

- **池化**：启动时预创建固定数量的请求线程，常驻运行直到 stop。不随请求创建/销毁。
- **数量**：默认 2 个，通过 config `data_client_pool_size` 修改
- **职责**：处理本 Worker 向其他 Worker 的数据请求（read_object 触发）
- **并发限制**：支持并发 read_object，但并发度受请求线程池大小限制。当池中所有线程都被占用时，新的 read_object 调用被阻塞，等待有线程归还后才能执行
- **连接模型**：
  - 每次 `read_object` 调用从池中获取一个空闲线程
  - 该线程创建到目标 Worker 的 TCP 连接
  - 在请求彻底成功/失败前不断开连接
  - 请求完成后归还线程到池中，关闭连接
- **DATA_NOT_READY 重试**：
  - 在同一连接上发送请求 → 收到 DATA_NOT_READY → sleep → 在同一连接上再次发送
  - 不新建连接，不切换线程
  - 连接保持直到请求成功或超时

### 11.3 连接复用规则

| 场景 | 连接行为 |
|------|---------|
| 单次 read_object | 创建 1 个 TCP 连接，请求完成后关闭 |
| 同一 read_object 的 DATA_NOT_READY 重试 | 复用同一连接（不新建） |
| 不同 read_object | 不同连接（不复用） |
| 服务端响应 | 长连接，不主动 close，等请求端断开 |

### 11.4 服务端 epoll 架构

```
DataServer 服务线程:
  epoll_fd = epoll_create1()
  epoll_ctl(ADD, listen_fd, EPOLLIN)

  while running:
    events = epoll_wait(epoll_fd, timeout=10ms)

    for event in events:
      if event.fd == listen_fd:
        client_fd = accept4(NONBLOCK)
        epoll_ctl(ADD, client_fd, EPOLLIN)
        conn_state[client_fd] = READING_HEADER

      elif event has EPOLLIN:
        fd = event.fd
        state = conn_state[fd]

        switch state:
          case READING_HEADER:
            recv 5 bytes (frame header)
            if complete → state = READING_PAYLOAD

          case READING_PAYLOAD:
            recv payload bytes
            if complete:
              decode DataRequestMessage
              try_read_local_raw(object_name)
              → encode DataResponseMessage
              send(fd, response)
              state = READING_HEADER  // 等待下一个请求（长连接）

      elif event has EPOLLHUP/EPOLLERR:
        close(fd)
        epoll_ctl(DEL, fd)
        conn_state.erase(fd)
```

**关键特性**：
- `try_read_local_raw` 是非阻塞的——不会卡住服务线程
- DATA_NOT_READY 响应立即返回——服务线程回到 epoll_wait
- 连接空闲等待期间（客户端 sleep 1s）不占任何线程
- 一个服务线程可处理任意数量的连接

### 11.5 请求端线程模型

```
DataClientPool::request(host, port, object_name, timeout):
  // 从预创建的线程池中获取空闲线程（池满时阻塞等待）
  acquire_request_slot()  // 信号量，池满时阻塞

  fd = create_connection(host, port)  // 新建 TCP
  deadline = now + timeout

  while true:
    send(fd, DataRequestMessage)

    recv(fd, header + payload)
    decode DataResponseMessage

    if success:
      close(fd)
      release_request_slot()  // 归还线程到池
      return data

    if error == "DATA_NOT_READY":
      if now >= deadline:
        close(fd)
        release_request_slot()
        return timeout_error
      sleep(1s)
      continue  // 同一 fd，同一线程

    if error == "BUSY":
      sleep(short_backoff)
      continue  // 同一 fd

    // OBJECT_NOT_FOUND 或其他错误
    close(fd)
    release_request_slot()
    return error
```

**关键特性**：
- 请求线程预创建池化，不随请求创建/销毁
- 不使用连接池（每次 read_object 新建连接，完成后关闭）
- DATA_NOT_READY 重试在同一连接上（无 TCP 重建开销）
- 请求并发度受线程池大小限制，池满时阻塞新请求
- acquire_request_slot / release_request_slot 保证线程池不超额

### 11.6 与旧设计的区别

| 维度 | 旧设计（第八节） | 新设计（第十一节） |
|------|----------------|-------------------|
| 服务端线程 | N 个 IO 线程，每连接独占 | 1-2 个 epoll 线程，多路复用 |
| 服务端连接 | 短连接（请求后 close） | 长连接（等请求端断开） |
| 客户端连接 | 连接池复用 | 每次请求新建，完成后关闭 |
| 并发上限 | 受 IO 线程数限制 | 受 fd 数限制（远大于线程数） |
| DATA_NOT_READY | 客户端重新 acquire | 同一连接重试 |
| 连接池 | DataClientPool 维护 fd 池 | 不需要连接池 |

### 11.7 实施步骤

1. **DataServer 改为 epoll 多路复用**——重写 handle_connection 为 epoll 事件驱动
2. **DataClientPool::request 改为每次新建连接**——移除池复用，同一连接内重试 DATA_NOT_READY
3. **新增 BUSY 响应**——accept 队列满时返回 BUSY
4. **移除旧的 IO 线程池**——`pending_fds_` 队列 + io_threads_ 改为 epoll
