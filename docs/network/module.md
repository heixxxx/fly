# Network 模块 — 网络层

## 模块概述

**位置**: `src/network/`

网络层提供基于 TCP (epoll) 的异步事件驱动通信框架，包括传输抽象、消息协议、Reactor 事件循环、IO 线程池和阻塞数据客户端。

---

## 核心文件

| 文件 | 说明 |
|------|------|
| `cpp/transport.h/cpp` | TransportLayer 抽象接口 |
| `cpp/tcp_transport.cpp` | POSIX TCP 实现 (epoll) |
| `cpp/reactor.h/cpp` | 单线程事件循环 |
| `cpp/message_protocol.h/cpp` | 二进制帧协议 |
| `cpp/message_types.h` | 22 种消息类型定义 |
| `cpp/io_thread_pool.h/cpp` | 通用线程池（submit + completion 回调） |
| `cpp/metadata_client.h/cpp` | 阻塞 TCP 元数据查询客户端（原名 MasterClient） |
| `cpp/data_client.h/cpp` | 阻塞 TCP 数据客户端 |
| `export/network_export.cpp` | Python 导出 |

---

## 类详细说明

### TransportLayer（传输抽象）

```cpp
class TransportLayer {
public:
    virtual ~TransportLayer() = default;

    // 服务端
    virtual void listen(const CMString& address, int port) = 0;
    void accept();  // 内部由 poll 触发

    // 客户端
    virtual uint64_t connect(const CMString& address, int port) = 0;

    // 数据收发
    virtual ssize_t send(uint64_t conn_id, const CMString& data) = 0;

    // 连接管理
    virtual void close(uint64_t conn_id) = 0;
    virtual void close_all() = 0;

    // 事件轮询
    virtual CMVector<TransportEvent> poll(int timeout_ms) = 0;

    // 辅助
    virtual int32_t get_bound_port() const = 0;
};

struct TransportEvent {
    enum Type { CONNECT, DATA, DISCONNECT, ERROR };
    Type type;
    uint64_t conn_id;
    CMString data;
};
```

### TCPTransport（TCP 实现）

基于 POSIX epoll 的 TCP 实现。

**关键特性**:
- 所有 socket 非阻塞 (`SOCK_NONBLOCK | SOCK_CLOEXEC`)
- listen_fd 用 `EPOLLIN`，client_fd 用 `EPOLLIN | EPOLLET`（边缘触发）
- `accept4` 直接创建非阻塞 client fd
- `drain_socket` 循环 recv 直到 EAGAIN，单次最多 64KB
- conn_id 单调递增，双映射 `conn_to_fd_` / `fd_to_conn_` 管理
- 工厂函数 `create_transport("tcp")` 支持扩展

**epoll 事件处理**:

```
poll(timeout_ms)
  → epoll_wait → 返回 CMVector<TransportEvent>
  ├── listen_fd EPOLLIN → accept4 → TransportEvent::CONNECT
  ├── client_fd EPOLLIN → drain_socket → TransportEvent::DATA
  ├── client_fd 空 recv → TransportEvent::DISCONNECT
  └── EPOLLERR|EPOLLHUP → TransportEvent::ERROR
```

---

### MessageProtocol（帧协议）

**帧格式**:

```
┌──────────────┬──────────┬────────────────┐
│ 4 bytes      │ 1 byte   │ N bytes        │
│ total_len    │ msg_type │ payload        │
│ (big-endian) │ (uint8)  │ (bitsery 编码) │
└──────────────┴──────────┴────────────────┘

total_len = 1 + payload.size()
```

**关键方法**:

| 方法 | 作用 |
|------|------|
| `encode<T>(msg)` | bitsery 序列化 → 拼接帧头 + payload |
| `decode<T>(buffer, msg)` | 校验长度+类型 → 反序列化 → 消费 buffer |
| `get_type(buffer)` | 读取第 5 字节得到 MessageType |
| `get_total_size(buffer)` | 读取前 4 字节大端序长度 |

**粘包处理**: `dispatch_message` 内 `while(!buffer.empty())` 循环解析。数据不足则等待更多数据。

---

### Reactor（事件循环）

```cpp
class Reactor {
public:
    explicit Reactor(std::unique_ptr<TransportLayer> transport);
    ~Reactor();

    void run();           // 启动事件循环（阻塞）
    void stop();          // 停止事件循环

    // Handler 注册
    template<typename MsgT>
    void register_handler(std::function<void(uint64_t, const MsgT&)> handler);

    // 发送消息（线程安全）
    void send(uint64_t conn_id, const CMString& data);
    template<typename MsgT>
    void send(uint64_t conn_id, const MsgT& msg);

    // IO 线程池
    void set_io_pool(IOThreadPool* pool);

private:
    std::unique_ptr<TransportLayer> transport_;
    CMMap<MessageType, GenericHandler> handlers_;
    CMMap<uint64_t, CMString> recv_buffers_;  // per-conn 拼接缓冲
    IOThreadPool* io_pool_ = nullptr;
    bool running_ = false;
};
```

**事件循环**:

```
reactor_->run()
  └── while(running_) {
        run_once(10ms)
          → transport_->poll(10) → events
          → for each event:
              ├── CONNECT:    初始化 recv_buffers_[conn_id]
              ├── DATA:       recv_buffers_[conn_id] += data
              │                 → dispatch_message() 循环解析
              ├── DISCONNECT:  回调 + 清理 buffer
              └── ERROR:       回调 + 清理
          → io_pool_->process_completions()  // 执行 IO 完成回调
      }
```

**Handler 注册机制**:

```cpp
reactor_->register_handler<RegisterMessage>(
    [this](uint64_t conn_id, const RegisterMessage& msg) { ... });
```

内部: handler 包装为 `GenericHandler` → 存入 `handlers_[MessageType]` 映射表。

**dispatch_message 流程**:

```
dispatch_message(buffer)
  → 读取 MessageType → 查 handlers_ 表
  → 找到 → MessageProtocol::decode<T>(buffer, msg)
  → decode 成功 → 调用 handler + buffer.erase 消费
  → 数据不足 → break 等待
  → 无 handler → 跳过，消费字节
```

**线程安全**: `reactor_->send()` 可在 Reactor 线程外调用。Linux `::send()` 对小帧是原子的，且只有 Reactor 线程 poll。

---

### IOThreadPool（通用线程池）

```cpp
class IOThreadPool {
public:
    using Task = std::function<void()>;
    using Completion = std::function<void()>;

    void start(int num_threads = 1);
    void stop();

    void submit(Task task, Completion completion);
    void process_completions();  // 在调用线程执行已完成的 completion
};
```

**使用模式**: Reactor 通过 `set_io_pool()` 持有引用，在 `run()` 循环中每轮调用 `process_completions()`，确保 completion 在 Reactor 线程执行。

**设计**: task 在工作线程执行（文件 I/O 等），completion 在 Reactor 线程执行（发送响应等），避免跨线程 transport 访问。

---

### DataClient（阻塞数据客户端）

```cpp
class DataClient {
public:
    static DataResponse request_data(const CMString& host, int32_t port,
                                      const CMString& object_name,
                                      int timeout_ms = 5000);
};
```

**设计特点**:
- 每次调用创建**独立阻塞 TCP socket**，完全不走主 Reactor
- 避免多线程并发读数据时的连接冲突
- 内置超时控制 (SO_SNDTIMEO + SO_RCVTIMEO + deadline)
- 消息帧收发: encode 发送 → 手动解析帧头 → decode 接收

---

### MetadataClient（阻塞元数据客户端）

```cpp
class MetadataClient {
public:
    struct DataLocation {
        bool found = false;
        uint64_t worker_id = 0;
        CMString host;
        int32_t port = 0;
        CMString error;
    };

    static DataLocation query_data_location(
        const CMString& master_host,
        int master_port,
        const CMString& object_name,
        int timeout_ms = 5000);
};
```

**设计特点**:
- 与 DataClient 类似，每次调用创建**独立阻塞 TCP socket**
- 功能：向 Master 查询数据对象的位置信息（哪个 Worker 持有）
- Worker 在三层降级读取的 Layer 3 使用：本地无 → 远程索引无 → 查 Master → MetadataClient → 直连 Worker
- 原名 `MasterClient`，因职责为元数据查询而非 Master 管理，更名为 `MetadataClient`

**位置**: `src/network/cpp/metadata_client.h/cpp`

---

### 消息类型定义（message_types.h）

22 种消息类型，每种定义对应的结构体，均支持 `FLY_SERIALIZE` 序列化。

**核心消息结构体示例**:

```cpp
struct RegisterMessage {
    MessageHeader header;
    uint64_t worker_id;
    CMString role;
    CMVector<CMString> attributes;
    CMString data_server_host;
    int32_t data_server_port;
    FLY_SERIALIZE(header, worker_id, role, attributes, data_server_host, data_server_port);
};

struct TaskSubmitMessage {
    MessageHeader header;
    uint64_t task_id;
    CMString task_name;
    CMString task_module;
    CMVector<CMString> args;
    CMVector<CMString> inputs;
    FLY_SERIALIZE(header, task_id, task_name, task_module, args, inputs);
};

struct TaskCompleteMessage {
    MessageHeader header;
    uint64_t task_id;
    uint64_t worker_id;
    CMVector<CMString> written_objects;
    CMVector<CMString> frozen_dbs;
    FLY_SERIALIZE(header, task_id, worker_id, written_objects, frozen_dbs);
};
```

---

## 核心流程

### Worker 注册

```
Worker.start()
  1. create_transport("tcp")
  2. transport->listen("0.0.0.0", 0)           // Data Server, 随机端口
  3. master_conn_ = transport->connect(master_host, master_port)
  4. reactor_ = new Reactor(transport)          // 共用一个 Reactor
  5. reactor_->send(master_conn_, RegisterMessage{...})

Master.ReactorThread
  → on_worker_register(conn_id, msg)
    → conn_to_worker_[conn_id] = worker_id
    → worker_manager_->register_worker(...)
    → DataService.register_worker(worker_id, host, port)
    → reactor_->send(conn_id, RegisterAckMessage{...})
```

### 心跳

```
Worker.heartbeat_thread_ (CV-based, 10s 间隔)
  → HeartbeatMessage → reactor_->send(master_conn_, ...)

Master.heartbeat_check_thread_ (CV-based, 5s 间隔)
  → heartbeat_monitor_->check_all_workers()
  → get_dead_workers() → 超时 Worker → ShutdownMessage
```

---

## 设计决策

| 决策 | 原因 |
|------|------|
| epoll 边缘触发 | 减少系统调用次数，高效通知 |
| 单 Reactor 线程 | 避免 IO 多线程的锁竞争，handler 无锁 |
| per-conn 拼接缓冲 | TCP 粘包/拆包安全处理 |
| IOThreadPool completion pattern | 文件 I/O 不阻塞 Reactor，回调线程安全 |
| DataClient 独立 socket | 多线程读无冲突，不走主 Reactor |
| 工厂函数 create_transport | 支持未来替换 UDP/RDMA |
| CV-based 心跳 | stop() 时可立即唤醒，无阻塞 |
