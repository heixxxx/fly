# Fly Layer 1: 核心存储层实现计划

> **状态**: ✅ 完成 (2026-05-14)
> **测试**: 45 tests pass
> **提交**: 12 commits

> **Goal:** 实现 Database、DataWriter、DataReader、LocalIndex、Serializer、StorageManager 等核心存储组件，为上层任务系统提供数据持久化能力。

> **Architecture:** C++20 实现 + nanobind 导出 + Bazel 构建。每个组件编译为独立 `.so`。

> **Tech Stack:** C++20, nanobind, zpp_bits, gtest, pytest

---

## 组件概览

| 组件 | 职责 | 文件结构 |
|------|------|----------|
| **Database** | 统一存储接口，read_object/write_object/freeze | `src/storage/cpp/database.h/cpp` |
| **DataWriter** | 单线程写入聚合器，小文件聚合+大文件分块 | `src/storage/cpp/data_writer.h/cpp` |
| **DataReader** | 数据读取，本地路径优先查找 | `src/storage/cpp/data_reader.h/cpp` |
| **LocalIndex** | 本地索引管理 (.idx 文件) | `src/storage/cpp/local_index.h/cpp` |
| **Serializer** | 对象序列化/反序列化 | 已实现 (serialization_macros.h) |
| **StorageManager** | 动态创建 Database/Writer | `src/storage/cpp/storage_manager.h/cpp` |
| **Object** | 数据对象抽象 | `src/storage/cpp/object.h/cpp` |

---

## 文件结构

```
src/storage/
├── cpp/
│   ├── database.h              # Database 类声明
│   ├── database.cpp            # Database 实现
│   ├── data_writer.h           # DataWriter 类声明
│   ├── data_writer.cpp         # DataWriter 实现
│   ├── data_reader.h           # DataReader 类声明
│   ├── data_reader.cpp         # DataReader 实现
│   ├── local_index.h           # LocalIndex 类声明
│   ├── local_index.cpp         # LocalIndex 实现
│   ├── storage_manager.h       # StorageManager 单例
│   ├── storage_manager.cpp     # StorageManager 实现
│   ├── object.h                # Object 抽象类
│   ├── object.cpp              # Object 实现
│   ├── index_entry.h           # IndexEntry 结构
│   ├── db_meta.h               # DbMeta 结构
│   └── BUILD                   # cc_library targets
├── export/
│   ├── storage_export.cpp      # nanobind 导出
│   └── BUILD                   # cc_binary: _fly_storage.so
├── py/
│   ├── __init__.py             # from _fly_storage import *
│   └── BUILD                   # py_library
└── tests/
    ├── database_test.cpp       # Database gtest
    ├── data_writer_test.cpp    # DataWriter gtest
    ├── data_reader_test.cpp    # DataReader gtest
    ├── local_index_test.cpp    # LocalIndex gtest
    ├── storage_manager_test.cpp # StorageManager gtest
    ├── object_test.cpp         # Object gtest
    ├── storage_test.py         # pytest 集成测试
    └── BUILD                   # cc_test targets
```

---

## Task 1: Object 抽象类

### 设计

```cpp
// src/storage/cpp/object.h
#pragma once

#include <common/cpp/common_types.h>
#include <cstdint>
#include <memory>

class Object {
public:
    virtual ~Object() = default;
    
    virtual CMString serialize() const = 0;
    virtual void deserialize(const CMString& data) = 0;
    
    virtual CMString type_name() const = 0;
    
    template<typename T>
    static std::shared_ptr<T> create();
};

template<typename T>
class ObjectImpl : public Object {
public:
    CMString serialize() const override {
        CMString output;
        FLY_ENCODE(*this, output);
        return output;
    }
    
    void deserialize(const CMString& data) override {
        FLY_DECODE(data, T, *this);
    }
    
    CMString type_name() const override {
        return typeid(T).name();
    }
};
```

### 测试

```cpp
TEST(ObjectTest, SerializeDeserialize) {
    // TestObject 继承 ObjectImpl<TestObject>
    auto obj = TestObject::create();
    obj->value = 42;
    
    CMString serialized = obj->serialize();
    auto decoded = TestObject::deserialize(serialized);
    
    EXPECT_EQ(decoded->value, 42);
}
```

---

## Task 2: IndexEntry 和 DbMeta 结构

### 设计

```cpp
// src/storage/cpp/index_entry.h
#pragma once

#include <common/cpp/common_types.h>
#include <cstdint>

struct IndexEntry {
    CMString object_name;   // 对象路径名
    CMString file_name;     // 数据文件名
    int64_t offset;         // 文件偏移
    int64_t size;           // 数据大小
    bool is_large;          // 是否为大文件分块
    int32_t block_count;    // 分块数量 (is_large=true时)
    
    FLY_SERIALIZE_DECLARE() {
        FLY_SERIALIZE_FIELDS(object_name, file_name, offset, size, is_large, block_count);
    }
};

// src/storage/cpp/db_meta.h
struct WorkerInfo {
    uint64_t worker_id;
    CMString host;
    CMString role;
    CMString data_path;
    CMString idx_file;
    int64_t idx_entry_count;
    CMString launch_command;
    
    FLY_SERIALIZE_DECLARE() {
        FLY_SERIALIZE_FIELDS(worker_id, host, role, data_path, idx_file, idx_entry_count, launch_command);
    }
};

struct DbMeta {
    CMString db_id;
    CMString base_path;
    int64_t created_at;
    int64_t frozen_at;
    CMVector<WorkerInfo> workers;
    
    FLY_SERIALIZE_DECLARE() {
        FLY_SERIALIZE_FIELDS(db_id, base_path, created_at, frozen_at, workers);
    }
};
```

---

## Task 3: LocalIndex 本地索引

### 设计

```cpp
// src/storage/cpp/local_index.h
#pragma once

#include "index_entry.h"
#include <common/cpp/common_types.h>
#include <unordered_map>
#include <fstream>

class LocalIndex {
public:
    LocalIndex(const CMString& idx_path);
    ~LocalIndex();
    
    void add_entry(const IndexEntry& entry);
    bool remove_entry(const CMString& object_name);
    IndexEntry* find_entry(const CMString& object_name);
    
    void save();
    void load();
    
    int64_t entry_count() const;
    CMVector<IndexEntry> get_all_entries() const;
    
private:
    CMString idx_path_;
    CMUnorderedMap<CMString, IndexEntry> entries_;
    bool modified_ = false;
};
```

### 测试

```cpp
TEST(LocalIndexTest, AddAndFindEntry) {
    LocalIndex index("/tmp/test.idx");
    
    IndexEntry entry;
    entry.object_name = "test/object";
    entry.file_name = "data_001.dat";
    entry.offset = 100;
    entry.size = 200;
    
    index.add_entry(entry);
    index.save();
    
    IndexEntry* found = index.find_entry("test/object");
    EXPECT_NE(found, nullptr);
    EXPECT_EQ(found->offset, 100);
}

TEST(LocalIndexTest, LoadFromFile) {
    // 先写入
    LocalIndex index1("/tmp/test.idx");
    index1.add_entry({...});
    index1.save();
    
    // 再加载
    LocalIndex index2("/tmp/test.idx");
    index2.load();
    
    EXPECT_EQ(index2.entry_count(), 1);
}

TEST(LocalIndexTest, MultipleEntries) {
    LocalIndex index("/tmp/multi.idx");
    
    for (int i = 0; i < 100; i++) {
        IndexEntry entry;
        entry.object_name = "obj_" + std::to_string(i);
        entry.file_name = "data.dat";
        entry.offset = i * 100;
        entry.size = 50;
        index.add_entry(entry);
    }
    
    index.save();
    EXPECT_EQ(index.entry_count(), 100);
}
```

---

## Task 4: DataWriter 数据写入

### 设计

```cpp
// src/storage/cpp/data_writer.h
#pragma once

#include "local_index.h"
#include <common/cpp/common_types.h>
#include <fstream>
#include <memory>

class DataWriter {
public:
    DataWriter(
        const CMString& base_path,
        const CMString& data_path,
        uint64_t worker_id,
        int64_t aggregation_threshold,
        int64_t large_file_threshold,
        int64_t block_size
    );
    
    ~DataWriter();
    
    CMString write_object(
        const CMString& object_name,
        const CMString& data,
        bool backup = false
    );
    
    void flush();
    void close();
    
    int64_t total_bytes_written() const;
    int32_t file_count() const;
    
private:
    void create_new_file();
    CMString get_current_file_name();
    void write_small_object(const CMString& object_name, const CMString& data);
    void write_large_object(const CMString& object_name, const CMString& data);
    
    CMString base_path_;
    CMString data_path_;
    uint64_t worker_id_;
    int64_t aggregation_threshold_;
    int64_t large_file_threshold_;
    int64_t block_size_;
    
    CMString current_file_;
    int32_t file_index_ = 1;
    int64_t current_file_size_ = 0;
    std::ofstream file_stream_;
    
    std::unique_ptr<LocalIndex> index_;
    int64_t total_bytes_ = 0;
};
```

### 测试

```cpp
TEST(DataWriterTest, WriteSmallObject) {
    Config::instance().reset();
    DataWriter writer("/tmp/base", "/tmp/data", 1, 1024, 10240, 128);
    
    CMString file = writer.write_object("small/test", "hello world", false);
    
    EXPECT_EQ(file, "aggregated_w1_001.dat");
    EXPECT_GT(writer.total_bytes_written(), 0);
}

TEST(DataWriterTest, WriteLargeObject) {
    Config::instance().reset();
    DataWriter writer("/tmp/base", "", 1, 1024, 100, 50);
    
    CMString large_data(500, 'x');
    CMString file = writer.write_object("large/test", large_data, false);
    
    EXPECT_TRUE(file.find("aggregated") != CMString::npos);
}

TEST(DataWriterTest, AggregationThreshold) {
    Config::instance().reset();
    DataWriter writer("/tmp/base", "", 1, 100, 1000, 50);
    
    // 写入多个小文件，触发聚合
    for (int i = 0; i < 10; i++) {
        CMString data(50, 'a');
        writer.write_object("obj_" + std::to_string(i), data, false);
    }
    
    EXPECT_EQ(writer.file_count(), 1);  // 聚合到一个文件
}

TEST(DataWriterTest, CreateNewFileWhenFull) {
    Config::instance().reset();
    DataWriter writer("/tmp/base", "", 1, 200, 1000, 50);
    
    CMString data(100, 'x');
    writer.write_object("obj1", data, false);  // 写入第一个文件
    writer.write_object("obj2", data, false);  // 超过阈值，创建新文件
    
    EXPECT_EQ(writer.file_count(), 2);
}
```

---

## Task 5: DataReader 数据读取

### 设计

```cpp
// src/storage/cpp/data_reader.h
#pragma once

#include "local_index.h"
#include <common/cpp/common_types.h>
#include <fstream>
#include <memory>

class DataReader {
public:
    DataReader(
        const CMString& base_path,
        const CMString& data_path,
        uint64_t worker_id
    );
    
    CMString read_object(const CMString& object_name);
    CMString read_object(const IndexEntry& entry);
    
    bool exists(const CMString& object_name);
    
private:
    CMString find_file_path(const CMString& file_name);
    CMString read_from_file(const CMString& file_path, int64_t offset, int64_t size);
    CMString read_large_object(const IndexEntry& entry);
    
    CMString base_path_;
    CMString data_path_;
    uint64_t worker_id_;
    
    std::unique_ptr<LocalIndex> index_;
};
```

### 测试

```cpp
TEST(DataReaderTest, ReadSmallObject) {
    // 先写入
    DataWriter writer("/tmp/base", "", 1, 1024, 10240, 128);
    writer.write_object("test/obj", "hello", false);
    writer.close();
    
    // 再读取
    DataReader reader("/tmp/base", "", 1);
    CMString data = reader.read_object("test/obj");
    
    EXPECT_EQ(data, "hello");
}

TEST(DataReaderTest, ReadFromLocalPath) {
    DataWriter writer("/tmp/base", "/tmp/data", 1, 1024, 10240, 128);
    writer.write_object("test/local", "data", false);
    writer.close();
    
    DataReader reader("/tmp/base", "/tmp/data", 1);
    CMString data = reader.read_object("test/local");
    
    EXPECT_EQ(data, "data");
}

TEST(DataReaderTest, ObjectNotExist) {
    DataReader reader("/tmp/base", "", 1);
    
    EXPECT_THROW(reader.read_object("nonexistent"), std::runtime_error);
}
```

---

## Task 6: Database 核心接口

### 设计

```cpp
// src/storage/cpp/database.h
#pragma once

#include "data_writer.h"
#include "data_reader.h"
#include "db_meta.h"
#include <common/cpp/common_types.h>
#include <memory>
#include <stdexcept>

class Database {
public:
    Database(const CMString& base_path, const CMString& data_path = "");
    ~Database();
    
    std::shared_ptr<Object> read_object(const CMString& object_name);
    void write_object(
        const CMString& object_name,
        std::shared_ptr<Object> data,
        bool backup = false
    );
    
    void freeze();
    bool is_frozen() const { return is_frozen_; }
    
    DbMeta load_meta() const;
    CMString get_db_id() const { return db_id_; }
    CMString get_base_path() const { return base_path_; }
    CMString get_data_path() const;
    
    void reset();  // For testing
    
private:
    void check_frozen();
    void create_frozen_marker();
    CMString generate_db_id();
    
    CMString base_path_;
    CMString data_path_;
    CMString db_id_;
    bool is_frozen_ = false;
    
    std::unique_ptr<DataWriter> writer_;
    std::unique_ptr<DataReader> reader_;
};
```

### 测试

```cpp
TEST(DatabaseTest, WriteAndReadObject) {
    Config::instance().reset();
    Database db("/tmp/test_db");
    
    auto obj = std::make_shared<TestObject>();
    obj->value = 42;
    db.write_object("test/obj", obj, false);
    
    auto read = db.read_object("test/obj");
    EXPECT_EQ(read->value, 42);
}

TEST(DatabaseTest, FreezePreventsWrite) {
    Config::instance().reset();
    Database db("/tmp/freeze_db");
    
    db.write_object("test/obj", std::make_shared<TestObject>(), false);
    db.freeze();
    
    EXPECT_TRUE(db.is_frozen());
    EXPECT_THROW(
        db.write_object("test/obj2", std::make_shared<TestObject>(), false),
        std::runtime_error
    );
}

TEST(DatabaseTest, FrozenMarkerCreated) {
    Config::instance().reset();
    Database db("/tmp/marker_db");
    db.freeze();
    
    // Check _FROZEN file exists
    std::ifstream frozen("/tmp/marker_db/_FROZEN");
    EXPECT_TRUE(frozen.good());
}

TEST(DatabaseTest, DoublePathReadPriority) {
    Config::instance().reset();
    Database db("/tmp/shared", "/tmp/local");
    
    auto obj = std::make_shared<TestObject>();
    obj->value = 100;
    db.write_object("priority/test", obj, false);
    
    auto read = db.read_object("priority/test");
    EXPECT_EQ(read->value, 100);
}

TEST(DatabaseTest, LoadMetaFromFrozenDatabase) {
    Config::instance().reset();
    Database db("/tmp/meta_db");
    db.write_object("test/obj", std::make_shared<TestObject>(), false);
    db.freeze();
    
    DbMeta meta = db.load_meta();
    EXPECT_EQ(meta.db_id, "/tmp/meta_db");
    EXPECT_TRUE(meta.frozen_at > 0);
}
```

---

## Task 7: StorageManager 单例

### 设计

```cpp
// src/storage/cpp/storage_manager.h
#pragma once

#include "database.h"
#include "data_writer.h"
#include <common/cpp/common_types.h>
#include <unordered_map>
#include <memory>

class StorageManager {
public:
    static StorageManager& instance();
    
    std::shared_ptr<Database> get_or_create_database(
        const CMString& base_path,
        const CMString& data_path = ""
    );
    
    std::shared_ptr<DataWriter> get_writer(uint64_t worker_id);
    
    void close_all();
    void reset();  // For testing
    
private:
    StorageManager();
    StorageManager(const StorageManager&) = delete;
    StorageManager& operator=(const StorageManager&) = delete;
    
    CMMap<CMString, std::shared_ptr<Database>> databases_;
    CMMap<uint64_t, std::shared_ptr<DataWriter>> writers_;
};
```

### 测试

```cpp
TEST(StorageManagerTest, SingletonReturnsSameInstance) {
    auto& m1 = StorageManager::instance();
    auto& m2 = StorageManager::instance();
    EXPECT_EQ(&m1, &m2);
}

TEST(StorageManagerTest, GetOrCreateDatabase) {
    Config::instance().reset();
    auto& manager = StorageManager::instance();
    
    auto db1 = manager.get_or_create_database("/tmp/db1", "");
    auto db2 = manager.get_or_create_database("/tmp/db1", "");
    
    EXPECT_EQ(db1, db2);  // Same instance for same path
}

TEST(StorageManagerTest, DifferentPathsCreateDifferentDatabases) {
    Config::instance().reset();
    auto& manager = StorageManager::instance();
    
    auto db1 = manager.get_or_create_database("/tmp/db1", "");
    auto db2 = manager.get_or_create_database("/tmp/db2", "");
    
    EXPECT_NE(db1, db2);
}

TEST(StorageManagerTest, GetWriterByWorkerId) {
    Config::instance().reset();
    auto& manager = StorageManager::instance();
    
    auto writer1 = manager.get_writer(1);
    auto writer2 = manager.get_writer(1);
    
    EXPECT_EQ(writer1, writer2);  // Same writer for same worker
}

TEST(StorageManagerTest, CloseAll) {
    Config::instance().reset();
    auto& manager = StorageManager::instance();
    
    manager.get_or_create_database("/tmp/db", "");
    manager.get_writer(1);
    
    manager.close_all();
    
    // After close, should create new instances
    auto db = manager.get_or_create_database("/tmp/db", "");
    EXPECT_NE(db, nullptr);
}
```

---

## Task 8: Python 导出

### 设计

```cpp
// src/storage/export/storage_export.cpp
#include "../../export/cpp/export_macros.h"
#include "../../serialization/cpp/serialization_macros.h"
#include "../cpp/database.h"
#include "../cpp/storage_manager.h"
#include "../cpp/object.h"
#include "../cpp/index_entry.h"
#include "../cpp/db_meta.h"

FLY_EXPORT_MODULE_BEGIN(_fly_storage)

// Object (抽象类，无 init)
FLY_EXPORT_CLASS_NO_INIT(m, Object,
    FLY_EXPORT_METHOD(serialize, &Object::serialize)
    FLY_EXPORT_METHOD(type_name, &Object::type_name)
);

// Database
FLY_EXPORT_CLASS_NO_INIT(m, Database,
    FLY_EXPORT_METHOD(read_object, &Database::read_object)
    FLY_EXPORT_METHOD(write_object, &Database::write_object)
    FLY_EXPORT_METHOD(freeze, &Database::freeze)
    FLY_EXPORT_METHOD(is_frozen, &Database::is_frozen)
    FLY_EXPORT_METHOD(get_db_id, &Database::get_db_id)
    FLY_EXPORT_METHOD(get_base_path, &Database::get_base_path)
    FLY_EXPORT_METHOD(get_data_path, &Database::get_data_path)
);

// StorageManager
FLY_EXPORT_CLASS_NO_INIT(m, StorageManager,
    FLY_EXPORT_METHOD(get_or_create_database, &StorageManager::get_or_create_database)
    FLY_EXPORT_METHOD(get_writer, &StorageManager::get_writer)
    FLY_EXPORT_METHOD(close_all, &StorageManager::close_all)
    FLY_EXPORT_METHOD(reset, &StorageManager::reset)
);

// IndexEntry
FLY_EXPORT_CLASS(m, IndexEntry,
    FLY_EXPORT_READONLY_ATTR(object_name, object_name)
    FLY_EXPORT_READONLY_ATTR(file_name, file_name)
    FLY_EXPORT_READONLY_ATTR(offset, offset)
    FLY_EXPORT_READONLY_ATTR(size, size)
    FLY_EXPORT_READONLY_ATTR(is_large, is_large)
    FLY_EXPORT_READONLY_ATTR(block_count, block_count)
);

// DbMeta
FLY_EXPORT_CLASS(m, DbMeta,
    FLY_EXPORT_READONLY_ATTR(db_id, db_id)
    FLY_EXPORT_READONLY_ATTR(base_path, base_path)
    FLY_EXPORT_READONLY_ATTR(created_at, created_at)
    FLY_EXPORT_READONLY_ATTR(frozen_at, frozen_at)
);

// Global functions
FLY_EXPORT_FUNCTION_REF_WITH_NAME(m, "get_storage_manager", []() { return &StorageManager::instance(); });

FLY_EXPORT_MODULE_END()
```

---

## 实现顺序

1. **Task 1**: Object 抽象类 (基础类型)
2. **Task 2**: IndexEntry 和 DbMeta 结构 (数据结构)
3. **Task 3**: LocalIndex (索引管理)
4. **Task 4**: DataWriter (写入器)
5. **Task 5**: DataReader (读取器)
6. **Task 6**: Database (核心接口)
7. **Task 7**: StorageManager (单例管理)
8. **Task 8**: Python 导出 (nanobind)
9. **Task 9**: pytest 集成测试

---

## 依赖关系

```
Object
  ↓
IndexEntry, DbMeta
  ↓
LocalIndex (依赖 IndexEntry)
  ↓
DataWriter (依赖 LocalIndex)
DataReader (依赖 LocalIndex)
  ↓
Database (依赖 DataWriter, DataReader)
  ↓
StorageManager (依赖 Database)
  ↓
storage_export.cpp (依赖所有)
```

---

## 测试策略

每个 Task 完成后：
1. 编写 gtest 测试
2. 运行 `bazel test //src/storage/tests:<task>_test`
3. 测试通过后进入下一个 Task

Layer 1 完成后：
1. `bazel build //src/storage/...`
2. `bazel test //src/storage/...`
3. `pytest qa/storage_test.py`

---

## BUILD 文件模板

```python
# src/storage/cpp/BUILD
package(default_visibility = ["//visibility:public"])

cc_library(
    name = "fly_storage_object",
    hdrs = ["object.h"],
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
)

cc_library(
    name = "fly_storage_index_entry",
    hdrs = ["index_entry.h"],
    deps = [
        "//src/common/cpp:fly_common_types",
        "//src/serialization/cpp:fly_serialization_macros",
    ],
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
)

cc_library(
    name = "fly_storage_local_index",
    srcs = ["local_index.cpp"],
    hdrs = ["local_index.h"],
    deps = [
        ":fly_storage_index_entry",
        "//src/common/cpp:fly_common_types",
        "//src/core/cpp:fly_core_cpp",
    ],
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
)

# ... 其他 library targets ...

cc_library(
    name = "fly_storage_cpp",
    deps = [
        ":fly_storage_object",
        ":fly_storage_index_entry",
        ":fly_storage_db_meta",
        ":fly_storage_local_index",
        ":fly_storage_data_writer",
        ":fly_storage_data_reader",
        ":fly_storage_database",
        ":fly_storage_storage_manager",
    ],
)
```

---

**文档创建时间**: 2026-05-14
**预计实现时间**: 2-3 天
**前置条件**: Layer 0 完成并通过所有测试