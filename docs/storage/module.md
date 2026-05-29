# Storage 模块 — 存储层

## 模块概述

**位置**: `src/storage/`

存储层是 Fly 框架的核心数据管理模块，负责数据的写入、聚合、索引管理、读取（本地 + 远程）、压缩和数据库生命周期管理。设计为 Master 和 Worker 共用的统一层。

---

## 核心文件

| 文件 | 说明 |
|------|------|
| `cpp/database.h/cpp` | 统一存储接口，异步写入（WriteBackQueue） |
| `cpp/data_writer.h/cpp` | 单线程写入聚合器，流式压缩管线 + 磁盘写入 |
| `cpp/data_reader.h/cpp` | 数据读取器（实例方法） |
| `cpp/fly_buffer_stream.h` | FlyBufferStreamBuf（streambuf→FlyBuffer）+ CountingStreamBuf |
| `cpp/data_service.h/cpp` | 统一内存索引：local_idx + remote_idx + db_paths_ + worker_registry |
| `cpp/storage_manager.h/cpp` | Database 生命周期管理，单例 |
| `cpp/local_index.h/cpp` | 本地索引持久化（.idx 文件） |
| `cpp/index_entry.h` | 索引条目结构（版本 3） |
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
    
    // existing_db_id: 非空时跳过 write_db_meta_header()，使用给定 db_id（用于 load_db 恢复）
    // 空时: generate_db_id() 生成 UUID v4 (32 hex chars)，写入 _DB_META header

    // 异步写入（非阻塞）
    template<typename T>
    CMString write_object(const CMString& object_name, const T& obj,
                          const CMString& py_name = "");

    CMString write_object(const CMString& object_name, const CMString& data,
                          bool backup = false);

    CMString write_object_typed(const CMString& object_name, const CMString& data,
                                 const CMString& py_name);

    // 读取
    template<typename T>
    CMSharedPtr<T> read_object(const CMString& object_name);

    CMString read_object(const CMString& object_name, bool backup = false);

    ReadResult read_object_typed(const CMString& object_name, bool backup = false);

    // 备份（内部使用，跳过 check_frozen，直接压缩传输落盘）
    void persist_read_result(const CMString& object_name, const ReadResult& result);
    void backup_object(const CMString& object_name);

    // 删除
    void remove_object(const CMString& object_name);
    
    // 生命周期
    void freeze();
    bool is_frozen() const;
    DbMeta load_meta() const;
    void write_db_meta_header();  // 写 _DB_META header（构造时自动调用，existing_db_id 非空时跳过）
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
    uint64_t writer_id_ = 0;
    CMString db_id_;
    CMString host_;
    bool is_frozen_ = false;
    CMSet<CMString> removed_objects_;  // 待删除对象集合（freeze时磁盘清理）
    
    CMUniquePtr<DataWriter> writer_;
    CMUniquePtr<DataReader> reader_;
};
```

---

### 写入流程（流式管线 + 异步落盘）

**核心设计**: 序列化和压缩在**调用线程**完成（CPU 密集），WBQ 后台线程**仅负责磁盘 I/O**。

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

`WorkerAgentContext` 使用 C 风格函数指针 + `void*` 上下文实现解耦：

```cpp
// 类型定义（worker_context.h）
using RecordWriteFunc = void(*)(void* ctx, const CMString& db_id, const CMString& name);
using RegisterWriteFunc = void(*)(void* ctx, const CMString& db_id, const CMString& name);

// thread_local 存储
class WorkerAgentContext {
    static inline thread_local RecordWriteFunc func_ = nullptr;
    static inline thread_local void* ctx_ = nullptr;  // 存 WorkerAgent*
};

// Trampoline（静态函数，签名匹配函数指针）
void WorkerAgent::record_write_trampoline(void* ctx, const CMString& db_id, const CMString& name) {
    static_cast<WorkerAgent*>(ctx)->record_write(db_id, name);
}
```

**调用链**:
```
Database.write_object()
  → WorkerAgentContext::register_write()
    → register_func_(ctx_, db_id, name)
    → trampoline → WorkerAgent::register_write_with_master()
  
  → 异步完成时 (complete lambda)
    → caller_record_func(caller_record_ctx, ...)
    → trampoline → WorkerAgent::record_write()
```

---

### DataWriter（写入聚合器）

```cpp
class DataWriter {
public:
    DataWriter(
        const CMString& base_path,
        const CMString& data_path,
        const CMString& writer_id,
        int64_t aggregation_threshold,
        int64_t large_file_threshold,
        int64_t block_size,
        CompressionType compression_type = CompressionType::LZ4,
        int64_t compression_threshold = 128,
        int compression_level = 0,
        int64_t stream_chunk_size = 4194304,
        const CMString& host = ""
    );
    
    void write_typed_object(const CMString& name, int64_t original_size,
                            const CMString& py_name, const char* data, int64_t size);
    
    // 流式压缩（调用线程）
    struct CompressResult { int64_t original_size; int64_t record_size; int32_t chunk_count; };
    CompressResult compress_to_buffer(uint64_t original_size, const CMString& py_name,
                                       const char* data, int64_t data_size, FlyBuffer& target);
    
    // 磁盘写入（WBQ 线程）
    void write_record(const CMString& object_name, int64_t original_size,
                      int32_t chunk_count, const FlyBuffer& record);
    
    void flush();
    void close();
    
    IndexEntry* get_last_entry(const CMString& object_name);
    CMVector<IndexEntry>* get_all_entries(const CMString& object_name);
    
    int64_t total_bytes_written() const;
    int file_count() const;

private:
    CMString base_path_;
    CMString data_path_;
    CMString writer_id_;
    CMString host_;
    int64_t aggregation_threshold_;
    int64_t large_file_threshold_;
    int64_t block_size_;
    CompressionType compression_type_;
    int64_t compression_threshold_;
    int compression_level_;
    int64_t stream_chunk_size_;
    CMUniquePtr<Compressor> compressor_;
    
    CMUniquePtr<LocalIndex> index_;
    std::ofstream file_stream_;
    int32_t file_index_;
    int64_t current_file_size_;
    int64_t total_bytes_;
    bool closed_ = false;
};
```

**写入策略**:
- 小文件 (size < large_file_threshold): 聚合写入 `.dat` 文件
- 大文件 (size >= large_file_threshold): 分块存储，每块 block_size 大小
- 文件超过阈值时滚动到新文件

**配置项**:
| 键 | 默认值 | 说明 |
|---|--------|------|
| `large_file_threshold_kb` | 65536 | 大文件阈值（KB），默认 64MB |
| `block_size` | 134217728 | 大文件分块大小（字节），默认 128MB |
| `aggregation_threshold` | 1048576 | 聚合阈值（字节） |

---

### DataReader（数据读取器）

```cpp
class DataReader {
public:
    DataReader(const CMString& base_path, 
               const CMString& data_path, 
               const CMString& writer_id);
    
    ReadResult read_from_entries(const CMVector<IndexEntry>& entries);
    ReadResult read_object_data(const IndexEntry& entry);
    ReadResult read_object_data(const CMString& object_name);
    
    template<typename T>
    CMSharedPtr<T> read_object(const CMString& object_name);
    
    CMString read_object(const CMString& object_name);
    
    bool exists(const CMString& object_name) const;

private:
    CMString base_path_;
    CMString data_path_;
    CMString writer_id_;
    CMString find_file_path(const CMString& file_name);
};
```

**注意**: 所有方法为**实例方法**（非静态），需构造 DataReader 实例。

---

### DataService（统一内存索引）

```cpp
class DataService {
public:
    static DataService& instance();
    
    // 本地索引
    void on_write_started(const CMString& db_id, const CMString& object_name);
    void on_write_completed(const CMString& db_id, const CMString& object_name,
                             const CMVector<IndexEntry>& entries);
    void on_write_failed(const CMString& db_id, const CMString& object_name,
                         const CMString& error_message);
    void on_object_flushed(const CMString& object_name);
    
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
    void submit_transfer(uint64_t conn_id, const CMString& object_name);

private:
    struct DbPaths {
        CMString base_path;
        CMString data_path;
        uint64_t writer_id = 0;
    };
    
    // Two-level index: db_id → (short_name → info)
    CMUnorderedMap<CMString, CMUnorderedMap<CMString, CMSharedPtr<LocalObjectInfo>>> local_idx_;
    CMUnorderedMap<CMString, CMUnorderedMap<CMString, RemoteObjectInfo>> remote_idx_;
    CMMap<uint64_t, RemoteObjectInfo> worker_registry_;
    CMUnorderedMap<CMString, DbPaths> db_paths_;
    
    CMSharedPtr<IOThreadPool> transfer_pool_;
    CMUniquePtr<WriteBackQueue> write_back_queue_;
};
```

**数据结构**:
```
DataService (单例)
├── local_idx: db_id → (short_name → LocalObjectInfo)    [两层索引]
│   └── LocalObjectInfo:
│       ├── entries: CMVector<IndexEntry>
│       ├── flushed: bool
│       ├── completion_state: INCOMPLETE / COMPLETE / FAILED
│       ├── cv: condition_variable
│       └── cv_mutex: mutex
│
├── remote_idx: db_id → (short_name → RemoteObjectInfo)  [两层索引]
│   └── RemoteObjectInfo:
│       ├── worker_id: uint64_t
│       ├── host: CMString
│       └── port: int32_t
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
    int8_t compression_type;  // 原始值，非枚举
    CMString host;            // v3 新增
    
    FLY_SERIALIZE_BEGIN(3)
        FLY_FIELD(object_name);
        FLY_FIELD(file_name);
        FLY_FIELD(offset);
        FLY_FIELD(size);
        FLY_BOOL(is_large);
        FLY_FIELD(block_count);
        if (version >= 2) {
            FLY_FIELD(compression_type);
        }
        if (version >= 3) {
            FLY_FIELD(host);
        }
    FLY_SERIALIZE_END
};
```

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
read_object("key")
  │
  ├─ Layer 1: DataService.try_read_local(key)
  │     └── 内存索引 local_idx → 找到 + flushed
  │           └── DataReader.read_from_entries()
  │
  ├─ Layer 2: DataService.lookup_remote_idx(key)
  │     └── 有缓存 → DataClient.request_data(host, port, key)
  │
  └─ Layer 3: read_raw(key) (最多 3 次重试)
        ├── MetadataClient::query_data_location() → 问 Master
        └── DataClient.request_data() → 直连目标 Worker
        └── 成功后 update_remote_idx() 缓存
```

---

## 设计决策

| 决策 | 原因 |
|------|------|
| 调用线程序列化+压缩，WBQ 仅落盘 | CPU 密集操作不阻塞 WBQ，单线程足以应对磁盘带宽 |
| 流式管线（FlyBufferStreamBuf + CompressingStreamBuf） | 零中间拷贝，峰值内存 = chunk_size + compressed_size |
| FlyBuffer 统一为 CMString 内部存储 | 消除 char↔uint8_t 阻抗失配，读取路径 `take(std::move(string))` 零拷贝 |
| WriteBackQueue 异步写入 | 文件 I/O 非阻塞，避免写入阻塞任务执行 |
| 回调模式解耦 | Database 不依赖 WorkerAgent，纯函数指针桥接 |
| large_file_threshold_kb 配置 | 用户可调整大文件阈值（单位 KB） |
| DataReader 实例方法 | 需要构造实例，持有路径信息 |
| DataService 进程级单例 | Master/Worker 共享，仅更新触发源不同 |
| IOThreadPool 数据传输 | 文件 I/O 不阻塞 Reactor 线程 |
| IndexEntry 版本 3 | 新增 host 字段支持跨节点追踪 |