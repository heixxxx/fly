# Storage 模块 — 存储层

## 模块概述

**位置**: `src/storage/`

存储层是 Fly 框架的核心数据管理模块，负责数据的写入、聚合、索引管理、读取（本地 + 远程）、压缩和数据库生命周期管理。设计为 Master 和 Worker 共用的统一层。

---

## 核心组件

| 组件 | 文件 | 职责 |
|------|------|------|
| Database | `cpp/database.h/cpp` | 统一存储接口，流式序列化+压缩，异步写入 |
| DataService | `cpp/data_service.h/cpp` | 统一内存索引，远程读写协调 |
| DataServer | `cpp/data_server.h/cpp` | epoll + send_thread_pool，响应远程数据请求 |
| ObjectCache | `cpp/object_cache.h` | 两层 LRU 读缓存 |
| DataWriter | `cpp/data_writer.h/cpp` | 纯落盘写入聚合器 |
| DataReader | `cpp/data_reader.h/cpp` | 纯读取字节流 |
| WriteBackQueue | `cpp/write_back_queue.h/cpp` | 异步写入队列 |
| StorageManager | `cpp/storage_manager.h/cpp` | Database 生命周期管理 |

---

## Database

### 核心职责

统一存储接口，负责数据的写入（流式序列化+压缩+异步落盘）和读取（缓存+远程IO+解压+反序列化）。

### 写入流程

**核心设计**: Database 统一负责流式序列化+压缩，DataWriter 纯落盘。CPU 密集操作在调用线程完成，WBQ 后台线程仅负责磁盘 I/O。

**时序保证**: 注册（通知 master）在压缩+缓存填充之后，确保 master 标记数据就绪时，远程读可立即从 cache 命中。

```
write_object(name, obj)
  │
  ├─ 1. 流式序列化 + 压缩管线
  │     → FlyBufferStreamBuf → CountingStreamBuf → CompressingStreamBuf
  │     → 输出：ObjectHeader + 分块压缩数据（完整磁盘格式）
  │     → 无中间 buffer 拷贝
  │
  ├─ 2. 填充 low cache
  │     → 远程读可直接命中，无需等待落盘
  │
  ├─ 3. 注册写入（通知 master）
  │     → master 标记数据就绪 → 调度依赖任务
  │     → 此时远程读已可从 cache 命中
  │
  └─ 4. 异步落盘
        → WBQ 后台线程执行磁盘写入
```

### Temp 写入流程（save_to_db=False）

temp 数据不落盘，仅存在于内存中。采用 shared_ptr 零拷贝路径：

```
write_temp_pickle(name, data)
  │
  ├─ 1. 压缩到 FlyBufferPtr（shared_ptr）
  │
  ├─ 2. 存储到 DataService（标记 COMPLETE）
  │
  └─ 3. 注册写入（通知 master）
```

### 读取流程

读取路径采用三级 fallback 设计：

```
read_object_compressed(name)
  │
  ├─ low cache hit → 直接返回（跳过远程 IO）
  │
  └─ miss → read_raw_compressed
            │
            ├─ Tier 1 (本地): try_read_local_raw
            │   → ObjectCache → local_idx → temp 数据
            │
            ├─ Tier 2 (直连): lookup_remote_idx → DataClient
            │   → 直接连接目标 Worker 读取
            │
            └─ Tier 3 (Master 代理): query_data_location → DataClient
                → 查询 Master 获取位置，再连接目标 Worker
```

---

## DataService

### 核心职责

单例，管理所有数据的内存索引，包括本地数据、远程数据、temp 数据和 Worker 注册信息。

### 索引管理

- **本地索引 (local_idx)**: 跟踪本地数据的写入状态（INCOMPLETE → COMPLETE），管理 temp 数据的 LRU 缓存和淘汰
- **远程索引 (remote_idx)**: 维护 object_name → (worker_id, host, port) 的映射，支持快速查找数据位置
- **Worker 注册 (worker_registry)**: 管理 Worker 的地址信息

### 远程读取的三级 fallback

1. **Tier 1 (本地)**: 检查 ObjectCache → 本地索引 → temp 数据
2. **Tier 2 (直连)**: 通过 remote_idx 直接连接目标 Worker
3. **Tier 3 (Master 代理)**: 查询 Master 获取数据位置，再连接目标 Worker

### Temp 数据管理

- temp 数据存储为 shared_ptr，实现零拷贝读取
- LRU 淘汰策略：超过内存限制时淘汰最久未访问的 temp 数据
- 淘汰的数据可溢出到 TempStore（磁盘）

### 远程索引更新时机

- WriteRegister 时：Master 更新 remote_idx
- Tier 3 读取成功后：Worker 缓存 remote_idx
- 依赖位置预取：TaskAssignMessage 携带依赖数据位置

---

## DataServer

### 核心职责

响应其他 Worker 的数据请求，采用 epoll + send_thread_pool 模式。

### 架构

- **epoll 线程池**: 接收连接和请求，解析 DataRequestMessage
- **send 线程池**: 执行实际数据发送，避免阻塞 epoll 线程
- **SendTask 队列**: epoll 线程提交发送任务，send 线程消费

### 工作流程

1. epoll 线程接收 DataRequestMessage
2. 查询 DataService.try_read_local_raw() 获取数据
3. 使用 DataResponseProtocol 两段式编码（header + raw payload）
4. 提交 SendTask 到 send_queue
5. send 线程执行实际发送

### 零拷贝设计

raw payload 通过 shared_ptr 共享引用 ObjectCache 中的数据，避免拷贝。

### scatter-gather 发送

当有 raw payload 时，使用 writev 系统调用将 header 和 payload 合并为一次发送，减少系统调用次数。

---

## ObjectCache

### 核心职责

两层 LRU 读缓存，减少磁盘和远程 IO。

### 两层设计

| 层 | 存储内容 | 收益 | 填充时机 |
|----|---------|------|---------|
| low | 压缩字节 (shared_ptr) | 省磁盘/远程 IO | write_object complete |
| high | 反序列化对象 | 省反序列化 | C++ read_object<T> 命中后 |

### 淘汰策略

- LFU score = read_count / (now - last_access)
- 30s 保护期：新创建的条目不会被立即淘汰
- 1.5× 硬限制：超过 max_bytes × 1.5 时强制淘汰

---

## 写入时序保证

**核心约束**: 注册（通知 master）必须在压缩+缓存填充之后。

原因：master 收到 WriteRegister 后立即标记数据就绪并调度依赖任务。如果数据还未存储或缓存未填充，其他 worker 的读取会失败。

```
正确时序：compress → cache.put_low → register_write
错误时序：register_write → compress → cache.put_low
```

---

## 设计决策

| 决策 | 原因 |
|------|------|
| 流式序列化+压缩管线 | 避免中间 buffer 拷贝 |
| 三级 fallback 读取 | 本地优先，减少网络 IO |
| 两层 LRU 缓存 | low 层省 IO，high 层省反序列化 |
| 零拷贝 shared_ptr | temp 数据和远程读避免拷贝 |
| writev scatter-gather | 合并 header+payload 发送 |
| 注册在压缩之后 | 确保数据可读后才通知 master |
