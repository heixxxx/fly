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
    CMString write_object(const CMString& object_name, const T& obj,
                          const CMString& py_name, bool backup = false);

    // Python pickle bytes 写入（压缩 + 异步落盘）
    CMString write_pickle_bytes(const CMString& object_name,
                                const char* data, int64_t data_size,
                                const CMString& py_name, bool backup = false);

    // 读取压缩数据（返回原始磁盘字节 + py_name）
    std::pair<CMString, CMString> read_object_compressed(const CMString& object_name, bool backup = false);

    // C++ 类型读取（解压 + 流式反序列化）
    template<typename T>
    CMSharedPtr<T> read_object(const CMString& object_name);

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

private:
    CMString base_path_;
    CMString data_path_;
    CMString writer_id_;
    CMString db_id_;
    CMString host_;
    bool is_frozen_ = false;
    CompressionType compression_type_;
    int compression_level_;
    int64_t serialize_chunk_size_;
    CMSet<CMString> removed_objects_;
    
    CMUniquePtr<DataWriter> writer_;
    CMUniquePtr<DataReader> reader_;
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
read_object(name)
  → _read_streaming → read_object_compressed → (compressed_data, py_name)
  → _reconstruct(data, py_name)
    ├─ C++ 类型 → __setstate__(data)
    └─ Python 类型 → _decompress_bytes → pickle.loads
```

---

### 写入流程（流式序列化+压缩管线 + 异步落盘）

**核心设计**: Database 统一负责流式序列化+压缩，DataWriter 纯落盘。CPU 密集操作在调用线程完成，WBQ 后台线程仅负责磁盘 I/O。

#### save_to_db=True（持久化写入）

三步写入流程：添加 local idx (INCOMPLETE) → 注册 Master → 后台落盘 → COMPLETE

```
write_object<T>(name, obj, py_name)  ← 调用线程
  │
  ├─ 1. FlyBufferStreamBuf → CountingStreamBuf → CompressingStreamBuf
  │     → obj.fly_serialize(os) 直接流式写入压缩管线
  │     → 输出：ObjectHeader + 分块压缩数据（完整磁盘格式）
  │
  ├─ 2. DataService.on_write_started(db_id, full_name)
  │     → 添加 local_idx 条目，状态 INCOMPLETE
  │     → Master 可发现数据（但尚不可读）
  │
  ├─ 3. WorkerAgentContext.register_write(db_id, name)
  │     → 发送 WriteRegisterMessage → Master
  │
  ├─ 4. enqueue_write_back(req)  ────→  WBQ 后台线程
  │     execute: write_record() + flush()    │
  │     complete: on_write_completed()       │→ file_stream_.write(record)
  │              + caller_record_func()       │→ index 更新
  │              + flushed=true               │→ flush
  │     → 状态变更为 COMPLETE（此时数据可读）
  │
  └─ 5. 返回 "" （立即返回）
```

#### save_to_db=False（临时数据，内存存储）

三步写入流程：添加 local idx (INCOMPLETE) → 注册 Master → 放入内存 → COMPLETE

```
put_temp_data(name, data)  ← 调用线程
  │
  ├─ 1. DataService.on_temp_write_started(db_id, full_name)
  │     → 添加 local_idx 条目，状态 INCOMPLETE，is_temp=true
  │
  ├─ 2. WorkerAgentContext.register_write(db_id, name)
  │     → 发送 WriteRegisterMessage → Master
  │
  ├─ 3. DataService.on_temp_write(db_id, full_name, data)
  │     → 将数据存入内存 temp 缓存
  │     → 状态变更为 COMPLETE（此时数据可读）
  │
  └─ 4. 返回
```

**关键语义**: COMPLETE = 可读。无论 save_to_db 与否，只要 completion_state == COMPLETE，其他 worker 即可读取。

**回调模式说明**:
write_object<T>(name, obj, py_name)  ← 调用线程（C++ 类型）
  │
  ├─ 1. FlyBufferStreamBuf → CountingStreamBuf → CompressingStreamBuf
  │     → obj.fly_serialize(os) 直接流式写入压缩管线
  │     → 输出：ObjectHeader + 分块压缩数据（完整磁盘格式）
  │     → 无中间 buffer 拷贝
  │
  ├─ 2. DataService.on_write_started(db_id, full_name)
  │
  ├─ 3. WorkerAgentContext.register_write(db_id, name)
  │
  ├─ 4. enqueue_write_back(req)  ────→  WBQ 后台线程
  │     execute: write_record() + flush()    │
  │     complete: on_write_completed()       │→ file_stream_.write(record)
  │              + caller_record_func()       │→ index 更新
  │                                           │→ flush
  └─ 5. 返回 "" （立即返回）

write_pickle_bytes(name, data, size, py_name)  ← 调用线程（Python pickle）
  │
  ├─ 1. compress_buffered_data(data, size, py_name, target)
  │     → ObjectHeader + os.write(data, size) → CompressingStreamBuf → target
  │
  └─ 2~5. 同上
```

**流式管线组件**:
| 组件 | 职责 |
|------|------|
| `FlyBufferStreamBuf` | `std::streambuf` → FlyBuffer 适配器，`xsputn` 直接 append |
| `CountingStreamBuf` | 包装 streambuf 并统计写入字节数（用于 `ObjectHeader.total_size`） |
| `CompressingStreamBuf` | 分块压缩，达到 `serialize_chunk_size` 时自动 flush chunk |

**Python pickle 路径**: `pickle.dumps(obj)` → `_write_pickle_bytes` 传裸指针给 `compress_buffered_data`。

**回调模式说明**:

`WorkerAgentContext` 使用 `std::function` 回调实现解耦（`#include <functional>`）：

**调用链**:
```
Database.write_object<T>()
  → WorkerAgentContext::register_write()
    → register_func_(db_id, name)
    → lambda → WorkerAgent::register_write_with_master()
  
  → 异步完成时 (complete lambda)
     → caller_record_func(...)
     → lambda → WorkerAgent::record_write()
```
write_object(name, obj)  ← 调用线程
  │
  ├─ 1. FLY_ENCODE_TO_BYTES(obj, serialized_buf)
  │     → bitsery 直接写入 FlyBuffer（零拷贝）
  │
  ├─ 2. compress_to_buffer(serialized → target FlyBuffer)
  │     → 流式管线：FlyBufferStreamBuf → CompressingStreamBuf → target
  │     → 输出：ObjectHeader + 分块压缩数据（完整线格式）
  │     → 无中间 ostringstream 拷贝
  │
  ├─ 3. DataService.on_write_started(db_id, full_name)
  │
  ├─ 4. WorkerAgentContext.register_write(db_id, name)
  │     → 发送 WriteRegisterMessage → Master
  │
  ├─ 5. enqueue_write_back(req)  ────→  WBQ 后台线程
  │     execute: write_record() + flush()    │
  │     complete: on_write_completed()       │→ file_stream_.write(record)
  │              + caller_record_func()       │→ index 更新
  │                                           │→ flush
  └─ 6. 返回 "" （立即返回）
```

**流式管线组件**:
| 组件 | 职责 |
|------|------|
| `FlyBufferStreamBuf` | `std::streambuf` → FlyBuffer 适配器，`xsputn` 直接 append |
| `CountingStreamBuf` | 包装 streambuf 并统计写入字节数（用于 `ObjectHeader.total_size`） |
| `CompressingStreamBuf` | 分块压缩，达到 `stream_chunk_size` 时自动 flush chunk |

**Python 对象路径**: `pickle.dumps(obj)` → `_write_pickle_bytes` 直接传裸指针给 `compress_to_buffer`，无中间 FlyBuffer 拷贝。

**回调模式说明**:

`WorkerAgentContext` 使用 `std::function` 回调实现解耦（`#include <functional>`）：

**调用链**:
```
Database.write_object()
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
    DataWriter(
        const CMString& base_path,
        const CMString& data_path,
        const CMString& writer_id,
        int64_t aggregation_threshold,
        const CMString& host = ""
    );
    
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

private:
    CMString base_path_;
    CMString data_path_;
    CMString writer_id_;
    CMString host_;
    int64_t aggregation_threshold_;
    CMUniquePtr<LocalIndex> index_;
    std::ofstream file_stream_;
    int32_t file_index_;
    int64_t current_file_size_;
    int64_t total_bytes_;
    bool closed_ = false;
};
```

**写入策略**:
- 所有对象统一聚合写入 `.dat` 文件
- 文件超过 `aggregation_threshold` 时滚动到新文件
- 不区分大小文件，全部使用统一的 `[ObjectHeader][Chunks]` 磁盘格式

**配置项**:
| 键 | 默认值 | 说明 |
|---|--------|------|
| `aggregation_threshold` | 1048576 | 聚合阈值（字节），超过时滚动新文件 |

---

### DataReader（纯读取字节流）

DataReader 不碰压缩/反序列化，仅负责从磁盘文件读取原始字节。

```cpp
class DataReader {
public:
    DataReader(const CMString& base_path, 
               const CMString& data_path, 
               const CMString& writer_id);
    
    // 读取原始压缩字节（[ObjectHeader][Chunks]）
    CMString read_raw_bytes(const CMString& object_name) const;
    
    // 检查对象是否存在
    bool exists(const CMString& object_name) const;

    // 索引访问
    IndexEntry* find_entry(const CMString& object_name) const;
    CMVector<IndexEntry>* find_all_entries(const CMString& object_name) const;

private:
    CMString base_path_;
    CMString data_path_;
    CMString writer_id_;
    CMUniquePtr<LocalIndex> index_;
};
```

**注意**: 解压和反序列化由 `DecompressingStreamBuf` 和 `fly_deserialize()` 在 Database/DataService 层完成。

---

### DataService（统一内存索引）

```cpp
class DataService : public std::enable_shared_from_this<DataService> {
public:
    static DataService& instance();
    static CMSharedPtr<DataService> instance_ptr();  // shared_from_this()
    
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
                       const CMString& data);
    
    // COMPLETE = 可读（统一语义，不论 save_to_db 与否）
    
    std::pair<bool, ReadResult> try_read_local(const CMString& object_name);
    std::pair<bool, ReadResult> try_read_local_or_wait(const CMString& object_name,
                                                          int timeout_ms = 3000);  // -1 = 无限等待
    ReadResult read_raw(const CMString& object_name, int max_retries = 3);
    
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
    // register_database: 同 base_path 不同 db_id → throw
    // Database 析构时自动调用 unregister_database
    void register_database(const CMString& db_id, const CMString& base_path,
                           const CMString& data_path, uint64_t writer_id = 0);
    void unregister_database(const CMString& db_id);
    bool has_database(const CMString& db_id) const;
    
    // 索引恢复 (load_db)
    void restore_entries(const CMString& db_id,
                          const CMVector<IndexEntry>& entries);
    
    // WriteBackQueue
    void start_write_back();
    void stop_write_back();
    void enqueue_write_back(WriteRequest&& task);
    void drain_write_back();
    bool is_write_back_running() const;
    
    // 传输服务器
    void start_transfer_server(int thread_count, TransferCallback callback);
    void stop_transfer_server();
    bool is_transfer_server_running() const;
    void submit_transfer(uint64_t conn_id, const CMString& object_name,
                         uint64_t requesting_worker_id, uint64_t request_id);

    // 自动备份访问频率追踪（内嵌于 remote_idx_）
    void record_remote_access(const CMString& object_name);
    BackupDecision evaluate_auto_backup(const CMString& object_name,
                                         uint64_t threshold,
                                         uint32_t target_replicas) const;
    void decay_remote_access(int64_t protection_seconds, int decay_factor_percent);
    uint64_t get_access_read_count(const CMString& object_name) const;

private:
    struct DbPaths {
        CMString base_path;
        CMString data_path;
        uint64_t writer_id = 0;
    };
    
    // Two-level index: db_id → (short_name → info)
    CMUnorderedMap<CMString, CMUnorderedMap<CMString, CMSharedPtr<LocalObjectInfo>>> local_idx_;
    CMUnorderedMap<CMString, CMUnorderedMap<CMString, RemoteObjectMeta>> remote_idx_;
    CMMap<uint64_t, RemoteObjectInfo> worker_registry_;
    CMUnorderedMap<CMString, DbPaths> db_paths_;
    
    CMSharedPtr<IOThreadPool> transfer_pool_;
    CMUniquePtr<WriteBackQueue> write_back_queue_;
    
    // 传输去重：(requesting_worker_id, object_name, request_id) → bool
    CMSet<std::tuple<uint64_t, CMString, uint64_t>> active_transfers_;
};
```

**数据结构**:
```
DataService (单例)
├── local_idx: db_id → (short_name → LocalObjectInfo)    [两层索引]
│   └── LocalObjectInfo:
│       ├── entries: CMVector<IndexEntry>
│       ├── flushed: bool               // 内部记账标记（save_to_db 时由 WBQ 完成）
│       ├── completion_state: INCOMPLETE / COMPLETE / FAILED
│       │   └── COMPLETE = 可读（统一语义，不论 save_to_db 与否）
│       │       save_to_db=True: WBQ 完成落盘 + flush 后标记
│       │       save_to_db=False: on_temp_write 放入内存后标记
│       ├── cv: condition_variable
│       └── cv_mutex: mutex
│
├── remote_idx: db_id → (short_name → RemoteObjectMeta)  [两层索引]
│   └── RemoteObjectMeta:
│       ├── workers: CMVector<uint64_t>  [持有该对象的 worker_id 列表]
│       ├── read_count: uint64_t         [跨 Worker 读取计数]
│       └── last_access_time: int64_t    [最后访问时间，epoch seconds]
│
├── worker_registry: worker_id → RemoteObjectInfo
│
└── db_paths: db_id → {base_path, data_path, writer_id}
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
    bool should_backup = false;
    uint32_t current_replicas = 0;
    uint32_t target_replicas = 0;
};
```

**说明**：`RemoteObjectMeta` 合并了原 `remote_idx_` 的 worker 列表和访问频率追踪数据，消除了对象名称的重复存储。`current_replicas` 直接取 `workers.size()`，无需手动同步。访问追踪方法（`record_remote_access`、`evaluate_auto_backup`、`decay_remote_access`）直接操作 `remote_idx_` 中的 `RemoteObjectMeta`，所有方法线程安全（通过 DataService 的 mutex_）。

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
    static CompressionType type_from_name(const CMString& name);
    static CMString name_from_type(CompressionType type);
};
```

---

## 读取流程（三层降级）

```
read_object<T>("key")
  │
  ├─ 1. read_object_compressed("key") → (compressed_data, py_name)
  │     └── DataService → DataReader.read_raw_bytes → 原始磁盘字节
  │
  ├─ 2. DecompressingStreamBuf(compressed_data)
  │     └── 自动解析 ObjectHeader + 逐 chunk 解压
  │
  └─ 3. obj.fly_deserialize(is)
        └── bitsery 流式反序列化

read_object("key")  ← Python 侧
  │
  ├─ _read_streaming → read_object_compressed → (compressed_data, py_name)
  │
  └─ _reconstruct(data, py_name)
      ├─ C++ 类型 → __setstate__(data)    [via DecompressingStreamBuf]
      └─ Python 类型 → _decompress_bytes → pickle.loads
```

**远程降级**:
```
DataService.try_read_local(key)     → Layer 1: 内存索引 → 本地读取
DataService.lookup_remote_idx(key)  → Layer 2: DataClient.request_data(host, port, key)
read_raw(key)                       → Layer 3: MetadataClient 查 Master → DataClient 直连
```

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
| DecompressingStreamBuf 自动解析 ObjectHeader | 读取路径无需从 IndexEntry 取压缩类型 |
| `FLY_SERIALIZE` 合并流式能力 | 所有序列化类型自动获得 `fly_serialize`/`fly_deserialize`，无需独立 `FLY_STREAMABLE()` |
| FlyBuffer 统一为 CMString 内部存储 | 消除 char↔uint8_t 阻抗失配，读取路径 `take(std::move(string))` 零拷贝 |
| WriteBackQueue 异步写入 | 文件 I/O 非阻塞，避免写入阻塞任务执行 |
| 回调模式解耦 | Database 不依赖 WorkerAgent，std::function 桥接 |
| DataService 进程级单例（enable_shared_from_this） | Master/Worker 共享，CMWeakPtr 观察者模式安全引用 |
| IOThreadPool 数据传输 | 文件 I/O 不阻塞 Reactor 线程 |
| COMPLETE = 可读（统一语义） | 不论 save_to_db 与否，completion_state==COMPLETE 即可读 |
| 传输去重三元组 | (requesting_worker_id, object_name, request_id) 防止重复大对象传输 |
| 三步写入流程 | INCOMPLETE→注册Master→COMPLETE，确保 Master 可发现数据后再变可读 |