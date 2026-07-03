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
| WriteBackQueue | `cpp/write_back_queue.h/cpp` | 异步写入队列（支持 clear_pending 丢弃脏写） |
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

### 写入事务（task 失败时的脏数据清理）

worker task 的写入被 BEGIN/END 段标记包裹（事务化）。task 失败时整段撤销，避免重跑时二次写入冲突：

- **BEGIN**：task 首次写入时打，记录 data 文件偏移作为回滚点
- **END**：task 成功时打，提交段内所有 ADD 进 idx entries_
- **ABORT**：task 失败时打，丢弃段内 ADD + data 文件 truncate 回回滚点

master 直接 write_object 不打标记（隐式事务，ADD 立即生效）。崩溃（进程死亡）→ 无 END/ABORT → load_db 时 pending 区自动丢弃未提交的脏 ADD。

异常清理由 `Database::abort_task_writes` 执行：`clear_pending`（清 WBQ 未落盘脏写）→ `abort_segment`（idx ABORT + data truncate）→ 清 DataService/ObjectCache 内存。详见 `docs/issues/001-failed-task-rerun-write-duplication.md`。

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

读取路径采用三级 fallback + 多副本轮询 + 分类重试设计：

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
            ├─ Tier 2 (多副本直连 + 退避): lookup_all_remote_idx
            │   → 遍历 remote_idx 中该对象的【全部副本】，逐个直连取数
            │   → 副本按网络质量排序（NetQualityMonitor.score）：连接性更好的副本优先
            │     尝试。stable_sort 保证等分（含无数据的冷启动副本）保持注册顺序，行为与
            │     未启用时一致。net_probe_enabled=0 时排序降级为 no-op。
            │   → 失败按 ReadError 分类: OBJECT_NOT_FOUND 删副本 / DATA_NOT_READY
            │     无限重试 / NETWORK 30s 限 / SHUTDOWN 立即终止
            │   → 一轮全失败后退避: 10ms 起 ×2 上限 500ms ±10% 抖动
            │
            └─ Tier 3 (位置查询 + 重入 TIER2): remote_compressed_read_handler
                → 向 master 查询对象的【全部副本】(DataLocationMessage.locations_)
                → 回填本地 remote_idx → 无条件重入 TIER2
                → master 也无位置时返回 can_still_produce,终止
```

**两层职责正交**：TIER2 负责「读取数据 + 退避重试」，TIER3 负责「查询位置 + 回填」。
TIER3 不取数，查到位置后重入 TIER2 让它取。`tier3_queried` 标志防止 TIER2↔TIER3 无限弹跳（TIER3 最多查一次）。

**master 进程特殊路径**：master 是位置权威，自己的 remote_idx 即全部副本。master
注册 direct handler（用 DataClientPool，与 worker 对称）走 TIER2，TIER3 handler 是
纯本地查询（`has_remote_location` + `has_pending||has_running`），不走网络——保留它
是为让 TIER2 无副本时能返回 `can_still_produce`（wait_obj 依赖此信号判断对象是否可能
仍被产出）。

---

## DataService

### 核心职责

单例，管理所有数据的内存索引，包括本地数据、远程数据、temp 数据和 Worker 注册信息。

### 索引管理

- **本地索引 (local_idx)**: 跟踪本地数据的写入状态（INCOMPLETE → COMPLETE），管理 temp 数据的 LRU 缓存和淘汰
- **远程索引 (remote_idx)**: 维护 object_name → **副本列表** (`RemoteObjectMeta.workers_`，可多副本) 的映射，支持多副本查找。`lookup_all_remote_idx` 返回全部副本供 TIER2 轮询；`lookup_remote_idx`（单值，front）供 auto-backup 选源等单点场景
- **Worker 注册 (worker_registry)**: 管理 Worker 的地址信息

### 远程读取的三级 fallback（含多副本容错）

1. **Tier 1 (本地)**: 检查 ObjectCache → 本地索引 → temp 数据
2. **Tier 2 (多副本直连)**: `lookup_all_remote_idx` 取该对象的**全部副本**，逐个用
   `DirectCompressedReadCallback`（底层 `DataClientPool`）直连取数。失败按 `ReadError`
   分类决策（见下「ReadError 分类与重试策略」）。一轮全失败后退避再轮询。
3. **Tier 3 (位置查询)**: `RemoteCompressedReadCallback` 向 master 查询全部副本，
   回填本地 `remote_idx` 后**重入 TIER2**。master 无位置时返回 `can_still_produce` 终止。

### ReadError 分类与重试策略

底层 `DataClientPool::request` 对失败做**单次**请求（不再内部轮询 DATA_NOT_READY），
返回 `ReadError` 枚举驱动 TIER2 重试决策：

| ReadError | 含义 | TIER2 策略 |
|-----------|------|-----------|
| `NONE` | 成功 | 返回数据 |
| `DATA_NOT_READY` | 对方正在写该对象（瞬时） | 保留副本，**无限重试**（数据可期，不可主动失败） |
| `OBJECT_NOT_FOUND` | 该副本不再持有此对象（永久） | `remove_remote_location` 删该副本 |
| `NETWORK` | 连接/超时/协议错误（瞬时） | 保留副本，有限重试（30s deadline，永久不可达不无限重试） |
| `SHUTDOWN` | pool 被停止 | 立即终止 |

**退避参数**：初始 10ms，每次 ×2，上限 500ms，每次 ±10% 随机抖动（`thread_local
std::mt19937`，避免请求风暴）。`DATA_NOT_READY` 存在时本轮不受 deadline 限制。

### Temp 数据管理

- temp 数据统一由 DataService 管理（`local_idx_` 中 `temp_compressed_data_` 字段 + LRU + `temp_eviction_store_` 磁盘溢出层）。Database 不自持 TempStore
- temp 数据存储为 `FlyBufferPtr`（shared_ptr），实现零拷贝读取
- LRU 淘汰策略：超过内存限制（`temp_store_size`，默认 2GB）时淘汰最久未访问的 temp 数据
- 淘汰的数据溢出到 `DataService::temp_eviction_store_`（磁盘），读取路径（`try_read_local*`）在 `temp_compressed_data_` 已 reset 时回退到该层
- `remove_local_index` / `cleanup_temp_entries` 会完整清理 temp 数据（local_idx 条目 + LRU 队列 + eviction store）

### 远程索引更新时机

- WriteRegister 时：Master 更新 remote_idx
- TIER3 位置查询成功后：Worker 回填全部副本到 remote_idx
- **任务分配时预取**：`on_task_assign` 收到 `TaskAssignMessage.dependency_locations_`
  （已是多副本 `CMVector<DataLocation>`）时**直接全部回填** remote_idx，使首轮读命中
  TIER2 而非落 TIER3。预取数据与持久索引统一为单一数据源（不再有 `prefetched_locations_`
  临时缓存）。

---

## DataServer

### 核心职责

响应其他 Worker 的数据请求，采用 epoll + send_thread_pool 模式。

### 架构

- **epoll 线程池**: 接收连接和请求，按消息类型 dispatch（`MessageProtocol::get_type`）
- **send 线程池**: 执行实际数据发送，避免阻塞 epoll 线程
- **SendTask 队列**: epoll 线程提交发送任务，send 线程消费

### 消息 dispatch

数据面承载两类消息（按帧头 type 字节路由）：

- **DATA_REQUEST**：现有读流程。查 `DataService.try_read_local_raw()` 取数，`DataResponseProtocol` 两段式编码，提交 SendTask。
- **NET_PROBE_REQUEST**（网络感知读优先级）：按请求的 `payload_size_` 生成 dummy payload，编码 `NetProbeResponseMessage`，提交 SendTask。供对端的 BandwidthProbeThread 测往返带宽。

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

### 同一 worker 内连续写入的顺序保证

`write_object`（含 `save_to_db=False` 的 temp 写入）对 master 的 WriteRegister 是
**同步往返**：worker 发出 WriteRegister 后在条件变量上阻塞，直到收到 master 的 ack
才返回（`WorkerAgent::request_write_register` → `on_write_register_ack`）。因此同一
worker 内连续多个 `write_object` 调用，WriteRegister 到达 master 的顺序与调用顺序
严格一致（同一连接、串行发起）。

**在默认 stream 模式下**（`dependency_update_mode==0`），master 在 `do_write_register`
处理 WriteRegister 时**立即** `mark_data_ready` + `update_remote_idx`（见上文「写入流程」
时序约束）。结合同步往返 ack，得到以下保证：worker 串行写入 `A → B → C` 时，worker
收到 `C` 的 ack 即意味着 master 已依次对 `A`、`B`、`C` 完成 `mark_data_ready`——三者
对读取均已可见。

**推论（read-after-ready 不竞态）**：`@wait_obj` 只需等待序列中**最后一个**对象，
其函数体内读取前置伴随对象是安全的。因为「最后一个对象就绪」蕴含「其前序对象早已
注册可读」，无需把所有伴随对象都列入 `@wait_obj` 的 inputs。

**反模式（曾导致 solver QA flaky）**：让 `@wait_obj` 等待序列中**非最后**的对象，
然后在函数体内读取**在其之后**才写入的伴随对象。例如 RAS solver 收尾时
`ras_check` 串行写出 `__ras__sol → __ras__final_res → __ras__iters → __ras__ok`，
旧代码 `@wait_obj` 只等 `__ras__sol`，解除阻塞后立即读 `__ras__final_res/iters/ok`，
而这些对象的 WriteRegister 尚未到达 master → `read_object` 对空数据解 pickle
抛 `EOFError`。修复：`@wait_obj` 改为等待最后的 `__ras__ok`，前三个伴随对象此时
必然已注册可读。详见 `docs/DOC_CHANGELOG.md`。

> **非 stream 模式**（`dependency_update_mode!=0`）下，可见性登记（`mark_data_ready`
> 等）不在 WriteRegister 时即时完成，而是延迟到 `on_task_complete` 对该 task 的
> `written_objects_` 统一处理（task 级原子性）。该模式下的可见性语义由 task 完成
> 消息驱动，不依赖上述「ack 即可见」推论。

---

## 设计决策

| 决策 | 原因 |
|------|------|
| 流式序列化+压缩管线 | 避免中间 buffer 拷贝 |
| 三级 fallback 读取 | 本地优先，减少网络 IO |
| 多副本轮询 + 分类重试 | TIER2 遍历全部副本，按 ReadError 区分瞬时/永久错误决定保留/删除，提升容错 |
| 指数退避 + 抖动 | 避免请求风暴，DATA_NOT_READY 可期则无限重试，NETWORK 永久不可达则有限重试 |
| 两层 LRU 缓存 | low 层省 IO，high 层省反序列化 |
| 零拷贝 shared_ptr | temp 数据和远程读避免拷贝 |
| writev scatter-gather | 合并 header+payload 发送 |
| 注册在压缩之后 | 确保数据可读后才通知 master |
