# Network 模块 — 网络层

## 模块概述

**位置**: `src/network/`

网络层提供基于 TCP (epoll) 的异步事件驱动通信框架，包括传输抽象、消息协议、Reactor 事件循环、IO 线程池和阻塞数据客户端。

---

## 核心组件

| 组件 | 文件 | 职责 |
|------|------|------|
| Transport | `cpp/transport_interface.h` | Socket 操作抽象 |
| TCPSocketTransport | `cpp/tcp_socket.h/cpp` | POSIX TCP 实现 |
| EpollMultiplexer | `cpp/epoll_multiplexer.h/cpp` | 事件复用抽象 |
| ConnectionManager | `cpp/connection_manager.h` | conn_id 管理 + 事件分发 |
| Reactor | `cpp/reactor.h/cpp` | 单线程事件循环 |
| MessageProtocol | `cpp/message_protocol.h/cpp` | 二进制帧协议 |
| IOThreadPool | `cpp/io_thread_pool.h/cpp` | 通用线程池 |
| DataClient | `cpp/data_client.h/cpp` | 阻塞 TCP 数据客户端 |
| DataClientPool | `cpp/data_client_pool.h/cpp` | 数据客户端连接池 |
| MetadataClient | `cpp/metadata_client.h/cpp` | 阻塞 TCP 元数据查询客户端 |

---

## Transport

### 核心职责

薄包装层，封装 POSIX socket 操作。不含事件模型、不含消息协议。

### 接口

- socket 创建/连接/监听
- 发送/接收（send/recv/send_all/sendv）
- socket 选项设置（nodelay/nonblocking/timeout）

---

## EpollMultiplexer

### 核心职责

封装 epoll 操作，头文件零 `<sys/epoll.h>` 依赖。使用自有事件类型（EV_READ/EV_WRITE/EV_ONESHOT）。

---

## ConnectionManager

### 核心职责

基于 Transport + EpollMultiplexer 构建，提供 conn_id 抽象 + 写缓冲 + 事件分发。

### 关键特性

- conn_id 单调递增，双映射 `conn_to_fd_` / `fd_to_conn_` 管理
- 写缓冲: send() EAGAIN → 数据存入 write_buffers_ → 注册 EPOLLOUT → drain
- drain_socket 循环 recv 直到 EAGAIN，单次最多 64KB

---

## MessageProtocol

### 帧格式

**通用消息**:
```
┌──────────────┬──────────┬────────────────┐
│ 4 bytes      │ 1 byte   │ N bytes        │
│ total_len    │ msg_type │ payload        │
│ (big-endian) │ (uint8)  │ (bitsery 编码) │
└──────────────┴──────────┴────────────────┘
```

**DataResponse（两段帧，仅 DATA_RESPONSE）**:

DataResponseMessage 的大 payload 不经 bitsery 序列化，作为帧尾 raw 段独立传输，消除用户态 copy：

```
┌──────────────┬──────────┬─────────────────┬──────────┬──────────────────┬────────────────┐
│ 4 bytes      │ 1 byte   │ 4 bytes         │ 1 byte   │ small_fields_len │ raw_len        │
│ total_len    │ type=    │ small_fields_len│ has_raw  │ (bitsery 小字段)  │ (raw payload)  │
│ (big-endian) │ DATA_RESP│ (big-endian)    │ (uint8)  │                  │                │
└──────────────┴──────────┴─────────────────┴──────────┴──────────────────┴────────────────┘
```

发送侧：DataServer 用 encode 编码小字段段 + 直接引用 FlyBufferPtr 发送 raw 段（零用户态 copy）。

接收侧：DataClient/DataClientPool 分步 recv（header → sub-header → small_fields → raw 直接进 FlyBuffer），零拷贝。

### 粘包处理

dispatch_message 内 while 循环解析。数据不足则等待更多数据。

---

## Reactor

### 核心职责

单线程事件循环，管理连接、分发消息、执行 IO 完成回调。

### 事件循环

```
reactor_->run()
  └── while(running_) {
        run_once(10ms)
          → transport_->poll(10) → events
          → for each event:
              ├── CONNECT:    初始化 recv_buffers_
              ├── DATA:       recv_buffers_ += data → dispatch_message()
              ├── DISCONNECT: 回调 + 清理 buffer
              └── ERROR:      回调 + 清理
          → io_pool_->process_completions()
      }
```

### Handler 注册

通过 `register_handler<MsgType>(callback)` 注册消息处理函数。内部包装为 GenericHandler 存入 handlers_ 映射表。

### 线程安全

`reactor_->send()` 可在 Reactor 线程外调用。Linux `::send()` 对小帧是原子的。

---

## IOThreadPool

### 核心职责

通用线程池，支持 task 在工作线程执行、completion 在 Reactor 线程执行。

### 使用模式

- submit(task, completion): task 在工作线程执行，completion 存入完成队列
- process_completions(): 在调用线程（通常是 Reactor 线程）执行已完成的 completion

### 设计

文件 I/O 不阻塞 Reactor，completion 回调线程安全。

---

## DataClient

### 核心职责

阻塞 TCP 数据客户端，用于 Worker 间数据传输。

### 设计特点

- 每次调用创建独立阻塞 TCP socket，完全不走主 Reactor
- 避免多线程并发读数据时的连接冲突
- 内置超时控制 (SO_SNDTIMEO + SO_RCVTIMEO + deadline)
- 两段式接收：先解析帧头，再接收 raw payload 直接到 FlyBufferPtr

---

## DataClientPool

### 核心职责

数据客户端连接池，支持并发请求控制。

### 设计特点

- 并发限制：pool_size（默认 2）限制同时 in-flight 的请求数
- 使用 active_count_ + slot_cv_ 实现信号量语义
- 与 DataClient 相同的两段式接收逻辑，零拷贝 raw payload

### 使用场景

Worker 读取远程数据时，通过 DataClientPool 并发请求多个 Worker，避免无限并发导致连接爆炸。

---

## MetadataClient

### 核心职责

阻塞 TCP 元数据客户端，向 Master 查询数据对象的位置信息。

### 设计特点

- 与 DataClient 类似，每次调用创建独立阻塞 TCP socket
- Worker 在三层降级读取的 Layer 3 使用

---

## 核心流程

### Worker 注册

```
Worker.start()
  → 创建 Transport + Data Server (随机端口)
  → 连接 Master
  → 发送 RegisterMessage

Master.on_worker_register()
  → conn_to_worker_[conn_id] = worker_id
  → worker_manager_->register_worker(...)
  → DataService.register_worker(worker_id, host, port)
  → 回复 RegisterAckMessage
```

### 心跳

```
Worker.heartbeat_thread_ (10s 间隔)
   → HeartbeatMessage → Master
   → Master 收到 → HeartbeatAckMessage → Worker
   → Worker 收到 ACK → touch_master_contact()

Master.heartbeat_check_thread_ (5s 间隔)
   → heartbeat_monitor_->check_all_workers()
   → 超时 Worker → ShutdownMessage

Worker.master_liveness_check:
   → MASTER_TIMEOUT_SECONDS = 120
   → 超时 → initiate_shutdown("master timeout")
```

---

## 设计决策

| 决策 | 原因 |
|------|------|
| epoll 边缘触发 | 减少系统调用次数 |
| 单 Reactor 线程 | handler 无锁，避免 IO 多线程锁竞争 |
| per-conn 拼接缓冲 | TCP 粘包/拆包安全处理 |
| IOThreadPool completion pattern | 文件 I/O 不阻塞 Reactor |
| DataClient 独立 socket | 多线程读无冲突 |
| 工厂函数 | 支持未来替换 UDP/RDMA |
| CV-based 心跳 | stop() 时可立即唤醒 |
| HeartbeatAck 双向检测 | Worker 通过 ACK 检测 Master 存活 |
| sendv scatter-gather | 合并 header+payload 发送 |
