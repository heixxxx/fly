# Storage 模块 — 存储层

## 模块概述

**位置**: `src/storage/`

存储层是 Fly 框架的核心数据管理模块，负责数据的写入、聚合、索引管理、读取（本地 + 远程）、压缩和数据库生命周期管理。设计为 Master 和 Worker 共用的统一层。

---

## 核心文件

| 文件 | 说明 |
|------|------|
| `cpp/database.h/cpp` | 统一存储接口，流式序列化+压缩，异步写入 |
| `cpp/data_writer.h/cpp` | 纯落盘写入聚合器（不持有压缩配置） |
| `cpp/data_reader.h/cpp` | 纯读取字节流（不碰压缩/反序列化） |
| `cpp/compressing_streambuf.h` | 流式压缩 streambuf（分块自动 flush） |
| `cpp/decompressing_streambuf.h/cpp` | 流式解压 streambuf（自动解析 ObjectHeader + 逐 chunk 解压） |
| `cpp/fly_buffer_stream.h` | FlyBufferStreamBuf（streambuf→FlyBuffer）+ CountingStreamBuf |
| `cpp/data_service.h/cpp` | 统一内存索引：local_idx + remote_idx + db_paths_ + worker_registry |
| `cpp/data_server.h/cpp` | epoll + send_thread_pool 数据服务（响应远程 Worker 数据请求） |
| `cpp/object_cache.h` | 两层 LRU 读缓存（low=压缩字节, high=反序列化对象） |
| `cpp/storage_manager.h/cpp` | Database 生命周期管理，单例 |
| `cpp/local_index.h/cpp` | 本地索引持久化（.idx 文件） |
| `cpp/index_entry.h` | 索引条目结构 |
| `cpp/compressor.h/cpp` | 压缩接口（虚函数 + 工厂） |
| `cpp/write_back_queue.h/cpp` | 异步写入队列 |
| `export/storage_export.cpp` | Python 导出 |

---

## 类详细说明

### Database（统一存储接口）

```cpp
class Database {
public:
    Database(const CMString& base_path,
             const CMString& data_path = "",
             uint64_t writer_id = 0,
             const CMString& host = "",
             const CMString& existing_db_id = "");
    ~Database();  // 析构时 unregister_database + drain_write_back

    // C++ 类型写入（流式序列化 + 压缩 + 异步落盘）
    template<typename T>
    WriteErrorType write_object(const CMString& object_name, const T& obj,
                                const CMString& py_name, bool backup = false);

    // Python pickle bytes 写入（压缩 + 异步落盘）
    WriteErrorType write_pickle_bytes(const CMString& object_name,
                                      const char* data, int64_t data_size,
                                      const CMString& py_name, bool backup = false);

    // 读取压缩数据（返回原始磁盘字节 + py_name）
    // bypass_cache=true 跳过 low 层缓存查询（cache="none" 模式）
    // backup=true 时，即使 low 层缓存命中也会检查 has_local_object：
    // 若本地无持久副本则触发 do_backup_write（缓存是内存态，不代表落盘）
    std::pair<FlyBufferPtr, CMString> read_object_compressed(const CMString& object_name,
                                                              bool backup = false,
                                                              bool bypass_cache = false);

    // C++ 类型读取（解压 + 流式反序列化，带缓存）
    // cache: "low"(默认)/"high" 查/填 high 层（省反序列化），"none" 完全 bypass
    template<typename T>
    CMSharedPtr<T> read_object(const CMString& object_name, const CMString& cache = "low");

    // 备份（跳过 check_frozen，直接压缩传输落盘）
    void backup_object(const CMString& object_name);

    // 删除
    void remove_object(const CMString& object_name);

    // 生命周期
    void freeze();
    bool is_frozen() const;
    DbMeta load_meta() const;
    CMString get_db_id() const;
    void set_db_id(const CMString& db_id);
    void reset();

    // 路径信息
    CMString get_base_path() const;
    CMString get_data_path() const;
    CMString get_obj_name(const CMString& name) const;
};
```

**Python 侧写路径**（`database.py`）:
```
write_object(name, obj)
  ├─ hasattr(obj, "_write_to_db") → C++ 路径: write_object<T>（流式序列化+压缩）
  └─ else → pickle.dumps → _write_pickle_bytes → write_object_raw_ptr（压缩）
```

**Python 侧读路径**（`database.py`）:
```
read_object(name, backup, cache)
  ├─ C++ 类型（nanobind 导出）→ _read_from_db → read_object<T>
  │   └─ high cache hit → 直接返回（跳过 IO + 反序列化）
  │   └─ miss → read_object_compressed → decompress + deserialize → put_high
  └─ Python 类型
      ├─ cache="high" → Python ReadCache → miss → _read_decompressed → pickle.loads
      └─ cache="low"  → _read_decompressed → pickle.loads

read_object_compressed（两条路径的公共 IO + 缓存 + backup 逻辑）:
  → low cache hit → 直接返回（跳过远程 IO）
  → miss → read_raw_compressed → Tier 1 本地 / Tier 2 直连 / Tier 3 Master 代理
  → backup 检查 → do_backup_write
  → put_low（填充 low cache）
```

---

### 写入流程（流式序列化+压缩管线 + 异步落盘）

**核心设计**: Database 统一负责流式序列化+压缩，DataWriter 纯落盘。CPU 密集操作在调用线程完成，WBQ 后台线程仅负责磁盘 I/O。

**时序保证**: 注册（通知 master）在压缩+缓存填充之后，确保 master 标记数据就绪时，远程读可立即从 cache 命中。

```
write_object<T>(name, obj, py_name)  ← 调用线程
  │
  ├─ 1. FlyBufferStreamBuf → CountingStreamBuf → CompressingStreamBuf
  │     → obj.fly_serialize(os) 直接流式写入压缩管线
  │     → 输出：ObjectHeader + 分块压缩数据（完整磁盘格式）
  │     → 无中间 buffer 拷贝
  │
  ├─ 2. ObjectCache.put_low(full, record)  ← 立即填充 low cache
  │     → 远程读（DataServer.try_read_local_raw）可直接命中
  │
  ├─ 3. DataService.on_write_started(db_id, full_name)
  │     → 添加 local_idx 条目，状态 INCOMPLETE
  │
  ├─ 4. WorkerAgentContext.register_write(db_id, name)
  │     → 发送 WriteRegisterMessage → Master
  │     → Master 标记数据就绪 → 调度依赖任务
  │     → 此时远程读已可从 cache 命中，无需等待落盘
  │     → 失败时回滚：ObjectCache.remove + on_write_failed
  │
  ├─ 5. enqueue_write_back(req)  ────→  WBQ 后台线程
  │     execute: write_record() + flush()    │
  │     complete: on_write_completed()       │→ file_stream_.write(record)
  │              + caller_record_func()       │→ index 更新
  │     → 状态变更为 COMPLETE（此时数据可读）
  │
  └─ 6. 返回 WriteErrorType::OK（立即返回）
```

**Temp 写入路径**（`save_to_db=False`，全链路零拷贝）：

Python pickle 对象走 `write_temp_pickle`（C++ 侧一步完成压缩+注册+存储，无 Python 往返）：

```
write_object(name, obj, save_to_db=False)
  └─ _write_temp(name, obj)
       └─ pickle.dumps(obj) → data
       └─ db._write_temp_pickle(name, data, py_name)  ← C++ 侧完成
            └─ compress_buffered_data → FlyBufferPtr (直接压缩到 shared_ptr)
            └─ put_temp_data(ptr) → on_temp_write_started + register_write + on_temp_write(ptr)
                └─ local_idx[db_id][short_name].temp_compressed_data_ = ptr  (shared_ptr 透传，零拷贝)
                └─ 溢出淘汰时：temp_eviction_store_->put() 才拷贝一次（淘汰是低频路径）
```

C++ 类型对象走 `_write_to_db` + `_mark_temp`（序列化在 C++ 侧完成，无需 Python 压缩）。

与正常写入的区别：无 disk write（`commit_write`），无 low cache（`put_low`），数据仅存在于 temp 缓存（`local_idx->temp_compressed_data_` 为 `FlyBufferPtr` 共享指针 + 溢出 `temp_eviction_store_`）。写入和读取路径均为零拷贝。

**流式管线组件**:
| 组件 | 职责 |
|------|------|
| `FlyBufferStreamBuf` | `std::streambuf` → FlyBuffer 适配器，`xsputn` 直接 append |
| `CountingStreamBuf` | 包装 streambuf 并统计写入字节数（用于 `ObjectHeader.total_size`） |
| `CompressingStreamBuf` | 分块压缩，达到 `serialize_chunk_size` 时自动 flush chunk |

**Python pickle 路径**: `pickle.dumps(obj)` → `_write_pickle_bytes` 传裸指针给 `compress_buffered_data`。

**回调模式说明**:

`WorkerAgentContext` 使用 `std::function` 回调实现解耦：

```
Database.write_object<T>()
  → WorkerAgentContext::register_write()
    → register_func_(db_id, name)
    → lambda → WorkerAgent::register_write_with_master()

  → 异步完成时 (complete lambda)
     → caller_record_func(...)
     → lambda → WorkerAgent::record_write()
```

---

### DataWriter（纯落盘写入聚合器）

DataWriter 不持有任何压缩配置，仅负责将预压缩的 FlyBuffer 写入磁盘文件并维护索引。

```cpp
class DataWriter {
public:
    DataWriter(const CMString& base_path, const CMString& data_path,
               const CMString& writer_id, int64_t aggregation_threshold,
               const CMString& host = "");

    // 纯落盘（数据已由 Database 层压缩完毕）
    void write_record(const CMString& object_name, int64_t original_size,
                      int32_t chunk_count, const FlyBuffer& record);

    void flush();
    void close();

    IndexEntry* get_last_entry(const CMString& object_name);
    CMVector<IndexEntry>* get_all_entries(const CMString& object_name);
    bool remove_entry(const CMString& object_name);

    int64_t total_bytes_written() const;
    int32_t file_count() const;
};
```

**写入策略**:
- 所有对象统一聚合写入 `.dat` 文件
- 文件超过 `aggregation_threshold` 时滚动到新文件
- 不区分大小文件，全部使用统一的 `[ObjectHeader][Chunks]` 磁盘格式

---

### DataReader（纯读取字节流）

DataReader 不碰压缩/反序列化，仅负责从磁盘文件读取原始字节。

```cpp
class DataReader {
public:
    DataReader(const CMString& base_path, const CMString& data_path,
               const CMString& writer_id);

    // 读取原始压缩字节（[ObjectHeader][Chunks]）
    CMString read_raw_bytes(const CMString& object_name) const;

    // 检查对象是否存在
    bool exists(const CMString& object_name) const;

    // 索引访问
    IndexEntry* find_entry(const CMString& object_name) const;
    CMVector<IndexEntry>* find_all_entries(const CMString& object_name) const;
};
```

**注意**: 解压和反序列化由 `DecompressingStreamBuf` 和 `fly_deserialize()` 在 Database/DataService 层完成。

---

### ObjectCache（两层 LRU 读缓存）

进程级单例，实现两层 LRU 读缓存，加速 `read_object` 路径。

```cpp
class ObjectCache {
public:
    static ObjectCache& instance();

    // High 层：反序列化对象（std::any 持 CMSharedPtr<T>）
    template<typename T>
    CMSharedPtr<T> get_high(const CMString& key);

    template<typename T>
    void put_high(const CMString& key, const CMSharedPtr<T>& obj, size_t size);

    // Low 层：压缩字节（FlyBufferPtr shared_ptr，零拷贝共享）
    std::pair<bool, FlyBufferPtr> get_low(const CMString& key);

    void put_low(const CMString& key, const FlyBufferPtr& data, size_t size);

    // 失效（双层清理）
    void remove(const CMString& key);
    void clear();

    // 统计
    struct Stats {
        std::atomic<uint64_t> low_hits, low_misses, low_puts, low_evictions;
        std::atomic<uint64_t> high_hits, high_misses, high_puts, high_evictions;
    };
    const Stats& stats() const;
    double low_hit_rate() const;
    double high_hit_rate() const;

    // 测试辅助
    void reset_for_test(size_t max_bytes = 0);
};
```

**缓存分层**:

| 层 | 存储内容 | 命中收益 | 填充时机 |
|----|---------|---------|---------|
| **low** | 压缩字节 (`FlyBufferPtr` shared_ptr) | 省磁盘/远程 IO | write_object complete_ (write-through) |
| **high** | 反序列化对象 (`std::any` 持 `CMSharedPtr<T>`) | 省反序列化 | C++ `read_object<T>` 命中后 |

**淘汰策略**:
- LFU score = `read_count / (now - last_access)`
- 30s 保护期：新创建的条目不会被立即淘汰
- 1.5× 硬限制：超过 `max_bytes * 1.5` 时强制淘汰
- 淘汰最低 score 的条目，直到低于 `max_bytes`

**线程安全**: 所有写操作通过 `mutex_` 保护，统计计数使用 `std::atomic` 支持无锁读取。

**失效触发**: `remove_object` / `remove_local_index` / `remove_remote_index` 均调用 `cache.remove(key)`，双层清理。

---

### DataService（统一内存索引）

```cpp
class DataService {
public:
    static CMSharedPtr<DataService> instance();

    // 本地索引
    void on_write_started(const CMString& db_id, const CMString& object_name);
    void on_write_completed(const CMString& db_id, const CMString& object_name,
                             const CMVector<IndexEntry>& entries);
    void on_write_failed(const CMString& db_id, const CMString& object_name,
                         const CMString& error_message);
    void on_object_flushed(const CMString& object_name);

    // Temp 数据写入（save_to_db=False）
    void on_temp_write_started(const CMString& db_id, const CMString& object_name);
    void on_temp_write(const CMString& db_id, const CMString& object_name,
                       FlyBufferPtr data);  // shared_ptr，零拷贝

    // 读取（COMPLETE = 可读，不论 save_to_db 与否）
    std::pair<bool, ReadResult> try_read_local(const CMString& object_name);
    std::pair<bool, ReadResult> try_read_local_or_wait(const CMString& object_name,
                                                       int timeout_ms = 3000);
    ReadResult read_raw(const CMString& object_name, int max_retries = 3);

    // 远程索引短路（DataServer 使用）
    FlyBufferPtr try_read_local_raw(const CMString& object_name);

    bool has_local_object(const CMString& object_name) const;
    void remove_local_index(const CMString& object_name);

    // 远程索引
    void update_remote_idx(const CMString& object_name, uint64_t worker_id,
                           const CMString& host, int32_t port);
    RemoteObjectInfo lookup_remote_idx(const CMString& object_name) const;
    bool has_remote_location(const CMString& object_name) const;
    void remove_remote_index(const CMString& object_name);

    // Worker 注册
    void register_worker(uint64_t worker_id, const CMString& host, int32_t port);
    RemoteObjectInfo get_worker_address(uint64_t worker_id) const;

    // DB 管理
    void register_database(const CMString& db_id, const CMString& base_path,
                           const CMString& data_path, uint64_t writer_id = 0);
    void unregister_database(const CMString& db_id);
    bool has_database(const CMString& db_id) const;

    // 索引恢复 (load_db)
    void restore_entries(const CMString& db_id, const CMVector<IndexEntry>& entries);

    // WriteBackQueue
    void start_write_back();
    void stop_write_back();
    void enqueue_write_back(WriteRequest&& task);
    void drain_write_back();

    // 传输服务器（DataServer）
    void start_transfer_server(int thread_count, TransferCallback callback);
    void stop_transfer_server();
    bool is_transfer_server_running() const;
    void submit_transfer(uint64_t conn_id, const CMString& object_name,
                         uint64_t requesting_worker_id, uint64_t request_id);

    // 自动备份访问频率追踪
    void record_remote_access(const CMString& object_name);
    BackupDecision evaluate_auto_backup(const CMString& object_name,
                                        uint64_t threshold,
                                        uint32_t target_replicas) const;
    void decay_remote_access(int64_t protection_seconds, int decay_factor_percent);
};
```

**数据结构**:
```
DataService (单例)
├── local_idx: db_id → (short_name → LocalObjectInfo)
│   └── LocalObjectInfo:
│       ├── entries: CMVector<IndexEntry>
│       ├── flushed: bool
│       ├── completion_state: INCOMPLETE / COMPLETE / FAILED
│       │   └── COMPLETE = 可读（统一语义）
│       ├── is_temp: bool
│       ├── temp_compressed_data: FlyBufferPtr  (shared_ptr, 零拷贝读取)
│       ├── cv: condition_variable
│       └── cv_mutex: mutex
│
├── remote_idx: db_id → (short_name → RemoteObjectMeta)
│   └── RemoteObjectMeta:
│       ├── workers: CMVector<uint64_t>
│       ├── read_count: uint64_t
│       └── last_access_time: int64_t
│
├── worker_registry: worker_id → RemoteObjectInfo
│
└── db_paths: db_id → {base_path, data_path, writer_id}
```

---

### DataServer（epoll + send_thread_pool 数据服务）

响应其他 Worker 的数据请求，采用 epoll + send_thread_pool 模式。

```cpp
class DataServer {
public:
    DataServer(DataService& service, CMSharedPtr<Transport> transport,
               CMSharedPtr<EpollMultiplexer> epoll, int thread_count);
    DataServer(DataService& service, int thread_count);  // 便捷构造

    void start(const CMString& host, int32_t port);
    void stop();
    int32_t get_port() const;

private:
    struct ConnState {
        int fd;
        CMString recv_buf;
    };

    struct SendTask {
        int fd;
        CMString data;
        FlyBufferPtr raw_data;  // 零拷贝 raw payload
    };

    DataService& service_;
    CMSharedPtr<Transport> transport_;
    CMSharedPtr<EpollMultiplexer> epoll_;
    int epoll_thread_count_;
    int send_thread_count_;
    std::atomic<bool> running_{false};

    // epoll 线程处理连接和请求
    CMVector<std::thread> epoll_threads_;

    // send 线程池处理数据发送
    CMVector<std::thread> send_threads_;
    std::queue<SendTask> send_queue_;
    std::mutex send_mutex_;
    std::condition_variable send_cv_;
};
```

**工作流程**:
1. epoll 线程接收 `DataRequestMessage`
2. 查询 `DataService.try_read_local_raw()` 短路本地数据
3. 使用 `DataResponseProtocol::encode()` 两段式编码
4. 提交 `SendTask` 到 send_queue
5. send 线程执行实际发送

**零拷贝路径**: `SendTask.raw_data` 指向 `FlyBufferPtr`（ObjectCache.low 的共享引用），避免数据拷贝。

**sendv 优化**: 当有 raw payload 时，使用 `writev` 系统调用将 header 和 payload 合并为一次发送，避免两次 `send` 的系统调用开销：

```cpp
// DataServer::send_loop()
if (has_raw) {
    struct iovec iov[2];
    iov[0] = {header.data(), header.size()};
    iov[1] = {raw_data->data(), raw_data->size()};
    transport_->sendv(fd, iov, 2);  // 单次 writev
} else {
    do_send(fd, header);  // 无 payload 时单次 send
}
```

---

### IndexEntry（索引条目）

```cpp
struct IndexEntry {
    CMString object_name;
    CMString file_name;
    int64_t offset;
    int64_t size;
    bool is_large;
    int32_t block_count;
    CMString host;

    FLY_SERIALIZE(object_name, file_name, offset, size, is_large, block_count, host);
};
```

---

### RemoteObjectMeta（远程对象元数据）

```cpp
struct RemoteObjectMeta {
    CMVector<uint64_t> workers;
    uint64_t read_count = 0;
    int64_t last_access_time = 0;   // epoch seconds
};

struct BackupDecision {
    uint64_t read_count = 0;       // 跨 Worker 读取次数（evaluate 时的快照）
    bool should_backup = false;
    uint32_t current_replicas = 0;
    uint32_t target_replicas = 0;
};
```

**说明**：`RemoteObjectMeta` 合并了 worker 列表和访问频率追踪数据。`current_replicas` 直接取 `workers.size()`。`read_count` 从 `RemoteObjectMeta.read_count_` 快照而来，供调用方记录日志。

---

### Compressor（压缩接口）

```cpp
enum class CompressionType { NONE = 0, LZ4 = 1, ZLIB = 2, ZSTD = 3 };

class Compressor {
public:
    virtual ~Compressor() = default;

    virtual CompressedChunk compress(const CMString& input) = 0;
    virtual CMString decompress(int32_t uncompressed_size,
                                const CMString& compressed_data) = 0;
    virtual CompressedChunk compress_chunk(const CMString& input) = 0;
    virtual CMString decompress_chunk(...) = 0;

    virtual CompressionType type() const = 0;
    virtual CMString name() const = 0;
};

class CompressorFactory {
public:
    static CMUniquePtr<Compressor> create(CompressionType type);
    static CMUniquePtr<Compressor> create_from_name(const CMString& name);
};
```

---

## 读取流程（三层降级 + 缓存）

```
read_object<T>("key")
  │
  ├─ 0. ObjectCache.get_high<T>(key)
  │     └── 命中 → 直接返回 CMSharedPtr<T>，省反序列化
  │
  ├─ 1. ObjectCache.get_low(key)
  │     └── 命中 → DecompressingStreamBuf → fly_deserialize → 返回
  │
  ├─ 2. read_object_compressed(key) → (FlyBufferPtr, py_name)
  │     └── DataService → DataReader.read_raw_bytes → 原始磁盘字节
  │     └── 填充 ObjectCache.low (write-through)
  │
  ├─ 3. DecompressingStreamBuf(compressed_data)
  │     └── 自动解析 ObjectHeader + 逐 chunk 解压
  │
  └─ 4. obj.fly_deserialize(is)
        └── bitsery 流式反序列化
        └── 填充 ObjectCache.high
```

**远程降级**（`read_raw_compressed`，单次尝试，不重试）:
```
try_read_local_raw(key)            → Tier 1: local_idx + ObjectCache.low + temp 缓存
lookup_remote_idx(key)             → Tier 2: remote_idx → DataClient 直连目标 Worker
remote_compressed_read_handler_(key) → Tier 3: agent 层兜底回调（Master 端直接查 remote_idx）
```

Tier 2 和 Tier 3 的区别：Tier 2 是 DataService 内置的 remote_idx 查找 + DataClient 直连；Tier 3 是 agent 层注入的回调，Master 端直接查本地 remote_idx（不走 reactor，避免 epoll 跨 fd 顺序不确定），Worker 端则通过网络查询 Master。

Tier 3 返回 `(found, data, py_name, can_still_produce)`。`can_still_produce=true` 表示有 pending/running task 可能产出该数据，调用方可重试。扩展等待由 Python 层 `wait_obj` 负责轮询。

---

## 序列化宏

`FLY_SERIALIZE(...)` 自动生成三个函数：
- `serialize(S& s)` — bitsery 通用序列化接口（字段声明）
- `fly_serialize(std::ostream&)` — 流式编码到 ostream（用于压缩管线）
- `fly_deserialize(std::istream&)` — 从 istream 流式解码（用于解压管线）

`FLY_EXPORT_SERIALIZE(ClassName)` 在 `FLY_SERIALIZE` 基础上额外生成：
- `_write_to_db` — Python 可调用，直接走 `Database::write_object<T>`
- `is_cpp` — 属性标记，标识 C++ 导出类型
- `__getstate__` / `__setstate__` — Python pickle 支持

---

## 设计决策

| 决策 | 原因 |
|------|------|
| Database 统一压缩，DataWriter 纯落盘 | 压缩配置集中管理，DataWriter 不持有压缩状态 |
| Database 统一解压，DataReader 纯读 | 读取路径不碰压缩/反序列化，职责单一 |
| 调用线程序列化+压缩，WBQ 仅落盘 | CPU 密集操作不阻塞 WBQ，单线程足以应对磁盘带宽 |
| 流式管线（FlyBufferStreamBuf + CompressingStreamBuf） | 零中间拷贝，峰值内存 = chunk_size + compressed_size |
| ObjectCache 两层缓存 | low=压缩字节省 IO，high=反序列化对象省 CPU |
| FlyBufferPtr 共享所有权 | 零拷贝共享压缩字节，避免不必要的内存拷贝 |
| DataServer epoll + send_thread_pool | 高并发数据服务，避免阻塞 Reactor |
| WriteBackQueue 异步写入 | 文件 I/O 非阻塞，避免写入阻塞任务执行 |
| 回调模式解耦 | Database 不依赖 WorkerAgent，std::function 桥接 |
| DataService 进程级单例 | Master/Worker 共享，instance() 返回 CMSharedPtr |
| COMPLETE = 可读（统一语义） | 不论 save_to_db 与否，completion_state==COMPLETE 即可读 |
| read_raw_compressed 单次尝试 | C++ 不重试，返回 can_still_produce 状态，扩展等待由 Python wait_obj 处理 |
| Tier 3 回调直接查 remote_idx | 避免 reactor epoll 跨 fd 顺序不确定导致的 race |
| temp_compressed_data_ 用 FlyBufferPtr | shared_ptr 零拷贝读取，避免每次读都全量拷贝压缩数据 |

---

*文档更新日期: 2026-06-19*
