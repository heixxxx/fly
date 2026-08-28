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
| EpollMultiplexer | `cpp/epoll_multiplexer.h/cpp` | 事件复用抽象（水平触发 + EV_ONESHOT） |
| ConnectionManager | `cpp/connection_manager.h` | conn_id 管理 + 事件分发（抽象接口，`create_connection_manager` 工厂） |
| TcpConnectionManager | `cpp/tcp_connection_manager.h/cpp` | ConnectionManager 的 TCP 实现（conn↔fd 双映射、write_buffers_、drain） |
| Reactor | `cpp/reactor.h/cpp` | 单线程事件循环（内含 HandlerThreadPool） |
| HandlerThreadPool | `cpp/reactor.h/cpp` | 通用任务线程池（有界背压）+ 消息 handler 专用串行 lane（同 conn 保序、跨 conn 并行；shutdown 排空不丢） |
| MessageProtocol | `cpp/message_protocol.h` | 二进制帧协议（header-only） |
| DataClientPool | `cpp/data_client_pool.h/cpp` | 数据客户端连接池 |
| NetQualityMonitor | `cpp/net_quality_monitor.h/cpp` | per-host 网络质量评分表（RTT/带宽） |
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

接收侧：DataClientPool 分步 recv（header → sub-header → small_fields → raw 直接进 FlyBuffer），零拷贝。

### 粘包处理

dispatch_message 内 while 循环解析。数据不足则等待更多数据。

### 消息类型总表（权威）

> **本节是消息类型语义的唯一权威口径**；其他文档提及消息类型一律链接此处，不复制清单、**不写具体数量**（数量随开发持续变化，以源码为准）。
> 权威源码：`src/network/cpp/message_types.h`（消息名与枚举编号以源码为准）。本表只维护语义分组，新增消息归入对应分组即可。

| 分组 | 消息 | 语义 |
|------|------|------|
| 生命周期 | REGISTER/ACK、HEARTBEAT/ACK、SHUTDOWN、STOP_NOW、WORKER_PROBE/ACK | 注册（role/数据端口/属性）、心跳、优雅退出、快速自杀、重复注册活性探测 |
| 任务 | TASK_SUBMIT/ACK、TASK_ASSIGN、TASK_COMPLETE、TASK_FAILED | 提交（inputs/requires/priority/vars）、派发（内联依赖位置+var payload）、完成（written_objects+资源指标）、失败（dirty_objects+错误分类） |
| 数据面控制 | DATA_QUERY/LOCATION、DATA_REQUEST/RESPONSE | master 查副本位置；两段式数据响应（见上） |
| 元数据 | DB_PATH_REQUEST/ACK、WRITE_REGISTER/ACK、OBJECT_REMOVED、REMOVE_*、IDX_LOAD_COMMAND/ACK | db 路径、写注册（provenance）、对象删除三级、idx 加载（load_db） |
| 备份 | BACKUP_REQUEST/ASSIGN/COMPLETE、WORKER_BACKUP_SUGGEST | 手动/自动副本、TIER2 读流量上报 |
| 冻结 | DATABASE_FREEZE/ACK | db 冻结（pending 两阶段 commit/rollback） |
| Var | VAR_SET/GET/ACK/REMOVE/BROADCAST | 小对象 KV |
| Merge | DELETE_DATA/ACK、MERGE_CLEANUP/ACK | 删源数据、全局一致性屏障 |
| 属性 | WORKER_PROPERTY_UPDATE、WORKER_PROPERTY_ASSIGN | 属性上行回报 / ensure_workers 下行追加 |
| 退出 | WORKER_EXIT | worker 正常退出显式声明（graceful 分支关连接前发出）：master 据此把断连归类为正常退出（handle_worker_exit），区别于异常判死；已入 serialized domain 保证先于同连接 DISCONNECT 处理 |
| 探测 | NET_PROBE_REQUEST/RESPONSE | 数据面 RTT/带宽探测 |
| PeerRpc | PEER_RPC_REQUEST/RESPONSE | worker↔worker 业务 RPC |
| 监控 | MONITOR_SAMPLE、MONITOR_TASK_IO | 负载采样成组上报、对象级 IO 明细 |
| 日志 | LOG_MESSAGE、MSG_COUNT_REQUEST/REPORT、MSG_LIMIT_SYNC | 高价值日志推送 + 配额（详见 [message-system.md](../message-system.md)） |
| 自动补齐 | STORAGE_SPAWN_REQUEST/ACK | auto storage node spawn |

---

## Reactor

### 核心职责

单线程事件循环，管理连接、分发消息。`handler_lanes > 0`（默认 4，Config）时启用
**lane 并行分发**：帧提取留在 reactor 线程，decode + handler 投递到
`conn_id % lanes` 的专用串行 lane——同连接消息严格保序（Register→*、
WriteRegister→TaskComplete 等协议顺序依赖），跨连接并行。connect/disconnect/error
事件回调同样经该 conn 的 lane 执行，保证与在途消息的先后关系。

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
      }
```

> 曾有独立的 IOThreadPool 类，2026-08-16 死代码清理批次删除（C++ 生产零使用，handler 职责由 HandlerThreadPool 承担）。

### Handler 注册

通过 `register_handler<MsgType>(callback)` 注册消息处理函数。内部包装为 GenericHandler 存入 handlers_ 映射表。

### 线程安全

`reactor_->send()` 可在 Reactor 线程外调用：per-conn 互斥锁（`conn_send_mutexes_`）保证同一连接的发送串行化。

---

## DataClientPool

### 核心职责

数据客户端连接池：keep-alive fd 复用 + 并发请求控制。

### 设计特点

- **keep-alive 连接复用**：同 peer 维持多条连接（单 fd 同步 request-response 无法并行），跨 request 复用 idle fd，避免高频远程读时反复 socket()+connect()+close()
- **并发限制**：pool_size 限制同时 in-flight 的请求数（slot 信号量：active_count_ + slot_cv_），保护对端不被压垮。生产进程按 Config `data_client_pool_size`（默认 4）初始化；C++ 构造签名的默认值 2 仅是兜底
- **容量模型**：fd 总量上限 `2×pool_size`（in-use ≤ pool_size，余量给 idle 缓冲）。达上限需新建时 idle ≥ pool_size ≥ 1，总能淘汰，不阻塞
- **反倾斜 + LRU 淘汰**：需淘汰时优先选 idle 数最多的 peer 里最老（last_used 最早）的 fd，防热点 peer 独占全部 idle 连接
- **三重健康保护**：
  1. idle TTL（60s）过期清理（防半开连接）
  2. 借出时 `SO_ERROR` 预检（probe_fd_health），失败不开工
  3. send/recv 失败即 `release_fd(fd, healthy=false)` 关闭并移除；完整交换 healthy=true 归还复用
- 两段式接收（帧头 → raw payload 直接进 FlyBufferPtr），零拷贝
- 锁纪律：probe/evict/reap 的 `::close` 在锁内（快），`connect` 在锁外（阻塞系统调用）；并发 connect 用配额预留避免超限

### 使用场景

Worker 读取远程数据时，通过 DataClientPool 并发请求多个 Worker，避免无限并发导致连接爆炸。

---

## NetQualityMonitor

### 核心职责

进程内单例，维护 `host → {rtt_ms, bandwidth_mbps}` 的网络质量评分表，供 DataService TIER2 远程读按连接性排序副本。

### 数据来源（两种，互补）

- **被动 RTT**：`DataClientPool::request` 在每次完整往返（含 DATA_NOT_READY/OBJECT_NOT_FOUND 等协议级响应）后，记录 connect→收完响应的耗时，调 `update_rtt`。连接失败不计。零额外探测流量。
- **主动带宽探测**：WorkerAgent 的 `bandwidth_probe_thread_`（仿 heartbeat 四件套，`net_probe_enabled` 控制）周期性对 `DataService::get_all_workers()` 返回的每个 peer 发 `NET_PROBE_REQUEST`，peer 的 DataServer 按请求 `payload_size_` 回 `NET_PROBE_RESPONSE`，探测线程据往返耗时算 RTT+带宽，调 `update_rtt` + `update_bandwidth`。

### 评分与排序

- `score(host) = w_rtt/max(rtt,1) + w_bw*bandwidth`（权重/ttl 为编译期常量，network 层无 Config 依赖）。
- 无数据 → score=0；超过 ttl 的陈旧数据 → 视为无数据。
- 消费侧（`DataService::read_raw_compressed` TIER2）用 `std::stable_sort` + lambda 调 `score()` 排序副本：等分（含冷启动无数据）保持注册顺序，行为与未启用一致。

### 分层

纯 network 层组件（仅依赖 CMString），不认识 RemoteObjectInfo。排序逻辑由 storage 层 DataService 完成，避免循环依赖。

---

## MetadataClient

### 核心职责

阻塞 TCP 元数据客户端，向 Master 查询数据对象的位置信息。

### 设计特点

- 每次调用创建独立阻塞 TCP socket
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
| epoll 水平触发 + ONESHOT | ONESHOT 防多线程惊群，处理完手动 rearm |
| 单 Reactor 线程 | handler 无锁，避免 IO 多线程锁竞争 |
| per-conn 拼接缓冲 | TCP 粘包/拆包安全处理 |
| DataClientPool 独立 socket + keep-alive | 多线程读无冲突，fd 跨请求复用 |
| 工厂函数 | 支持测试 Transport 注入 |
| CV-based 心跳 | stop() 时可立即唤醒 |
| HeartbeatAck 双向检测 | Worker 通过 ACK 检测 Master 存活 |
| sendv scatter-gather | 合并 header+payload 发送 |
