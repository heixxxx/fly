# Fly Layer 2: Networking/Transport 设计

## 一、概述

### 项目目标

实现分布式任务框架的通信层，为 Master/Worker 提供可靠的消息传递机制：
- TCP Socket 为默认传输方式（通过 TransportLayer 抽象，支持未来替换为 UDP/RDMA）
- 单线程 Reactor 事件循环处理轻量消息
- 独立线程池处理重 I/O 任务（文件传输）
- Length-prefix 消息帧协议，复用 FLY_ENCODE/FLY_DECODE 序列化

### 技术栈

- **C++20**: 核心 I/O 实现（POSIX socket + epoll/kqueue）
- **nanobind**: Python 绑定（通过 FLY_EXPORT_* 宏）
- **bitsery**: 序列化（通过 FLY_SERIALIZE 宏）
- **gtest**: C++ 单元测试
- **pytest**: Python 集成测试
- **Bazel**: 构建系统

### 设计原则

1. **抽象接口**: TransportLayer 抽象层，底层实现可替换
2. **宏封装**: 消息结构使用 FLY_SERIALIZE，序列化后端可替换
3. **非阻塞 I/O**: poll() 返回事件，send/recv 立即返回
4. **事件驱动**: Reactor 单线程事件循环，确定性执行
5. **线程池隔离**: 重 I/O 任务提交到 IOThreadPool，不阻塞 Reactor

---

## 二、架构概览

### 2.1 组件分层

```
┌─────────────────────────────────────────────┐
│  MasterAgent / WorkerAgent (Layer 4)        │
│  - 业务逻辑                                  │
│  - 消息处理器                                 │
└────────────────────────┬────────────────────┘
                         │
┌────────────────────────▼────────────────────┐
│  Reactor (事件循环)                          │
│  - 单线程事件分发                             │
│  - 调用 TransportLayer::poll()              │
│  - 触发消息处理器                             │
└────────────────────────┬────────────────────┘
                         │
┌────────────────────────▼────────────────────┐
│  TransportLayer (抽象接口)                   │
│  TCPTransport 实现此接口                      │
│  - Non-blocking poll() 返回事件              │
│  - send/recv 为 fire-and-return             │
└────────────────────────┬────────────────────┘
                         │
┌────────────────────────▼────────────────────┐
│  MessageProtocol                            │
│  - Length-prefix 帧格式                      │
│  - 使用 FLY_ENCODE/FLY_DECODE               │
└─────────────────────────────────────────────┘

┌─────────────────────────────────────────────┐
│  IOThreadPool (并行)                         │
│  - 重 I/O: 文件传输、大数据读取               │
│  - 大小: Config::data_server_threads         │
│  - 异步提交，完成后回调                       │
└─────────────────────────────────────────────┘
```

### 2.2 执行路径

**消息路径（单线程，确定性）:**
```
Reactor 事件循环 → TransportLayer::poll() → 解码 → 处理器 → 编码 → send()
```

**重 I/O 路径（线程池，非阻塞）:**
```
处理器提交任务 → IOThreadPool → 异步执行 → 完成回调 → Reactor 分发
```

### 2.3 核心组件

| 组件 | 职责 | 关键接口 |
|------|------|----------|
| TransportLayer | 抽象 I/O 接口 | `listen()`, `connect()`, `poll()`, `send()`, `recv()` |
| TCPTransport | POSIX socket 实现 | 实现 TransportLayer，使用 epoll/kqueue |
| MessageProtocol | 二进制帧协议 | `encode()`, `decode()` |
| Reactor | 单线程事件循环 | `register_handler()`, `run()`, `stop()` |
| IOThreadPool | 重 I/O 线程池 | `submit()`, `process_completions()` |
| MessageTypes | 所有消息结构 | 使用 FLY_SERIALIZE 宏 |

---

## 三、TransportLayer 抽象接口

### 3.1 接口定义

```cpp
// src/network/cpp/transport.h
#pragma once

#include <common/cpp/common_types.h>
#include <cstdint>
#include <memory>

// 传输事件类型
enum class TransportEventType {
    CONNECT,      // 新连接建立
    DATA,         // 数据接收
    DISCONNECT,   // 连接关闭
    ERROR         // 连接错误
};

// 传输事件（poll() 返回）
struct TransportEvent {
    TransportEventType type;
    uint64_t conn_id;       // 连接标识符
    CMString data;          // 数据缓冲（DATA 事件）
    int error_code;         // 错误码（ERROR 事件）
    
    FLY_SERIALIZE(type, conn_id, data, error_code);
};

// 抽象传输接口 - 实现可替换
class TransportLayer {
public:
    virtual ~TransportLayer() = default;
    
    // 服务端操作
    virtual void listen(const CMString& address, int port) = 0;
    virtual void stop_listening() = 0;
    
    // 客户端操作
    virtual uint64_t connect(const CMString& address, int port) = 0;
    
    // 非阻塞 I/O
    virtual ssize_t send(uint64_t conn_id, const CMString& data) = 0;
    virtual ssize_t recv(uint64_t conn_id, CMString& buffer, size_t max_size) = 0;
    
    // 事件轮询（Reactor 核心）
    virtual CMVector<TransportEvent> poll(int timeout_ms) = 0;
    
    // 连接管理
    virtual void close(uint64_t conn_id) = 0;
    virtual void close_all() = 0;
    virtual bool is_connected(uint64_t conn_id) const = 0;
    
    // 统计
    virtual size_t connection_count() const = 0;
};

// 工厂函数 - 根据配置创建实现
std::unique_ptr<TransportLayer> create_transport(const CMString& type);
```

### 3.2 设计要点

1. **poll() 是核心方法** - 返回事件向量，集成 Reactor 事件循环
2. **非阻塞语义** - send/recv 立即返回处理字节数（或 -1 表示 would-block）
3. **连接 ID** - 64 位标识符，非 socket fd（抽象实现细节）
4. **工厂模式** - `create_transport("tcp")` 返回 TCPTransport；未来 `create_transport("rdma")` 返回 RDMA 实现

---

## 四、TCPTransport 实现

### 4.1 类定义

```cpp
// src/network/cpp/tcp_transport.h
#pragma once

#include "transport.h"
#include <unordered_map>
#include <sys/epoll.h>  // Linux; macOS 使用 kqueue

class TCPTransport : public TransportLayer {
public:
    TCPTransport();
    ~TCPTransport() override;
    
    // 实现所有 TransportLayer 方法
    void listen(const CMString& address, int port) override;
    void stop_listening() override;
    uint64_t connect(const CMString& address, int port) override;
    ssize_t send(uint64_t conn_id, const CMString& data) override;
    ssize_t recv(uint64_t conn_id, CMString& buffer, size_t max_size) override;
    CMVector<TransportEvent> poll(int timeout_ms) override;
    void close(uint64_t conn_id) override;
    void close_all() override;
    bool is_connected(uint64_t conn_id) const override;
    size_t connection_count() const override;

private:
    int epoll_fd_;                          // epoll 实例
    int listen_fd_;                         // 监听 socket（-1 表示未监听）
    uint64_t next_conn_id_;                 // 连接 ID 生成器
    
    CMUnorderedMap<uint64_t, int> conn_to_fd_;    // conn_id -> socket fd
    CMUnorderedMap<int, uint64_t> fd_to_conn_;    // socket fd -> conn_id
    
    CMUnorderedMap<uint64_t, CMString> recv_buffers_;  // 连接级接收缓冲
    
    uint64_t register_connection(int fd);
    void unregister_connection(uint64_t conn_id);
    void set_nonblocking(int fd);
    CMString drain_socket(int fd, size_t max_size);
};
```

### 4.2 实现细节

1. **epoll 事件多路复用** - `epoll_wait()` 返回就绪 socket，转换为 `TransportEvent` 向量
2. **连接 ID 映射** - 内部 fd ↔ conn_id 映射，隐藏 POSIX 细节
3. **非阻塞 socket** - 所有 socket 创建后设置 `O_NONBLOCK`
4. **部分读取处理** - `recv_buffers_` 累积不完整数据，下次 poll 继续读取

### 4.3 poll() 实现

```cpp
CMVector<TransportEvent> TCPTransport::poll(int timeout_ms) {
    CMVector<TransportEvent> events;
    
    struct epoll_event evs[64];
    int n = epoll_wait(epoll_fd_, evs, 64, timeout_ms);
    
    for (int i = 0; i < n; i++) {
        int fd = evs[i].data.fd;
        
        if (fd == listen_fd_) {
            // 新连接
            int client_fd = accept(listen_fd_, nullptr, nullptr);
            set_nonblocking(client_fd);
            uint64_t conn_id = register_connection(client_fd);
            
            TransportEvent ev;
            ev.type = TransportEventType::CONNECT;
            ev.conn_id = conn_id;
            events.push_back(ev);
        } else {
            uint64_t conn_id = fd_to_conn_[fd];
            
            if (evs[i].events & EPOLLIN) {
                // 数据接收
                CMString data = drain_socket(fd, 65536);
                if (data.empty()) {
                    // 连接关闭
                    TransportEvent ev;
                    ev.type = TransportEventType::DISCONNECT;
                    ev.conn_id = conn_id;
                    events.push_back(ev);
                    close(conn_id);
                } else {
                    TransportEvent ev;
                    ev.type = TransportEventType::DATA;
                    ev.conn_id = conn_id;
                    ev.data = data;
                    events.push_back(ev);
                }
            }
            
            if (evs[i].events & EPOLLERR) {
                // 错误
                TransportEvent ev;
                ev.type = TransportEventType::ERROR;
                ev.conn_id = conn_id;
                ev.error_code = errno;
                events.push_back(ev);
                close(conn_id);
            }
        }
    }
    
    return events;
}
```

---

## 五、MessageProtocol 帧协议

### 5.1 帧格式

**Length-prefix framing:**
```
[长度 (4 bytes, network byte order)] [payload (N bytes)]
```

- 长度字段：4 字节大端序（网络字节序），表示 payload 字节数
- Payload：FLY_ENCODE 序列化的消息结构体

### 5.2 接口定义

```cpp
// src/network/cpp/message_protocol.h
#pragma once

#include "transport.h"
#include <cstdint>

class MessageProtocol {
public:
    // 编码任意可序列化结构到 wire format
    template<typename T>
    static CMString encode(const T& msg) {
        CMString payload;
        FLY_ENCODE(msg, payload);
        
        uint32_t len = static_cast<uint32_t>(payload.size());
        CMString frame;
        frame.resize(4 + payload.size());
        
        // 写入长度（网络字节序）
        frame[0] = static_cast<char>((len >> 24) & 0xFF);
        frame[1] = static_cast<char>((len >> 16) & 0xFF);
        frame[2] = static_cast<char>((len >> 8) & 0xFF);
        frame[3] = static_cast<char>(len & 0xFF);
        
        std::copy(payload.begin(), payload.end(), frame.begin() + 4);
        return frame;
    }
    
    // 解码 - 返回 true 表示完整消息可用
    template<typename T>
    static bool decode(CMString& buffer, T& msg) {
        if (buffer.size() < 4) return false;
        
        uint32_t len = 
            (static_cast<uint32_t>(buffer[0]) << 24) |
            (static_cast<uint32_t>(buffer[1]) << 16) |
            (static_cast<uint32_t>(buffer[2]) << 8) |
            static_cast<uint32_t>(buffer[3]);
        
        if (buffer.size() < 4 + len) return false;
        
        CMString payload(buffer.substr(4, len));
        buffer = buffer.substr(4 + len);  // 消费帧
        
        FLY_DECODE(payload, T, msg);
        return true;
    }
    
    // 从 header 提取消息类型（用于分发）
    static MessageType get_type(const CMString& buffer);
};
```

### 5.3 设计要点

1. **Length-prefix framing** - 4 字节大端序长度 + 序列化 payload（标准 TCP 协议模式）
2. **部分缓冲** - decode() 返回 false 表示不完整，buffer 保留供下次 recv
3. **模板化** - 支持任意使用 FLY_SERIALIZE 的结构体
4. **网络字节序** - 大端序确保跨架构兼容

---

## 六、Reactor 事件循环

### 6.1 类定义

```cpp
// src/network/cpp/reactor.h
#pragma once

#include "transport.h"
#include "message_protocol.h"
#include <functional>
#include <unordered_map>
#include <atomic>

// 消息处理器回调类型
template<typename T>
using MessageHandler = std::function<void(uint64_t conn_id, const T& msg)>;

// 通用处理器（未知消息类型）
using GenericHandler = std::function<void(uint64_t conn_id, CMString& raw_msg)>;

class Reactor {
public:
    explicit Reactor(std::unique_ptr<TransportLayer> transport);
    ~Reactor();
    
    // 注册类型化处理器
    template<typename T>
    void register_handler(MessageHandler<T> handler);
    
    // 连接事件处理器
    void on_connect(std::function<void(uint64_t)> handler);
    void on_disconnect(std::function<void(uint64_t)> handler);
    void on_error(std::function<void(uint64_t, int)> handler);
    
    // 事件循环控制
    void run();                         // 阻塞循环
    void run_once(int timeout_ms = 100); // 单次迭代
    void stop();                        // 信号停止（atomic）
    
    // 发送消息（fire-and-return，非阻塞）
    template<typename T>
    void send(uint64_t conn_id, const T& msg);
    
    // IOThreadPool 集成
    void set_io_pool(std::shared_ptr<IOThreadPool> pool);

private:
    std::unique_ptr<TransportLayer> transport_;
    std::shared_ptr<IOThreadPool> io_pool_;
    
    // 连接级接收缓冲（部分消息）
    CMUnorderedMap<uint64_t, CMString> recv_buffers_;
    
    // 消息分发（type -> handler）
    CMUnorderedMap<MessageType, GenericHandler> handlers_;
    
    // 连接生命周期处理器
    std::function<void(uint64_t)> connect_handler_;
    std::function<void(uint64_t)> disconnect_handler_;
    std::function<void(uint64_t, int)> error_handler_;
    
    std::atomic<bool> running_{false};
    
    void handle_event(const TransportEvent& event);
    void dispatch_message(uint64_t conn_id, CMString& buffer);
};
```

### 6.2 事件循环流程

```cpp
void Reactor::run() {
    running_ = true;
    while (running_) {
        run_once(100);
        if (io_pool_) {
            io_pool_->process_completions();  // 处理线程池完成回调
        }
    }
}

void Reactor::run_once(int timeout_ms) {
    auto events = transport_->poll(timeout_ms);
    for (const auto& event : events) {
        handle_event(event);
    }
}

void Reactor::handle_event(const TransportEvent& event) {
    switch (event.type) {
        case TransportEventType::CONNECT:
            recv_buffers_[event.conn_id] = "";
            if (connect_handler_) connect_handler_(event.conn_id);
            break;
            
        case TransportEventType::DATA:
            recv_buffers_[event.conn_id] += event.data;
            dispatch_message(event.conn_id, recv_buffers_[event.conn_id]);
            break;
            
        case TransportEventType::DISCONNECT:
            if (disconnect_handler_) disconnect_handler_(event.conn_id);
            recv_buffers_.erase(event.conn_id);
            break;
            
        case TransportEventType::ERROR:
            if (error_handler_) error_handler_(event.conn_id, event.error_code);
            recv_buffers_.erase(event.conn_id);
            break;
    }
}

void Reactor::dispatch_message(uint64_t conn_id, CMString& buffer) {
    while (true) {
        MessageType type = MessageProtocol::get_type(buffer);
        if (handlers_.find(type) == handlers_.end()) break;  // 未注册类型
        
        auto& handler = handlers_[type];
        CMString temp = buffer;  // 保留原 buffer
        
        // 尝试解码
        // 注意：这里需要类型特化的解码，handler 内部处理
        // 实际实现中，handler 会收到已解码的消息
        handler(conn_id, buffer);
        
        if (buffer == temp) break;  // 未消费，不完整消息
    }
}

template<typename T>
void Reactor::send(uint64_t conn_id, const T& msg) {
    CMString frame = MessageProtocol::encode(msg);
    transport_->send(conn_id, frame);
}
```

### 6.3 设计要点

1. **模板化处理器** - 类型安全的消息分发，无运行时 cast
2. **部分缓冲** - recv_buffers_ 存储每连接的不完整帧
3. **非阻塞 send** - send() 调用 transport_->send()，立即返回
4. **IOThreadPool 集成** - 处理器可提交重 I/O 到线程池，完成回调分发回 Reactor

---

## 七、IOThreadPool 重 I/O 线程池

### 7.1 类定义

```cpp
// src/network/cpp/io_thread_pool.h
#pragma once

#include <common/cpp/common_types.h>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>
#include <memory>

using IOTask = std::function<void()>;
using CompletionCallback = std::function<void()>;

class IOThreadPool {
public:
    explicit IOThreadPool(int thread_count);
    ~IOThreadPool();
    
    // 提交 I/O 任务 + 完成回调
    void submit(IOTask task, CompletionCallback completion = nullptr);
    
    // 处理完成回调（从 Reactor 线程调用）
    void process_completions();
    
    // 控制
    void start();
    void stop();
    
    // 统计
    int queue_size() const;
    bool is_idle() const;

private:
    int thread_count_;
    CMVector<std::thread> workers_;
    
    std::queue<std::pair<IOTask, CompletionCallback>> tasks_;
    std::mutex tasks_mutex_;
    std::condition_variable tasks_cv_;
    
    CMVector<CompletionCallback> completions_;
    std::mutex completions_mutex_;
    
    std::atomic<bool> running_{false};
    std::atomic<int> active_tasks_{0};
    
    void worker_loop();
};
```

### 7.2 集成模式

```
消息处理器（Reactor 线程）:
├─ 收到 DataRequestMessage（worker 请求文件）
├─ submit_io_task(
│     task = [file_path] { read_large_file(file_path); },
│     completion = [conn_id, reactor] { reactor->send(conn_id, DataResponseMessage); }
│   )
└─ 立即返回（非阻塞）

Worker 线程:
├─ 读取文件（重 I/O，可能耗时数秒）
├─ 存储结果
└─ 推送完成回调到 completions 队列

Reactor 下次迭代:
├─ io_pool_->process_completions()
├─ 执行完成回调（Reactor 线程，可安全 send()）
└─ DataResponseMessage 发送至请求方
```

### 7.3 实现细节

```cpp
void IOThreadPool::submit(IOTask task, CompletionCallback completion) {
    {
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        tasks_.emplace(task, completion);
    }
    tasks_cv_.notify_one();
    active_tasks_++;
}

void IOThreadPool::worker_loop() {
    while (running_) {
        std::pair<IOTask, CompletionCallback> item;
        {
            std::unique_lock<std::mutex> lock(tasks_mutex_);
            tasks_cv_.wait(lock, [this] { return !tasks_.empty() || !running_; });
            if (!running_ && tasks_.empty()) return;
            item = tasks_.front();
            tasks_.pop();
        }
        
        // 执行任务
        item.first();
        active_tasks_--;
        
        // 推送完成回调
        if (item.second) {
            std::lock_guard<std::mutex> lock(completions_mutex_);
            completions_.push_back(item.second);
        }
    }
}

void IOThreadPool::process_completions() {
    CMVector<CompletionCallback> to_process;
    {
        std::lock_guard<std::mutex> lock(completions_mutex_);
        to_process = std::move(completions_);
        completions_.clear();
    }
    
    for (auto& cb : to_process) {
        cb();  // 在 Reactor 线程执行
    }
}
```

### 7.4 Config 参数

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `data_server_threads` | 1 | IOThreadPool 线程数 |

---

## 八、MessageTypes 消息类型

### 8.1 消息类型枚举

```cpp
// src/network/cpp/message_types.h
#pragma once

#include <common/cpp/common_types.h>
#include <cstdint>

enum class MessageType : uint8_t {
    REGISTER = 1,
    REGISTER_ACK = 2,
    HEARTBEAT = 3,
    TASK_SUBMIT = 4,
    TASK_ASSIGN = 5,
    TASK_COMPLETE = 6,
    TASK_FAILED = 7,
    DATA_READY = 8,
    DATA_QUERY = 9,
    DATA_LOCATION = 10,
    DATA_REQUEST = 11,
    DATA_RESPONSE = 12,
    SHUTDOWN = 13,
    DATABASE_FREEZE = 14,
    IDX_REQUEST = 15,
    IDX_RESPONSE = 16,
    CLEANUP_TASK = 17,
    CLEANUP_COMPLETE = 18,
};
```

### 8.2 消息头结构

```cpp
struct MessageHeader {
    MessageType type;
    uint32_t message_id;      // 消息唯一 ID
    uint64_t timestamp;       // 发送时间戳
    
    FLY_SERIALIZE(type, message_id, timestamp);
};
```

### 8.3 核心消息示例

```cpp
// Worker → Master: 注册
struct RegisterMessage {
    MessageHeader header;
    uint64_t worker_id;
    CMString role;                       // "hybrid" | "storage_only"
    CMVector<CMString> attributes;
    
    static constexpr MessageType msg_type = MessageType::REGISTER;
    
    FLY_SERIALIZE(header, worker_id, role, attributes);
};

// Master → Worker: 注册确认
struct RegisterAckMessage {
    MessageHeader header;
    uint64_t worker_id;
    CMString master_address;
    int master_port;
    
    static constexpr MessageType msg_type = MessageType::REGISTER_ACK;
    
    FLY_SERIALIZE(header, worker_id, master_address, master_port);
};

// Worker → Master: 心跳
struct HeartbeatMessage {
    MessageHeader header;
    uint64_t worker_id;
    CMVector<uint64_t> running_tasks;
    CMVector<CMString> attributes;
    
    static constexpr MessageType msg_type = MessageType::HEARTBEAT;
    
    FLY_SERIALIZE(header, worker_id, running_tasks, attributes);
};

// Worker → Worker: 数据请求（重 I/O）
struct DataRequestMessage {
    MessageHeader header;
    CMString object_name;
    uint64_t requesting_worker_id;
    
    static constexpr MessageType msg_type = MessageType::DATA_REQUEST;
    
    FLY_SERIALIZE(header, object_name, requesting_worker_id);
};

// Worker → Worker: 数据响应（可能较大）
struct DataResponseMessage {
    MessageHeader header;
    CMString object_name;
    CMString data;  // 二进制 payload（可能 MB 级）
    
    static constexpr MessageType msg_type = MessageType::DATA_RESPONSE;
    
    FLY_SERIALIZE(header, object_name, data);
};
```

### 8.4 设计要点

1. **MessageType enum** - 8 位类型 ID，用于分发
2. **static constexpr msg_type** - 模板化处理器标识符
3. **MessageHeader 共用** - 所有消息继承 header 结构
4. **FLY_SERIALIZE** - 使用现有序列化宏（bitsery 后端）
5. **DataResponseMessage** - 可携带大 payload（重 I/O 场景）

---

## 九、Config 网络参数

### 9.1 新增参数

```cpp
// src/core/cpp/config.h additions

static const CMMap<CMString, int64_t> NETWORK_DEFAULTS = {
    {"transport_type", 0},           // 0=TCP, 1=UDP (future), 2=RDMA (future)
    {"data_server_threads", 1},      // IOThreadPool 线程数
    {"master_port", 8000},           // Master 监听端口
    {"poll_timeout_ms", 100},        // Reactor poll 超时
    {"max_message_size", 1048576},   // 1MB（更大走文件传输）
    {"use_tls", 0},                  // 0=plain, 1=TLS (future Layer 5+)
};
```

---

## 十、文件结构

```
src/network/
├── cpp/
│   ├── transport.h              # TransportLayer 抽象接口
│   ├── tcp_transport.h          # TCPTransport 声明
│   ├── tcp_transport.cpp        # TCPTransport 实现（epoll）
│   ├── message_protocol.h       # Length-prefix 帧协议
│   ├── reactor.h                # Reactor 声明
│   ├── reactor.cpp              # Reactor 实现
│   ├── io_thread_pool.h         # IOThreadPool 声明
│   ├── io_thread_pool.cpp       # IOThreadPool 实现
│   ├── message_types.h          # 所有消息结构
│   └── BUILD                    # cc_library targets
├── export/
│   ├── network_export.cpp       # nanobind 导出（Reactor, Transport）
│   └── BUILD                    # cc_binary: _fly_network.so
├── py/
│   ├── __init__.py              # from _fly_network import *
│   └── BUILD                    # py_library
└── tests/
    ├── tcp_transport_test.cpp   # TCPTransport gtest
    ├── message_protocol_test.cpp # MessageProtocol gtest
    ├── reactor_test.cpp         # Reactor gtest
    ├── io_thread_pool_test.cpp  # IOThreadPool gtest
    ├── network_test.py          # pytest 集成测试
    └── BUILD                    # cc_test targets
```

---

## 十一、测试策略

### 11.1 每组件测试

| 组件 | 测试文件 | 关键测试 |
|------|----------|----------|
| TCPTransport | `tcp_transport_test.cpp` | listen/connect, poll, send/recv, partial read |
| MessageProtocol | `message_protocol_test.cpp` | encode/decode, partial buffer, 各种大小 |
| Reactor | `reactor_test.cpp` | handler 注册, 事件分发, send 集成 |
| IOThreadPool | `io_thread_pool_test.cpp` | submit, process_completions, 线程安全 |

### 11.2 测试示例

```cpp
TEST(TCPTransportTest, ListenAndConnect) {
    TCPTransport server, client;
    server.listen("127.0.0.1", 9999);
    uint64_t conn = client.connect("127.0.0.1", 9999);
    
    auto events = server.poll(100);
    EXPECT_EQ(events.size(), 1);
    EXPECT_EQ(events[0].type, TransportEventType::CONNECT);
}

TEST(MessageProtocolTest, EncodeDecodeRoundTrip) {
    RegisterMessage msg;
    msg.worker_id = 123;
    msg.role = "hybrid";
    
    CMString encoded = MessageProtocol::encode(msg);
    CMString buffer = encoded;
    
    RegisterMessage decoded;
    EXPECT_TRUE(MessageProtocol::decode(buffer, decoded));
    EXPECT_EQ(decoded.worker_id, 123);
    EXPECT_EQ(decoded.role, "hybrid");
}

TEST(ReactorTest, MessageDispatch) {
    auto transport = std::make_unique<TCPTransport>();
    Reactor reactor(std::move(transport));
    
    int received = 0;
    reactor.register_handler<HeartbeatMessage>([&](uint64_t conn, const auto& msg) {
        received++;
    });
    
    // 模拟接收...
    reactor.run_once();
    EXPECT_EQ(received, expected_count);
}

TEST(IOThreadPoolTest, AsyncExecution) {
    IOThreadPool pool(2);
    pool.start();
    
    int done = 0;
    pool.submit(
        [] { sleep(1); },  // 模拟重 I/O
        [&] { done++; }
    );
    
    sleep(2);
    pool.process_completions();
    EXPECT_EQ(done, 1);
    
    pool.stop();
}
```

---

## 十二、依赖关系

```
TransportLayer (抽象)
    ↓
TCPTransport (实现)
    ↓
MessageProtocol (帧协议)
    ↓
Reactor (事件循环) ← IOThreadPool (重 I/O)
    ↓
MessageTypes (消息结构)
    ↓
network_export.cpp (nanobind)
```

---

## 十三、实现顺序

1. **Task 1**: TransportLayer 抽象接口 + TCPTransport 实现
2. **Task 2**: MessageProtocol 帧协议
3. **Task 3**: MessageTypes 核心消息结构
4. **Task 4**: Reactor 事件循环
5. **Task 5**: IOThreadPool 线程池
6. **Task 6**: Config 网络参数
7. **Task 7**: Python 导出（nanobind）
8. **Task 8**: pytest 集成测试

---

## 十四、后续扩展（Layer 5+）

1. **TLS 支持**: `create_transport("tls")` 返回 TLSTransport（OpenSSL/mbedTLS）
2. **UDP 实现**: `create_transport("udp")` 用于低延迟场景
3. **RDMA 实现**: `create_transport("rdma")` 用于高性能计算集群
4. **消息压缩**: 大消息自动压缩（复用 Layer 1 Compressor）

---

**文档创建时间**: 2026-05-15
**前置条件**: Layer 1 Storage 完成并通过所有测试
**预计实现时间**: 3-4 天