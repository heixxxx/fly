# Reactor 异步化改造设计

> 状态: 设计中（待确认）
> 创建日期: 2026-06-14
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
  → IOThreadPool worker 线程：读取本地数据 + 压缩
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

当前 DataService transfer server 有自己的 IOThreadPool 做数据读取和压缩。但 completion 回到 reactor 线程执行发送。数据传输的发送路径需要改造为不阻塞 reactor。

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
- IOThreadPool / transfer server 的数据读取+压缩机制不变

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

## 八、待确认项

1. **DataResponseMessage 发送路径**：是否让 IOThreadPool completion 不再回到 reactor 线程，直接在 IO 线程发送？还是 DataService transfer server 应有完全独立的收发通道？
2. **并行线程池大小**：默认 2-4 个线程处理 PARALLEL 类消息？
3. **DATA_QUERY 分类**：已确认 PARALLEL 安全（trigger_auto_backup 的操作都有各自独立锁保护，不碰 DependencyGraph）。
4. **IOThreadPool process_completions() 是否完全移除**：如果 transfer completion 改为线程内直接执行，此机制可移除。但需确认是否有其他使用者依赖 process_completions 回到 reactor 线程。

---

## 九、影响范围

### 需要修改的文件

| 文件 | 改动 |
|------|------|
| `src/network/cpp/reactor.h` | 新增 MessageCategory / category_of() / SendQueue / 顺序队列，改造 dispatch_message |
| `src/network/cpp/reactor.cpp` | 实现 SendQueue、顺序队列消费线程、路由逻辑，移除 process_completions 调用 |
| `src/agent/cpp/master_agent.cpp` | `reactor_->send()` → `reactor_->post_send()`（~20 处） |
| `src/agent/cpp/worker_agent.cpp` | `reactor_->send()` → `reactor_->post_send()`（~20 处），transfer callback 发送路径改造 |
| `src/network/cpp/io_thread_pool.h/cpp` | completion 改为线程内直接执行（待确认） |

### 不需要修改的文件

- 消息定义 `message_types.h`
- 序列化 `serialization_macros.h` / `message_protocol.h`
- DataClient / MetadataClient / DataClientPool
- 所有 `.py` 文件
- 所有 test 文件（除非 handler 调用方式变化）
