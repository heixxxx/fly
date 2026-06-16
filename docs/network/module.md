# Network 模块 — 网络层

## 模块概述

**位置**: `src/network/`

网络层提供基于 TCP (epoll) 的异步事件驱动通信框架，包括传输抽象、消息协议、Reactor 事件循环、IO 线程池和阻塞数据客户端。

---

## 核心文件

| 文件 | 说明 |
|------|------|
| `cpp/transport_interface.h` | Transport 抽象接口（socket 操作） |
| `cpp/tcp_socket.h/cpp` | TCPSocketTransport — POSIX TCP 实现 |
| `cpp/epoll_multiplexer.h/cpp` | EpollMultiplexer 抽象接口 + 实现（事件复用） |
| `cpp/connection_manager.h` | ConnectionManager 抽象接口（conn_id 管理 + 事件分发） |
| `cpp/tcp_connection_manager.h/cpp` | TcpConnectionManager — 基于 Transport+EpollMultiplexer |
| `cpp/reactor.h/cpp` | 单线程事件循环 |
| `cpp/message_protocol.h/cpp` | 二进制帧协议 |
| `cpp/message_types.h` | 33 种消息结构定义（含 MessageHeader） |
| `cpp/io_thread_pool.h/cpp` | 通用线程池（submit + completion 回调） |
| `cpp/metadata_client.h/cpp` | 阻塞 TCP 元数据查询客户端（原名 MasterClient） |
| `cpp/data_client.h/cpp` | 阻塞 TCP 数据客户端 |
| `cpp/data_client_pool.h/cpp` | 数据客户端连接池（并发请求控制） |
| `export/network_export.cpp` | Python 导出 |

---

## 类详细说明

### Transport（Socket 操作抽象）

薄包装层，封装 POSIX socket 操作。不含事件模型、不含消息协议。

```cpp
class Transport {
public:
    virtual int create_listen_socket(const CMString& host, int port) = 0;
    virtual int accept_connection(int listen_fd) = 0;
    virtual int create_connection(const CMString& host, int port) = 0;

    virtual void set_nodelay(int fd) = 0;
    virtual void set_nonblocking(int fd) = 0;
    virtual void set_recv_timeout(int fd, int timeout_ms) = 0;
    virtual void set_send_timeout(int fd, int timeout_ms) = 0;

    virtual ssize_t send(int fd, const char* data, size_t len) = 0;
    virtual ssize_t recv(int fd, char* buf, size_t len) = 0;
    virtual bool send_all(int fd, const char* data, size_t len) = 0;
    virtual int get_port(int fd) = 0;
    virtual void close(int fd) = 0;
};
```

**实现**: `TCPSocketTransport`（`tcp_socket.h/cpp`），工厂函数 `create_tcp_transport()`。

### EpollMultiplexer（事件复用抽象）

封装 epoll 操作，头文件零 `<sys/epoll.h>` 依赖。使用自有事件类型。

```cpp
struct IoEvent {
    int fd = -1;
    bool readable = false;
    bool writable = false;
    bool error = false;
    bool hangup = false;
};

class EpollMultiplexer {
public:
    virtual int create() = 0;
    virtual bool add(int epfd, int fd, uint32_t events) = 0;  // EV_READ | EV_ONESHOT
    virtual bool mod(int epfd, int fd, uint32_t events) = 0;
    virtual bool del(int epfd, int fd) = 0;
    virtual int wait(int epfd, IoEvent* events, int max_events, int timeout_ms) = 0;
    virtual void destroy(int epfd) = 0;
};
```

**事件标志**: `EV_READ`, `EV_WRITE`, `EV_ONESHOT`（定义于 `epoll_multiplexer.h`）

### ConnectionManager（连接管理抽象）

基于 Transport + EpollMultiplexer 构建，提供 conn_id 抽象 + 写缓冲 + 事件分发。

```cpp
class ConnectionManager {
public:
    virtual void listen(const CMString& address, int port) = 0;
    virtual uint64_t connect(const CMString& address, int port) = 0;
    virtual ssize_t send(uint64_t conn_id, const CMString& data) = 0;
    virtual CMVector<TransportEvent> poll(int timeout_ms) = 0;
    virtual void close(uint64_t conn_id) = 0;
    virtual int get_bound_port() const = 0;
};
```

**实现**: `TcpConnectionManager`（`tcp_connection_manager.h/cpp`），工厂函数 `create_connection_manager("tcp")`。

**关键特性**:
- 内部持有 Transport + EpollMultiplexer 实例，所有 socket/epoll 操作委托给它们
- conn_id 单调递增，双映射 `conn_to_fd_` / `fd_to_conn_` 管理
- 写缓冲: send() EAGAIN → 数据存入 `write_buffers_` → 注册 EPOLLOUT → drain
- `drain_socket` 循环 recv 直到 EAGAIN，单次最多 64KB

---

### MessageProtocol（帧协议）

**帧格式**（通用）:

```
┌──────────────┬──────────┬────────────────┐
│ 4 bytes      │ 1 byte   │ N bytes        │
│ total_len    │ msg_type │ payload        │
│ (big-endian) │ (uint8)  │ (bitsery 编码) │
└──────────────┴──────────┴────────────────┘

total_len = 1 + payload.size()
```

**DataResponseProtocol（两段帧，仅 DATA_RESPONSE）**:

DataResponseMessage 的大 payload（compressed_data_）不经 bitsery 序列化，
作为帧尾 raw 段独立传输，消除用户态 copy：

```
┌──────────────┬──────────┬─────────────────┬──────────┬──────────────────┬────────────────┐
│ 4 bytes      │ 1 byte   │ 4 bytes         │ 1 byte   │ small_fields_len │ raw_len        │
│ total_len    │ type=    │ small_fields_len│ has_raw  │ (bitsery 小字段)  │ (raw payload)  │
│ (big-endian) │ DATA_RESP│ (big-endian)    │ (uint8)  │                  │                │
└──────────────┴──────────┴─────────────────┴──────────┴──────────────────┴────────────────┘

total_len = 1 + 4 + 1 + small_fields_len + raw_len
raw_len = total_len - 6 - small_fields_len（接收侧推算）
```

发送侧：DataServer 用 `DataResponseProtocol::encode` 编码小字段段 + 直接引用
FlyBufferPtr 发送 raw 段（零用户态 copy）。

接收侧：DataClient/DataClientPool 分步 recv（header → sub-header → small_fields
→ raw 直接进 FlyBuffer），零拷贝。

**关键方法**:

| 方法 | 作用 |
|------|------|
| `MessageProtocol::encode<T>(msg)` | bitsery 序列化 → 拼接帧头 + payload（通用消息）|
| `MessageProtocol::decode<T>(buffer, msg)` | 校验长度+类型 → 反序列化 → 消费 buffer |
| `DataResponseProtocol::encode(msg, raw)` | 两段编码：bitsery 小字段 + raw 引用（零拷贝）|
| `DataResponseProtocol::decode_small_fields` | 解码小字段段（不含 raw）|
| `get_type(buffer)` | 读取第 5 字节得到 MessageType |
| `get_total_size(buffer)` | 读取前 4 字节大端序长度 |

**粘包处理**: `dispatch_message` 内 `while(!buffer.empty())` 循环解析。数据不足则等待更多数据。

---

### Reactor（事件循环）

```cpp
class Reactor {
public:
    explicit Reactor(std::unique_ptr<ConnectionManager> transport);
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

    // 非阻塞发送（线程安全，连接断开时不阻塞）
    bool try_send(uint64_t conn_id, const CMString& data);
    template<typename MsgT>
    bool try_send(uint64_t conn_id, const MsgT& msg);

    // IO 线程池
    void set_io_pool(IOThreadPool* pool);

private:
    std::unique_ptr<ConnectionManager> transport_;
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

> **优化**: `decode()` 已 in-place 修改 buffer（erase 已消费的前缀），`register_handler` 内无需额外 buffer 拷贝。

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
        bool found_ = false;
        uint64_t worker_id_ = 0;
        CMString host_;
        int32_t port_ = 0;
        CMString error_;
    };

    explicit MetadataClient(CMSharedPtr<Transport> transport);
    MetadataClient();

    DataLocation query_data_location(
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

33 种消息结构（含 MessageHeader），每种定义对应的结构体，均支持 `FLY_SERIALIZE` 序列化。

**核心消息结构体示例**:

```cpp
struct RegisterMessage {
    MessageHeader header;
    uint64_t worker_id;
    CMString role;
    CMVector<CMString> attributes;
    CMString data_server_host;
    int32_t data_server_port;
    CMString hostname;       // Worker 自行探测上报 (gethostname)
    CMString ip_address;     // Worker IP
    FLY_SERIALIZE(header, worker_id, role, attributes, data_server_host, data_server_port,
                  hostname, ip_address);
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

struct ObjectRemovedMessage {
    MessageHeader header;
    CMString object_name;   // "db_id:obj_name"
    CMString db_id;
    FLY_SERIALIZE(header, object_name, db_id);
};

struct IdxLoadCommandMessage {       // type=25, Master → Worker
    MessageHeader header;
    CMString db_id;
    CMString base_path;
    CMVector<CMString> writer_ids;
    FLY_SERIALIZE(header, db_id, base_path, writer_ids);
};

struct IdxLoadAckMessage {           // type=26, Worker → Master
    MessageHeader header;
    uint64_t worker_id;
    CMString db_id;
    bool success;
    int32_t loaded_count;
    CMString error_message;
    FLY_SERIALIZE(header, worker_id, db_id, success, loaded_count, error_message);
};

struct HeartbeatAckMessage {         // type=33, Master → Worker
    MessageHeader header;
    uint64_t worker_id = 0;
    FLY_SERIALIZE(header, worker_id);
};

struct DataRequestMessage {          // type=11, Worker → Worker（重 I/O）
    MessageHeader header;
    CMString object_name;
    uint64_t requesting_worker_id = 0;  // 请求方 worker_id（用于传输去重）
    uint64_t request_id = 0;            // 随机 ID（用于传输去重）
    FLY_SERIALIZE(header, object_name, requesting_worker_id, request_id);
};
```

---

## 核心流程

### Worker 注册

```
Worker.start()
  1. create_connection_manager("tcp")
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
   → HeartbeatMessage → reactor_->try_send(master_conn_, ...)
   → Master 收到 → HeartbeatAckMessage → Worker
   → Worker.on_heartbeat_ack() → touch_master_contact()

Master.heartbeat_check_thread_ (CV-based, 5s 间隔)
   → heartbeat_monitor_->check_all_workers()
   → get_dead_workers() → 超时 Worker → ShutdownMessage

Worker.master_liveness_check:
   → MASTER_TIMEOUT_SECONDS = 120 (12 次未收到 ACK)
   → 超时 → initiate_shutdown("master timeout")
```

**HeartbeatAck 设计要点**:
- Master 在 `on_heartbeat()` 中回复 `HeartbeatAckMessage`（type=33），携带 `worker_id`
- Worker 通过收到 ACK 判断 Master 存活，而非依赖任务分配
- `try_send()` 非阻塞发送，避免心跳线程阻塞在已断开的连接上
- 与 Worker→Master 心跳方向相反，形成双向存活检测

---

## 设计决策

| 决策 | 原因 |
|------|------|
| epoll 边缘触发 | 减少系统调用次数，高效通知 |
| 单 Reactor 线程 | 避免 IO 多线程的锁竞争，handler 无锁 |
| per-conn 拼接缓冲 | TCP 粘包/拆包安全处理 |
| IOThreadPool completion pattern | 文件 I/O 不阻塞 Reactor，回调线程安全 |
| DataClient 独立 socket | 多线程读无冲突，不走主 Reactor |
| 工厂函数 create_connection_manager | 支持未来替换 UDP/RDMA |
| CV-based 心跳 | stop() 时可立即唤醒，无阻塞 |
| HeartbeatAck 双向检测 | Worker 通过 ACK 检测 Master 存活，不依赖任务分配 |
| try_send() 非阻塞发送 | 心跳线程在连接断开时不阻塞，安全跳过 |
| DataRequestMessage 三元组去重 | (requesting_worker_id, object_name, request_id) 防止重复传输 |
