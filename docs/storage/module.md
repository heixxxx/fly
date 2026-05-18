# Storage 模块 — 存储层

## 模块概述

**位置**: `src/storage/`

存储层是 Fly 框架的核心数据管理模块，负责数据的写入聚合、索引管理、读取（本地 + 远程）、压缩和数据库生命周期管理。设计为 Master 和 Worker 共用的统一层。

---

## 核心文件

| 文件 | 说明 |
|------|------|
| `cpp/database.h/cpp` | 统一存储接口，写时通知 DataService，读时走 DataService |
| `cpp/data_writer.h/cpp` | 单线程写入聚合器，小文件聚合 + 大文件分块 |
| `cpp/data_reader.h/cpp` | 数据读取，`read_from_entries()` 按 IndexEntry 直接读取 |
| `cpp/data_service.h/cpp` | **统一内存索引：local_idx + remote_idx + worker_registry + IOThreadPool** |
| `cpp/storage_manager.h/cpp` | Database 生命周期管理，单例 |
| `cpp/local_index.h/cpp` | 本地索引持久化（.idx 文件读写） |
| `cpp/compressor.h/cpp` | LZ4 / ZLIB / ZSTD 压缩实现 |
| `cpp/object_header.h` | 对象头结构（标记 Python 类型名） |
| `export/storage_export.cpp` | nanobind Python 导出 |
| `export/BUILD` | Bazel 构建配置 |

---

## 类详细说明

### Database（统一存储接口）

```cpp
class Database {
public:
    Database(const CMString& base_path, const CMString& data_path = "", int writer_id = 0);

    // 写入
    CMString write_object_typed(const CMString& name, const CMString& data, const CMString& py_name);
    CMString write_object_raw(const CMString& name, const CMString& data);

    // 读取
    ReadResult read_object_typed(const CMString& name);

    // 管理
    void freeze();
    bool is_frozen() const;
    CMString get_db_id() const;
    CMString get_obj_name(const CMString& name) const;
    CMString get_base_path() const;
    CMString get_data_path() const;

    // 刷新
    void flush();

private:
    CMString base_path_;    // 共享存储路径（所有节点可访问）
    CMString data_path_;    // 本地存储路径（可选，高性能本地写入）
    CMString db_id_;        // 基于路径哈希的唯一标识
    bool is_frozen_ = false;
    std::shared_ptr<DataWriter> writer_;
    int writer_id_;
};
```

**双路径设计**:

| 路径 | 说明 | 必填 |
|------|------|------|
| `base_path` | 共享存储路径，所有节点可访问 | 是 |
| `data_path` | 本地磁盘路径，写入走此路径（高性能） | 否（默认 = base_path） |

**db_id 生成**: 基于路径字符串哈希自动生成，确保唯一性。

**get_obj_name**: 返回 `"{db_id}:{object_name}"` 格式的全局唯一标识符，用于依赖声明和跨 DB 去重。

---

### DataWriter（写入聚合器）

```cpp
class DataWriter {
public:
    DataWriter(const CMString& base_path, int writer_id);

    IndexEntry write_object(const CMString& name, const CMString& data,
                            const CMString& py_name = "");
    void flush();

    IndexEntry get_last_entry() const;  // 暴露最后一条索引

private:
    CMString base_path_;
    int writer_id_;
    CMString current_file_;    // 当前数据文件名
    int64_t current_offset_;   // 当前偏移量
    int file_counter_;         // 文件滚动计数器
    bool dirty_;               // 是否有未刷盘数据
};
```

**写入策略**:

```
小文件 (size < large_file_threshold):
  → 聚合写入当前 .dat 文件
  → 当前文件超过阈值 → 创建新文件 (滚动)

大文件 (size >= large_file_threshold):
  → 分块存储，block_size 大小的连续块
  → IndexEntry.is_large = true, block_count 记录块数
```

**数据文件命名**: `aggregated_w{writer_id}_{counter}.dat`

**两阶段写入**: `write_object()` + `flush()`。write 返回 IndexEntry，flush 刷盘到磁盘。

---

### DataReader（数据读取器）

```cpp
class DataReader {
public:
    static ReadResult read_from_entries(const CMVector<IndexEntry>& entries,
                                        const CMString& base_path);
    static ReadResult read_object_data(const IndexEntry& entry,
                                       const CMString& base_path);

private:
    static CMString find_file(const CMString& file_name, const CMString& base_path);
};
```

**读取流程**:

```
read_from_entries(entries, base_path)
  ├── 单条目 → read_object_data(entry)
  │     └── 打开 .dat 文件 → seek(offset) → read(size)
  │           → 解析 ObjectHeader → 提取 py_name + data
  └── 多条目 (large object)
        → 按 offset 排序 → 逐块读取 → 拼接
        → 从首块 ObjectHeader 提取 py_name
        → 返回 {data_buffer, py_name}
```

---

### DataService（统一内存索引）⭐ 核心组件

```cpp
class DataService {
public:
    static DataService& instance();  // 进程级单例

    // 本地索引
    void on_object_written(const CMString& db_id, const CMString& name,
                           const CMVector<IndexEntry>& entries);
    void on_write_started(const CMString& db_id, const CMString& name);
    void on_write_completed(const CMString& db_id, const CMString& name,
                            const CMVector<IndexEntry>& entries);
    void on_write_failed(const CMString& db_id, const CMString& name);
    void on_flush(const CMString& db_id);

    ReadResult try_read_local(const CMString& name);
    ReadResult try_read_local_or_wait(const CMString& name, int timeout_ms);

    // 远程索引
    bool has_remote_location(const CMString& name) const;
    void update_remote_idx(const CMString& name, uint64_t worker_id,
                           const CMString& host, int32_t port);
    bool lookup_remote_idx(const CMString& name, uint64_t& worker_id,
                           CMString& host, int32_t& port) const;

    // Worker 注册
    void register_worker(uint64_t worker_id, const CMString& host, int32_t port);

    // 数据传输（异步）
    void submit_transfer(uint64_t conn_id, const CMString& object_name);
    void process_completions();

    // DB 路径管理
    void register_db_paths(const CMString& db_id, const CMString& base_path,
                           const CMString& data_path);
    bool get_db_paths(const CMString& db_id, CMString& base_path,
                      CMString& data_path) const;

private:
    // 本地索引：object_name → {db_id, entries[], flushed, completion_state}
    CMUnorderedMap<CMString, LocalObjectInfo> local_idx_;

    // 远程索引：object_name → {worker_id, host, port}
    CMUnorderedMap<CMString, RemoteLocation> remote_idx_;

    // Worker 注册表：worker_id → {host, port}
    CMUnorderedMap<uint64_t, WorkerLocation> worker_registry_;

    // DB 路径映射：db_id → {base_path, data_path}
    CMUnorderedMap<CMString, DbPaths> db_paths_;

    // 数据传输
    std::unique_ptr<IOThreadPool> transfer_pool_;
    CMVector<TransferTask> pending_transfers_;
};
```

**数据结构**:

```
DataService (单例, Master 和 Worker 通用)
├── local_idx:  object_name → LocalObjectInfo
│   ├── db_id: CMString
│   ├── entries: CMVector<IndexEntry>
│   ├── flushed: bool
│   ├── state: CompletionState (INCOMPLETE / COMPLETE / FAILED)
│   ├── cv: condition_variable (等待写入完成)
│   └── cv_mutex: mutex
│
├── remote_idx: object_name → RemoteLocation
│   ├── worker_id: uint64_t
│   ├── host: CMString
│   └── port: int32_t
│
├── worker_registry: worker_id → WorkerLocation
│   ├── host: CMString
│   └── port: int32_t
│
└── transfer_pool: IOThreadPool (文件 I/O 线程池)
```

---

### LocalIndex（索引持久化）

```cpp
class LocalIndex {
public:
    explicit LocalIndex(const CMString& idx_path);

    void write_entry(const IndexEntry& entry);
    CMVector<IndexEntry> read_all_entries();
    void clear();
};
```

将 `IndexEntry` 序列化写入 `.idx` 文件，支持追加写入和全量读取。

---

### IndexEntry（索引条目）

```cpp
struct IndexEntry {
    CMString object_name;   // 对象名
    CMString file_name;     // 所属数据文件名
    int64_t offset;         // 文件内偏移
    int64_t size;           // 数据大小
    bool is_large;          // 是否大文件分块
    int block_count;        // 块数量（大文件）
    CompressionType compression_type;  // 压缩类型

    FLY_SERIALIZE_BEGIN(2)
        FLY_FIELD(object_name);
        FLY_FIELD(file_name);
        FLY_FIELD(offset);
        FLY_FIELD(size);
        FLY_FIELD(is_large);
        FLY_FIELD(block_count);
        if (version >= 2) {
            FLY_FIELD(compression_type);
        }
    FLY_SERIALIZE_END
};
```

---

### Compressor（压缩）

```cpp
enum class CompressionType { NONE, LZ4, ZLIB, ZSTD };

class Compressor {
public:
    static CMString compress(const CMString& data, CompressionType type);
    static CMString decompress(const CMString& data, CompressionType type);
    static CompressionType type_from_name(const CMString& name);
};

class CompressorFactory {
public:
    static CompressionType type_from_name(const CMString& name);
};
```

---

## 核心流程

### 写入流程

```
db.write_object("key", obj)
  │
  ├─ 1. Database._write_typed(name, data, py_name)
  │     └── 检查 is_frozen_ → 冻结则抛异常
  │
  ├─ 2. WorkerAgentContext 触发 → on_write_started(db_id, name)
  │     └── DataService 创建 incomplete LocalObjectInfo
  │
  ├─ 3. register_write_with_master(db_id, name)
  │     └── WriteRegisterMessage → Master ACK
  │         ├── ACK success → 继续
  │         └── ACK fail → 抛 WriteRegistrationError
  │
  ├─ 4. DataWriter::write_object(name, data, py_name)
  │     └── 落盘 → 返回 IndexEntry
  │
  ├─ 5. on_write_completed(db_id, name, entries)
  │     └── DataService 设置 COMPLETE 状态
  │
  └─ 6. Database::flush()
        └── DataWriter::flush() → on_flush(db_id)
            └── DataService 通知 CV (唤醒等待的读取者)
```

### 读取流程（三层降级）

```
db.read_object("key")
  │
  ├─ Layer 1: DataService.try_read_local(key)
  │     └── 查内存 local_idx
  │         ├── 找到 + flushed → DataReader.read_from_entries() → 返回
  │         ├── 找到 + INCOMPLETE → wait on CV → COMPLETE → 返回
  │         └── 未找到 → 进入 Layer 2
  │
  ├─ Layer 2: DataService.lookup_remote_idx(key)
  │     └── 查内存 remote_idx
  │         ├── 有缓存 → DataClient.request_data(host, port, key)
  │         │     └── 独立 TCP socket 直连目标 Worker → 返回
  │         └── 失败/无缓存 → 进入 Layer 3
  │
  └─ Layer 3: request_remote_data(key) (最多 3 次重试)
        └── DataQuery → Master → DataLocation
            → DataClient 直连 → 返回
            → 成功后 update_remote_idx() 缓存
```

### 数据传输架构

**Worker B（服务端 — 响应数据请求）**:

```
Reactor 收到 DataRequestMessage
  → on_data_request handler
  → DataService.submit_transfer(conn_id, object_name)  ← 非阻塞入队
  → IOThreadPool 工作线程:
      → try_read_local_or_wait(object_name)
      → 文件 I/O + 解压
  → process_completions() (回到 Reactor 线程)
      → reactor_->send(conn_id, DataResponseMessage)
```

**Worker A（客户端 — 发起数据请求）**:

```
DataClient::request_data(host, port, object_name)
  → 创建独立阻塞 TCP socket (不走主 Reactor)
  → connect → send DataRequestMessage → recv DataResponseMessage → close
```

---

## Python 导出

```cpp
FLY_EXPORT_MODULE(_fly_storage) {
    FLY_EXPORT_CLASS(Database, "EXStgDatabase")
        FLY_EXPORT_INIT(CMString, CMString, int)
        FLY_EXPORT_METHOD("write_object_raw", &Database::write_object_raw)
        FLY_EXPORT_METHOD("read_object_raw", &Database::read_object_raw)
        FLY_EXPORT_READONLY_ATTR("db_id", &Database::get_db_id)
        FLY_EXPORT_METHOD("get_obj_name", &Database::get_obj_name)
        FLY_EXPORT_METHOD("freeze", &Database::freeze)
        FLY_EXPORT_METHOD("is_frozen", &Database::is_frozen)
        FLY_EXPORT_METHOD("flush", &Database::flush)
        FLY_EXPORT_METHOD("get_base_path", &Database::get_base_path)
        FLY_EXPORT_METHOD("get_data_path", &Database::get_data_path);

    FLY_EXPORT_FUNCTION("ex_stg_create_database", ...);
    FLY_EXPORT_FUNCTION_REF("ex_stg_get_data_service", ...);

    FLY_EXPORT_CLASS(IndexEntry, "EXStgIndexEntry")
        FLY_EXPORT_INIT(CMString, CMString, int64_t, int64_t, bool, int, int32_t)
        FLY_EXPORT_READONLY_ATTR("object_name", &IndexEntry::object_name)
        // ...
        FLY_EXPORT_SERIALIZE(IndexEntry);
}
```

---

## 存储文件格式

### 目录结构

```
/data/                           (base_path — 共享路径)
├── aggregated_w1_001.dat        # Worker1 数据文件
├── aggregated_w1.idx            # Worker1 索引文件
├── merged.idx                   # 合并索引（freeze 后生成）
├── _FROZEN                      # 冻结标识文件
├── _META                        # 数据库元信息（JSON）
└── ...

/ssd/local/                      (data_path — 本地路径，可选)
├── aggregated_w1_001.dat        # Worker1 本地数据
└── aggregated_w1.idx            # Worker1 本地索引
```

### 索引文件（.idx）

每个 Worker 一个索引文件，二进制格式（bitsery 序列化），记录该 Worker 所有数据文件中的对象索引：

```
[IndexEntry][IndexEntry][IndexEntry]...

每条 IndexEntry:
  object_name (string) | file_name (string) | offset (int64)
  size (int64) | is_large (bool) | block_count (int)
  compression_type (enum, v2+)
```

---

## 设计决策

| 决策 | 原因 |
|------|------|
| DataService 进程级单例 | Master 和 Worker 共享同一个类，仅更新触发源不同 |
| local_idx + remote_idx 不分模式 | 统一数据结构，降低复杂度 |
| Worker 远程读取后更新 remote_idx | 避免重复查 Master，后续同对象直接 Worker→Worker |
| IOThreadPool 异步传输 | 文件 I/O 不阻塞 Reactor，心跳和任务分配不受影响 |
| DataClient 独立连接 | 每次读创建独立 socket，避免多线程读冲突 |
| completion 回调在 Reactor 线程 | 复用 Reactor transport 发送，避免跨线程 send |
| Write Registration 协议 | 防止写入已冻结 DB，支持并发读取等待 |
| LocalObjectInfo shared_ptr | condition_variable 不可移动，shared_ptr 保证地址稳定 |
| on_flush 通知 CV | CV predicate 等待 COMPLETE+flushed，flush 必须 notify |
