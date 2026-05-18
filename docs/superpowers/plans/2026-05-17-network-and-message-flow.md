# Fly 网络连接层实现方式与消息收发流程

**日期**: 2026-05-17

---

## 一、整体架构

```
┌───────────────────────────────────────────────────────────┐
│                    Master Node                             │
│                                                            │
│  Main Thread                                               │
│  ├── MasterAgent                                           │
│  │   ├── submit_task() → graph_->add_task()               │
│  │   └── schedule_tasks() → scheduler_->schedule_all()    │
│  │                                                         │
│  Reactor Thread (独立线程)                                  │
│  ├── TransportLayer (epoll TCP Server)                     │
│  │   └── listen("0.0.0.0:port") → accept4(NONBLOCK)      │
│  ├── recv_buffers_ (per-conn 拼接缓冲)                     │
│  └── handlers_ (MessageType → GenericHandler 映射)         │
│                                                            │
│  Heartbeat Check Thread (独立线程)                          │
│  └── 每 5s 检查 Worker 心跳超时                             │
└───────────────────────────┬───────────────────────────────┘
                            │ TCP
          ┌─────────────────┼─────────────────┐
          ▼                 ▼                 ▼
┌──────────────┐  ┌──────────────┐  ┌──────────────┐
│  Worker 1    │  │  Worker 2    │  │  Worker N    │
│              │  │              │  │              │
│ Reactor Thd  │  │ Reactor Thd  │  │ Reactor Thd  │
│ ├── TCP Srv  │  │ ├── TCP Srv  │  │ ├── TCP Srv  │
│ │ (Data Srv) │  │ │ (Data Srv) │  │ │ (Data Srv) │
│ └── Master ←─│  │ └── Master ←─│  │ └── Master ←─│
│     conn     │  │     conn     │  │     conn     │
│              │  │              │  │              │
│ DataService  │  │ DataService  │  │ DataService  │
│ IOThreadPool │  │ IOThreadPool │  │ IOThreadPool │
│ (transfer)   │  │ (transfer)   │  │ (transfer)   │
│              │  │              │  │              │
│ Heartbeat Thd│  │ Heartbeat Thd│  │ Heartbeat Thd│
│ (CV, 10s)    │  │ (CV, 10s)    │  │ (CV, 10s)    │
│              │  │              │  │              │
│ Main Thread  │  │ Main Thread  │  │ Main Thread  │
│ └── poll_task│  │ └── poll_task│  │ └── poll_task│
│ DataClient * │  │ DataClient * │  │ DataClient * │
└──────────────┘  └──────────────┘  └──────────────┘
  * 按需创建, 独立于 Reactor
```

**关键设计**: 每个 Worker 的 Master 连接和 Data Server 共用同一个 Transport + Reactor。一个 epoll 实例同时管理 Master 通信 fd 和 Data Server listen fd。数据传输请求由 DataService 的 IOThreadPool 异步处理，不阻塞 Reactor 线程。Worker A 通过独立 DataClient 连接读取远程数据，不走主 Reactor。

---

## 二、核心组件

### 2.1 TransportLayer (传输抽象)

**文件**: `network/cpp/transport.h` + `tcp_transport.cpp`

| 接口 | 作用 |
|------|------|
| `listen(addr, port)` | TCP Server: socket → bind → listen → epoll ADD |
| `connect(addr, port)` | TCP Client: socket → connect → epoll ADD (非阻塞, EINPROGRESS) |
| `poll(timeout_ms)` | epoll_wait → 返回 `CMVector<TransportEvent>` |
| `send(conn_id, data)` | 通过 conn_id→fd 映射，调用 `::send(fd, ...)` |
| `close(conn_id)` | epoll DEL → close(fd) → unregister |
| `get_bound_port()` | port=0 时通过 getsockname 获取内核分配的实际端口 |

**实现特点**:
- epoll 事件模式: listen_fd 用 `EPOLLIN`，client_fd 用 `EPOLLIN | EPOLLET` (边缘触发)
- 所有 socket 非阻塞 (`SOCK_NONBLOCK | SOCK_CLOEXEC`)
- `accept4` 直接创建非阻塞 client fd
- `drain_socket` 循环 recv 直到 EAGAIN，单次最多 64KB
- conn_id 单调递增，双映射 `conn_to_fd_` / `fd_to_conn_` 管理
- 工厂函数 `create_transport("tcp")` 支持未来扩展 UDP/RDMA

### 2.2 MessageProtocol (帧协议)

**文件**: `network/cpp/message_protocol.h`

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
| `encode<T>(msg)` | bitsery 序列化 → 拼接 `[4字节长度][1字节类型][payload]` |
| `decode<T>(buffer, msg)` | 校验长度+类型 → bitsery 反序列化 → `buffer.erase` 消费已解析字节 |
| `get_type(buffer)` | 从 buffer 读取第 5 字节得到 MessageType |
| `get_total_size(buffer)` | 读取前 4 字节大端序长度 |

### 2.3 Reactor (事件循环)

**文件**: `network/cpp/reactor.h` + `reactor.cpp`

**事件循环**:

```
reactor_->run()
  └── while(running_) {
        run_once(10ms)
          └── transport_->poll(10) → CMVector<TransportEvent>
                └── for each event: handle_event()
                      ├── CONNECT:    初始化 recv_buffers_[conn_id]
                      ├── DATA:       recv_buffers_[conn_id] += data
                      │                 └── dispatch_message() 循环解析
                      ├── DISCONNECT:  回调 + 清理 buffer
                      └── ERROR:       回调 + 清理 buffer
      }
```

**Handler 注册机制**:

```cpp
reactor_->register_handler<RegisterMessage>(
    [this](uint64_t conn_id, const RegisterMessage& msg) { ... });
```

内部实现: 将 handler 包装为 `GenericHandler` 存入 `handlers_[MessageType]` 映射表。收到数据时:
1. `dispatch_message` 从 buffer 读 MessageType
2. 查 `handlers_` 表找到对应 GenericHandler
3. GenericHandler 内部调用 `MessageProtocol::decode<T>(buffer, msg)`
4. decode 成功则调用具体 handler，同时 buffer 被 `erase` 消费

**TCP 粘包处理**: `dispatch_message` 内 `while(!buffer.empty())` 循环解析。每轮尝试 decode:
- 成功 → 消费字节，继续下一轮
- 数据不足 → break 等待更多数据
- 无对应 handler → 跳过该消息，消费字节

**线程安全**: `reactor_->send()` 可在 Reactor 线程外调用（如 Main Thread 发送 TaskComplete）。由于 Linux `::send()` 对单个 fd 写入是原子的（小帧），且只有 Reactor 线程 poll，多线程 send 安全。

### 2.4 IOThreadPool (线程池)

**文件**: `network/cpp/io_thread_pool.h` + `io_thread_pool.cpp`

| 接口 | 作用 |
|------|------|
| `submit(task, completion)` | 提交任务，task 在工作线程执行，completion 在 `process_completions()` 调用线程执行 |
| `start()` / `stop()` | 启动/停止工作线程 |
| `process_completions()` | 在调用线程（通常是 Reactor 线程）执行已完成的 completion 回调 |

**使用模式**: Reactor 通过 `set_io_pool()` 持有 IOThreadPool 引用，在 `run()` 循环中每轮调用 `process_completions()`，确保 completion 回调在 Reactor 线程执行（线程安全）。

### 2.5 DataClient (阻塞数据客户端)

**文件**: `network/cpp/data_client.h` + `data_client.cpp`

| 接口 | 作用 |
|------|------|
| `request_data(host, port, object_name, timeout_ms)` | 阻塞 TCP 请求：connect → send DataRequestMessage → recv DataResponseMessage → close |

**设计特点**:
- 每次调用创建独立阻塞 TCP socket，**完全不走主 Reactor**
- 避免多线程并发读数据时的连接冲突
- 内置超时控制 (SO_SNDTIMEO + SO_RCVTIMEO + deadline)
- 消息帧收发: `MessageProtocol::encode` 发送，手动解析帧头 + `MessageProtocol::decode` 接收

---

## 三、各类消息收发流程

### 3.1 Worker 注册流程

```
Worker.start()
  1. create_transport("tcp")
  2. transport->listen("0.0.0.0", 0)           // Data Server, 随机端口
  3. data_server_port_ = transport->get_bound_port()
  4. master_conn_ = transport->connect(master_host, master_port)
  5. reactor_ = new Reactor(transport)          // Master conn + Data Server 共用一个 Reactor
  6. register_handler<RegisterAckMessage>(...)
  7. reactor_thread_ = thread { reactor_->run() }
  8. reactor_->send(master_conn_, RegisterMessage{
       worker_id, role, attributes,
       data_server_host="127.0.0.1",
       data_server_port
     })
                    │
                    ▼ TCP
Master.ReactorThread
  └── on_worker_register(conn_id, RegisterMessage)
        ├── conn_to_worker_[conn_id] = worker_id
        ├── worker_to_conn_[worker_id] = conn_id
        ├── worker_manager_->register_worker(...)
        ├── DataService.register_worker(worker_id, host, port)
        └── reactor_->send(conn_id, RegisterAckMessage{
               worker_id, master_address, master_port
             })
                    │
                    ▼ TCP
Worker.ReactorThread
  └── on_register_ack(RegisterAckMessage)
        └── registered_ = true
```

### 3.2 心跳流程

```
Worker.heartbeat_thread_ (独立线程, CV-based, 10s 间隔)
  └── while(heartbeat_running_) {
        cv_.wait_for(10s)  // 非 sleep, stop() 可立即唤醒
        HeartbeatMessage{worker_id} → reactor_->send(master_conn_, ...)
      }
              │
              ▼ TCP
Master.ReactorThread
  └── on_heartbeat(conn_id, HeartbeatMessage)
        └── worker_manager_->set_heartbeat(worker_id, timestamp)

Master.heartbeat_check_thread_ (独立线程, CV-based, 5s 间隔)
  └── heartbeat_monitor_->check_all_workers(timestamp)
        └── get_dead_workers() → 超时 Worker → 发送 ShutdownMessage
```

### 3.3 任务提交与调度流程

```
[用户代码 / Worker 递归提交]
WorkerAgent.submit_task(name, module, args, inputs)
  └── reactor_->send(master_conn_, TaskSubmitMessage{name, module, args, inputs})
              │
              ▼ TCP
Master.ReactorThread
  └── handler: submit_task(++remote_task_counter_, name, module, args, inputs, {})
        ├── metadata_->create_task(task_id, name, inputs, ...)
        ├── graph_->add_task(task_id, inputs)     // 注册依赖
        └── schedule_tasks()
              ├── scheduler_->schedule_all_available()
              │     └── 遍历 ready_tasks × idle_workers → FIFO 匹配
              └── for each (task, worker):
                    assign_task_to_worker(task_id, worker_id)
                      └── reactor_->send(worker_conn, TaskAssignMessage{
                             task_id, task_name, task_module, args
                           })
```

### 3.4 任务执行与完成流程

```
Worker.ReactorThread
  └── on_task_assign(TaskAssignMessage)
        └── task_queue_.push({task_id, task_name, module, args})  // 入队，不直接执行

Worker.MainThread (poll_task 循环)
  └── poll_task()
        ├── task = task_queue_.pop()
        ├── begin_task(task_id)               // 设置 current_task_id_, 清空 current_writes_
        │     └── WorkerAgentContext::set(trampoline, this)  // 注册写入回调
        ├── executor_->execute(task_id, ...)
        │     ├── importlib.import_module(task_module)
        │     ├── pickle.loads(args)
        │     └── 执行 Python 原始函数
        │           └── 函数内 db.write_object(name, obj)
        │                 └── Database._write_typed(name, data, type_name)
        │                       └── WorkerAgentContext 触发 record_write_trampoline
        │                             └── current_writes_.push_back(db_id + ":" + name)
        ├── tracked_writes = end_task(task_id) // WorkerAgentContext::clear()
        │
        ├── [成功] reactor_->send(master_conn_, TaskCompleteMessage{
        │       task_id, worker_id,
        │       written_objects = tracked_writes + result.outputs,
        │       frozen_dbs = result.frozen_dbs
        │     })
        │
        └── [失败] reactor_->send(master_conn_, TaskFailedMessage{
               task_id, worker_id, error_message
             })
                    │
                    ▼ TCP
Master.ReactorThread
  └── on_task_complete(TaskCompleteMessage)
        ├── worker_manager_->complete_task(worker_id)    // Worker 标记空闲
        ├── for written_object in written_objects:
        │     ├── [non-streaming] graph_->mark_data_ready(data_path)
        │     └── [non-streaming] DataService.update_remote_idx(obj_name, worker_id, host, port)
        ├── for frozen_db in frozen_dbs:
        │     └── db_instances_[db_id]->freeze()         // Master 侧 C++ freeze
        ├── graph_->remove_task(task_id)
        ├── metadata_->update_task_status(task_id, COMPLETED)
        └── schedule_tasks()                             // 尝试调度新任务
```

### 3.5 跨 Worker 数据读取流程

读取流程分三层，逐步降级，最大限度减少 Master 查询:

```
Python: db.read_object("key")

Layer 1: 本地读取 (DataService.try_read_local)
  └── 查内存 local_idx (O(1))
        ├── 找到 + flushed → DataReader.read_from_entries()
        │     └── 单条目 → read_object_data(entry) → 解析 ObjectHeader (含 py_name)
        │     └── 多条目 (large object) → 排序拼接, 从首块 ObjectHeader 提取 py_name
        │     └── 返回 ReadResult{data_buffer, py_name}
        └── 未找到 → 进入 Layer 2

Layer 2: 远程索引缓存 (DataService.lookup_remote_idx)
  └── 查内存 remote_idx
        ├── 有缓存 → WorkerAgent.request_data_from_worker(host, port, key)
        │     └── DataClient::request_data() — 独立阻塞 TCP 连接，不走主 Reactor
        │              │
        │              ▼ TCP (直连 Worker B Data Server)
        │     Worker B.ReactorThread
        │       └── on_data_request(conn_id, DataRequestMessage)
        │             └── DataService.submit_transfer(conn_id, object_name)  ← 非阻塞，立即返回
        │                   │
        │                   ▼ IOThreadPool (data_server_threads 个工作线程)
        │                   DataService.try_read_local(object_name)
        │                     └── DataReader 文件 I/O
        │                   │
        │                   ▼ process_completions() (回到 Reactor 线程)
        │                   TransferCallback → reactor_->send(conn_id, DataResponseMessage)
        │
        ├── 成功 → 返回 {data, py_name}
        └── 失败 (缓存过期/Worker 下线) → 日志记录具体异常 → 降级, 进入 Layer 3

Layer 3: 全程远程 (最多 3 次重试, 每次 sleep 1s)
  └── WorkerAgent.request_remote_data(object_name)
        ├── pending_data_[object_name] = PendingRemoteData{}
        ├── reactor_->send(master_conn_, DataQueryMessage{object_name})
        │              │
        │              ▼ TCP
        │ Master.ReactorThread
        │   └── DataService.has_remote_location(object_name)?
        │         ├── Yes → DataLocationMessage{worker_id, data_host, data_port, success=true}
        │         └── No  → DataLocationMessage{success=false}
        │         reactor_->send(conn_id, response)
        │              │
        │              ▼ TCP
        │ Worker A.ReactorThread
        │   └── on_data_location(conn_id, DataLocationMessage)
        │         └── pending->data_host/port = ... ; pending->location_received = true
        │
        ├── poll 循环 (sleep 50ms, 最多 5s):
        │     └── 检测 location_received →
        │           DataClient::request_data(data_host, data_port, object_name)
        │             ← 独立阻塞 TCP 连接，不走主 Reactor
        │                     │
        │                     ▼ TCP (直连 Worker B Data Server)
        │           Worker B: submit_transfer → IOThreadPool 文件 I/O → reactor_->send(DataResponseMessage)
        │                     │
        │                     ▼ TCP (同一连接)
        │           DataClient 接收并解码 DataResponseMessage → 返回 {success, data, error}
        │
        ├── 成功 → DataService.update_remote_idx(name, worker_id, host, port) → 返回
        ├── 失败 → sleep(1s) → 重试 (回到 Layer 3 开头)
        └── 3 次均失败 → throw RuntimeError → executor 进入 task 失败处理
```

C++ 模板读取路径 (无 Python 参与):
```
Database::read_object<T>(name)
  └── fly::DataService::instance().try_read_local(name)
        ├── [找到] ReadResult → FLY_DECODE_FROM_BYTES(result.data_buffer, T, obj)
        └── [未找到] throw runtime_error
```

**关键设计**:
- 所有读取路径 (Python 三层 + C++ 模板) 统一经过 `DataService.try_read_local`
- `DataReader.read_from_entries` 从 ObjectHeader 提取 `py_name`，确保 C++ 导出类型正确反序列化
- **Worker B 数据传输**: `on_data_request` 非阻塞委托给 DataService IOThreadPool，文件 I/O 在线程池执行，完成后回调在 Reactor 线程发送响应
- **Worker A 数据读取**: `DataClient::request_data()` 创建独立阻塞 TCP 连接，完全不走主 Reactor，避免多线程读冲突
- 远程读取成功后自动更新 `DataService.remote_idx`，后续同对象读取可跳过 Master 查询
- 重试仅在 Layer 3 (全程远程)，Layer 1/2 是确定性的 (内存查找)
- 重试假设 task 的 read 操作都是合理的 (依赖已声明，数据应已就绪)

### 3.6 Master 侧数据服务

```
Worker/Master → Master (DataRequestMessage)
Master.ReactorThread
  └── on_data_request(conn_id, DataRequestMessage)
        ├── 遍历 db_instances_ 查找 object_name
        ├── db->read_object_typed(name) → 读取 bytes
        └── reactor_->send(conn_id, DataResponseMessage{data, success})
```

Master 也充当 data server (worker_id=0)，提供自己写入的数据。

### 3.7 DB 路径查询

Worker 在执行任务时如需打开不在本地 `databases_` 中的 Database，通过此流程向 Master 查询路径：

```
WorkerAgent.request_db_path(db_id)
  ├── 查本地 databases_[db_id] → 已有 → return true
  └── 未有 →
        ├── pending_db_paths_[db_id] = PendingDbPath{}
        ├── reactor_->send(master_conn_, DbPathRequestMessage{db_id})
        │              │
        │              ▼ TCP
        │ Master.ReactorThread
        │   └── 查找 db_registry_[db_id] → {base_path, data_path}
        │         ├── 找到 → DbPathResponseMessage{db_id, base_path, data_path, success=true}
        │         └── 未找到 → DbPathResponseMessage{db_id, success=false}
        │         reactor_->send(conn_id, response)
        │              │
        │              ▼ TCP
        │ Worker.ReactorThread
        │   └── on_db_path_response → pending->completed = true
        │
        ├── poll 循环 (sleep 50ms, 最多 5s):
        │     └── completed →
        │           [success] 创建 Database(base_path, data_path, worker_id)
        │                    存入 databases_[db_id] → return true
        │           [fail]   → return false
        │
        └── 超时 → return false
```

### 3.8 关机流程

```
Master.stop()
  └── 广播 ShutdownMessage 给所有 worker_to_conn_
              │
              ▼ TCP
Worker.ReactorThread
  └── on_shutdown(ShutdownMessage)
        └── registered_ = false

Master.stop() (继续)
  ├── heartbeat_check_running_ = false; cv_.notify_all()  // 立即唤醒心跳线程
  ├── heartbeat_check_thread_.join()
  ├── reactor_->stop()  → running_ = false                 // 退出事件循环
  └── reactor_thread_.join()
```

---

## 四、线程模型

| 节点 | 线程 | 职责 | 停止方式 |
|------|------|------|---------|
| **Master** | Main Thread | 用户 Python 代码，submit_task，查询状态 | — |
| | Reactor Thread | epoll 事件循环，所有消息收发和 handler 执行 | `reactor_->stop()` |
| | Heartbeat Check Thread | 每 5s 检查 Worker 心跳超时 | CV notify + join |
| **Worker** | Main Thread | poll_task() 循环，执行任务，处理结果 | — |
| | Reactor Thread | epoll 事件循环 (Master conn + Data Server)，消息 handler | `reactor_->stop()` |
| | Heartbeat Thread | 每 10s 发送心跳 | CV notify + join |

**心跳线程使用 `condition_variable::wait_for()` 而非 `sleep_for()`**，stop() 时可立即唤醒，无阻塞。

---

## 五、消息类型汇总

### 已实现并使用的消息

| 类型 | 枚举值 | 方向 | 触发时机 |
|------|--------|------|---------|
| REGISTER | 1 | W→M | Worker 启动连接 Master |
| REGISTER_ACK | 2 | M→W | Worker 注册成功后确认 |
| HEARTBEAT | 3 | W→M | 每 10s 心跳 |
| TASK_SUBMIT | 4 | 任意→M | 用户代码或 Worker 递归提交任务 |
| TASK_ASSIGN | 5 | M→W | 调度器匹配到空闲 Worker |
| TASK_COMPLETE | 6 | W→M | 任务执行成功 |
| TASK_FAILED | 7 | W→M | 任务执行失败 |
| DATA_READY | 8 | W→M | write_object 时实时发送 (streaming mode) |
| DATA_QUERY | 9 | W→M | Worker 请求远程数据位置 |
| DATA_LOCATION | 10 | M→W | Master 返回数据所在 Worker 的地址 |
| DATA_REQUEST | 11 | W→W / W→M | 直连目标请求数据 |
| DATA_RESPONSE | 12 | W→W / M→W | 返回请求的数据 bytes |
| SHUTDOWN | 13 | M→W | Master 关停时广播 |
| DB_PATH_REQUEST | 19 | W→M | Worker 查询 Database 路径 |
| DB_PATH_RESPONSE | 20 | M→W | Master 返回 Database 路径 |

### 已定义结构体但未独立使用的消息

| 类型 | 枚举值 | 说明 |
|------|--------|------|
| DATABASE_FREEZE | 14 | 结构体已定义，实际通过 TaskCompleteMessage.frozen_dbs 字段传递 |
| IDX_REQUEST | 15 | 结构体已定义，Freeze 后处理未实现 |
| IDX_RESPONSE | 16 | 结构体已定义，Freeze 后处理未实现 |
| CLEANUP_TASK | 17 | 结构体已定义，容错机制未实现 |
| CLEANUP_COMPLETE | 18 | 结构体已定义，容错机制未实现 |

---

## 六、数据结构速查

### Master 核心映射

```
conn_to_worker_:  conn_id → worker_id     // 连接与 Worker ID 双向映射
worker_to_conn_:  worker_id → conn_id
task_modules_:    task_id → module_name    // 任务参数存储
task_args_:       task_id → args[]
db_registry_:     db_id → {base_path, data_path}  // DB 路径注册表
db_instances_:    db_id → shared_ptr<Database>     // DB 实例
frozen_dbs_:      set<db_id>                       // 已冻结 DB 集合

DataService.instance() (单例):
  local_idx_:      object_name → {db_id, IndexEntry[], flushed}
  remote_idx_:     object_name → {worker_id, host, port}
  worker_registry_: worker_id → {host, port}
  db_paths_:       db_id → {base_path, data_path}
```

### Worker 核心映射

```
master_conn_:     uint64_t                 // 到 Master 的连接 ID
data_server_port_: int32_t                 // Data Server 监听端口
task_queue_:      queue<PendingTask>       // 待执行任务队列 (Reactor→Main 传递)
databases_:       db_id → shared_ptr<Database>
pending_data_:    object_name → PendingRemoteData  // 远程数据请求状态
current_task_id_: uint64_t                 // 当前执行任务
current_writes_:  vector<string>           // 当前任务写入记录

DataService.instance() (单例, 与 Master 共享):
  local_idx_:      本 Worker 写入的所有对象
  remote_idx_:     远程读取成功后缓存的位置信息
```

---

## 七、连接生命周期

### Master 侧 (Server)

```
TCP listen fd (epoll EPOLLIN)
  │
  ├── accept4 → client fd (SOCK_NONBLOCK)
  │     └── epoll ADD (EPOLLIN | EPOLLET)
  │     └── TransportEvent::CONNECT → recv_buffers_[conn_id] = ""
  │
  ├── EPOLLIN → drain_socket(fd, 64KB)
  │     ├── data → TransportEvent::DATA → recv_buffers_[conn_id] += data → dispatch
  │     └── empty → TransportEvent::DISCONNECT → 清理映射 + buffer
  │
  ├── EPOLLERR|EPOLLHUP → TransportEvent::ERROR → 清理
  │
  └── reactor_->send(conn_id, msg)
        └── encode msg → ::send(fd, frame)
```

### Worker 侧 (Client + Server 混合)

```
一个 Transport 实例同时管理:
  ├── listen fd (Data Server)    → 接受其他 Worker 的数据请求
  ├── master_conn_ fd (Client)   → 与 Master 通信
  └── 动态 connect fd            → request_remote_data 临时连接其他 Worker

所有 fd 共享一个 epoll 实例 + 一个 Reactor 线程
```

---

## 八、待实现功能与已知问题

### 8.1 待实现功能

#### F1. Database Freeze 后处理 (高优先级)

**当前状态**: Worker 侧 `db.freeze()` 成功，Master 通过 `TaskCompleteMessage.frozen_dbs` 收到通知，但后处理流程未实现。

**已定义但未使用**:
- `IDX_REQUEST (15)` / `IDX_RESPONSE (16)` 消息类型已在 `message_types.h` 中定义
- `IdxRequestMessage` / `IdxResponseMessage` 结构体已定义
- 无 handler 注册 (`register_handler<IdxRequestMessage>` 不存在)

**待实现流程**:
```
Worker: db.freeze() → TaskCompleteMessage.frozen_dbs = [db_id]
Master.on_task_complete():
  └── for frozen_db in frozen_dbs:
        └── db_instances_[db_id]->freeze()    // ✅ 已实现

Master (后处理, 未实现):
  1. 遍历 frozen_dbs, 向所有持有该 db 数据的 Worker 发送 IdxRequestMessage
  2. Worker 收到 IdxRequest → 返回本地 .idx 内容 (IdxResponseMessage)
  3. Master 合并所有 Worker 的 idx → 写入 base_path/merged.idx
  4. Master 收集 Worker 信息 → 写入 base_path/_META
  5. read_object 优先使用 merged.idx
  6. 已冻结 Database 的加载恢复 (load_meta + merged.idx)
```

#### F2. 跨 Worker 数据读取 E2E 测试 (高优先级)

**当前状态**: C++ 单元测试覆盖 DataService、DataReader、WorkerAgent 单独的接口，但缺少端到端验证:
- Worker A 写入 → Worker B 通过三层流程读取成功
- remote_idx 缓存命中/未命中路径
- 3 次重试后失败路径

#### F3. SSH / Custom Worker 启动 (中优先级)

**当前状态**: Python `agent.py` 仅有 `launch_local_workers()` (thread/process mode)。

**待实现**:
- `master.launch_ssh_workers(workers, ssh_user, ssh_key)`
- `master.launch_custom_workers(workers, submit_command)`
- `master.wait_for_workers(worker_ids)` — 阻塞等待指定 Worker 注册完成

#### F4. Locality 优化调度 (中优先级)

**当前状态**: `TaskScheduler::schedule_all_available()` 为 FIFO 匹配，不考虑数据位置。

**待实现**:
- DataService 提供查询接口: 给定 object_name 列表，返回每个 Worker 持有的数据量
- TaskScheduler 计算 locality_score: `worker_local_bytes / total_input_bytes`
- 优先分配 locality_score 最高的任务-Worker 对
- 调度策略可配置: FIFO / Locality / Hybrid

#### F5. 数据副本策略 (低优先级)

**当前状态**: `Database.write_object(name, data, backup=false)` — backup 参数已存在但未使用。

**待实现**:
- backup=True 时，写入完成后 Master 调度副本任务到其他 Worker
- BackupManager 组件 (Master 侧)，跟踪副本位置
- 副本感知调度: 数据请求可路由到任意持有副本的 Worker

#### F6. 容错机制 (低优先级)

**当前状态**: `HeartbeatMonitor` 检测超时 Worker → `on_disconnect` 标记 DEAD，但不会重调度任务。

**已定义但未使用**:
- `CLEANUP_TASK (17)` / `CLEANUP_COMPLETE (18)` 消息类型已定义
- 无 handler 注册

**待实现**:
```
HeartbeatMonitor.get_dead_workers() → dead_worker_ids
  └── Master:
        1. 查询 dead_worker 的 assigned_tasks → 重新入队 (PENDING)
        2. 发送 CleanupTaskMessage 给其他 Worker 清理临时数据
        3. worker_manager_->remove_worker(worker_id)
        4. schedule_tasks() → 重新调度
```

### 8.2 已知代码问题

#### ~~C1. `read_from_entries` 未设置 `py_name`~~ ✅ 已修复

`read_from_entries` 单对象场景委托 `read_object_data(entry)` (已处理 ObjectHeader)；多对象 (large object) 从首块 ObjectHeader 提取 `py_name`。

#### ~~C2. 模板 `read_object<T>` 绕过 DataService~~ ✅ 已修复

模板方法现在调用 `fly::DataService::instance().try_read_local()`，与 `read_object_typed` 路径统一。

#### ~~C3. Python `except Exception` 无 `as e`~~ ✅ 已修复

已改为 `except Exception as e` 并记录具体错误信息。

#### ~~C4/C5. `catch (...)` 无日志~~ ✅ 已修复 → 已重构

原 `on_data_request` 中的 try/catch + 同步文件 I/O 已被重构为 `DataService.submit_transfer()` 非阻塞委托。文件 I/O 在 IOThreadPool 执行，`try_read_local` 内部处理异常。完成回调在 Reactor 线程发送响应。

#### ~~C6. `DbPathRequestMessage`/`DbPathResponseMessage` 复用消息类型~~ ✅ 已修复

已分配独立消息类型 `DB_PATH_REQUEST (19)` 和 `DB_PATH_RESPONSE (20)`，不再复用 `DATA_QUERY`/`DATA_LOCATION`。

#### ~~C7. `on_db_path_response` 空操作~~ ✅ 已修复

已实现完整流程：
- `on_db_path_response` 存储响应并标记 `completed`
- 新增 `WorkerAgent::request_db_path(db_id)` 同步阻塞方法：发送请求 → 轮询响应 → 自动创建 Database 实例
- 已导出到 Python (`request_db_path`)
