# Fly 项目开发规范

本文档整合 Fly 项目的所有代码规范、命名约定、目录结构和设计决策。代码检查时优先参考此文档。

---

## 1. 目录结构

### 1.1 模块标准结构

每个模块包含以下子目录：

```
src/<module>/
├── cpp/          # C++ 类型定义和实现
│   ├── <module>.h
│   ├── <module>.cpp
│   └── BUILD
├── export/       # nanobind Python 绑定导出
│   ├── <module>_export.cpp
│   └── BUILD
├── py/           # Python 包
│   ├── __init__.py
│   └── BUILD
└── tests/        # 测试文件
    ├── <module>_test.cpp    # gtest C++ 测试
    ├── <module>_test.py     # pytest Python 测试
    └── BUILD
```

### 1.2 当前模块列表

| 模块 | 职责 |
|------|------|
| `src/common/` | 公共类型定义（容器别名等） |
| `src/core/` | 核心基础模块（Config 等） |
| `src/serialization/` | 序列化宏和工具 |
| `src/export/` | 导出宏定义 |
| `src/storage/` | 存储层（Database, DataService, DataWriter, DataReader） |
| `src/network/` | 网络层（Reactor, TCP, 消息协议） |
| `src/task/` | 任务系统层（调度, 依赖图, 元数据） |
| `src/agent/` | Agent 层（MasterAgent, WorkerAgent, TaskExecutor） |
| `src/log/` | 日志模块 |

### 1.3 子目录职责

| 目录 | 职责 |
|------|------|
| `cpp/` | C++ 类型定义、核心算法，不直接操作 Python 对象 |
| `export/` | nanobind 导出，将 C++ 类/函数暴露给 Python |
| `py/` | Python 流程控制、主循环 |
| `tests/` | 单元测试和集成测试 |

---

## 2. 命名规范

### 2.1 Bazel Target 命名

**格式**: `fly_<module>_<subdir>`

| 类型 | 命名示例 | 说明 |
|------|----------|------|
| C++ library | `fly_core_cpp` | 模块名 + `_cpp` |
| Export library | `fly_export_macros` | 模块名 + 功能描述 |
| Python library | `fly_core_py` | 模块名 + `_py` |
| Python extension | `_fly_core.so` | 下划线开头 + 模块名 |
| Common types | `fly_common_types` | `fly_` + 功能描述 |

**BUILD 文件示例**:
```python
cc_library(
    name = "fly_core_cpp",
    srcs = ["config.cpp"],
    hdrs = ["config.h"],
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
    deps = ["//src/common/cpp:fly_common_types"],
)
```

### 2.2 C++ 容器别名命名

**格式**: `CM<ContainerType>` (CM = Common)

| 别名 | 原类型 | 说明 |
|------|--------|------|
| `CMMap<K,V>` | `std::map<K,V>` | 有序映射 |
| `CMUnorderedMap<K,V>` | `std::unordered_map<K,V>` | 无序映射 |
| `CMVector<T>` | `std::vector<T>` | 动态数组 |
| `CMSet<T>` | `std::set<T>` | 有序集合 |
| `CMUnorderedSet<T>` | `std::unordered_set<T>` | 无序集合 |
| `CMList<T>` | `std::list<T>` | 双向链表 |
| `CMDeque<T>` | `std::deque<T>` | 双端队列 |
| `CMQueue<T>` | `std::queue<T>` | 队列 |
| `CMStack<T>` | `std::stack<T>` | 栈 |
| `CMString` | `std::string` | 字符串 |

**使用方式**:
```cpp
#include <common/cpp/common_types.h>

CMMap<CMString, int64_t> config_values;
CMVector<std::byte> buffer;
```

### 2.3 宏命名规范

**格式**: `FLY_<CATEGORY>_<ACTION>`

| 类别 | 前缀 | 示例 |
|------|------|------|
| 序列化 | `FLY_SERIALIZE` | `FLY_SERIALIZE(id, name)` |
| 序列化操作 | `FLY_ENCODE` / `FLY_DECODE` | `FLY_ENCODE(msg, out)` |
| 导出模块 | `FLY_EXPORT_MODULE` | `FLY_EXPORT_MODULE(_fly_module)` |
| 导出类 | `FLY_EXPORT_CLASS` | `FLY_EXPORT_CLASS(Type, "EXStgType")` |
| 导出方法 | `FLY_EXPORT_METHOD` | `FLY_EXPORT_METHOD("name", func)` |

### 2.4 导出类型命名规范

**所有导出到 Python 的 C++ 类型必须使用前缀命名**：

**格式**: `EX<ModuleAbbr><TypeName>`

| 模块 | 缩写 | C++ 类型 | Python 导出名 |
|------|------|----------|--------------|
| storage | Stg | `CompressionType` | `EXStgCompressionType` |
| storage | Stg | `Database` | `EXStgDatabase` |
| storage | Stg | `IndexEntry` | `EXStgIndexEntry` |
| storage | Stg | `DbMeta` | `EXStgDbMeta` |
| storage | Stg | `WorkerInfo` | `EXStgWorkerInfo` |
| core | Core | `Config` | `EXCoreConfig` |
| network | Net | `TransportEvent` | `EXNetTransportEvent` |
| network | Net | `HeartbeatMessage` | `EXNetHeartbeatMessage` |

**示例**:
```cpp
// storage_export.cpp
FLY_EXPORT_ENUM(CompressionType, "EXStgCompressionType")
FLY_EXPORT_CLASS(IndexEntry, "EXStgIndexEntry")
    FLY_EXPORT_INIT()
    FLY_EXPORT_READONLY_ATTR("object_name", &IndexEntry::object_name)
    FLY_EXPORT_SERIALIZE(IndexEntry);
```

### 2.5 导出函数命名规范

**所有导出到 Python 的 C++ 函数必须使用前缀命名**：

**格式**: `ex_<module_abbr>_<function_name>`

| 模块 | 缩写 | C++ 函数 | Python 导出名 |
|------|------|----------|--------------|
| storage | stg | `get_storage_manager()` | `ex_stg_get_storage_manager` |
| storage | stg | `create_database()` | `ex_stg_create_database` |
| core | core | `get_config()` | `ex_core_get_config` |
| network | net | `create_connection_manager()` | `ex_net_create_connection_manager` |
| network | net | `encode_message()` | `ex_net_encode_message` |

**目的**: 便于区分 C++ 导出函数与纯 Python 函数，一眼识别函数来源。

**示例**:
```cpp
// storage_export.cpp
FLY_EXPORT_FUNCTION("ex_stg_create_database", [](const CMString& base_path, const CMString& data_path) {
    return std::make_shared<Database>(base_path, data_path);
});

FLY_EXPORT_FUNCTION("ex_stg_get_storage_manager", []() -> StorageManager& {
    return StorageManager::instance();
});

// network_export.cpp
FLY_EXPORT_FUNCTION("ex_net_create_connection_manager", [](const CMString& type) {
    return create_connection_manager(type);
});
```

---

## 3. Include 路径规范

### 3.1 模块式路径（推荐）

使用模块式路径，不使用相对路径：

```cpp
// 正确：模块式路径
#include <core/cpp/config.h>
#include <serialization/cpp/serialization_macros.h>
#include <export/cpp/export_macros.h>
#include <common/cpp/common_types.h>

// 错误：相对路径
#include "../cpp/config.h"
#include "../../serialization/cpp/serialization_macros.h"
```

### 3.2 BUILD 文件配置

每个 header 库需配置 `strip_include_prefix`:

```python
cc_library(
    name = "fly_xxx",
    hdrs = ["xxx.h"],
    strip_include_prefix = "/src",  # 去除 src/ 前缀
    copts = ["-std=c++20"],
)
```

---

## 4. 宏抽象规范

### 4.1 设计原则

所有外部库依赖必须通过宏封装，以便未来替换实现：

| 库 | 宏封装 | 替换方案 |
|---|--------|----------|
| bitsery | `FLY_SERIALIZE_*` | cereal / protobuf |
| nanobind | `FLY_EXPORT_*` | pybind11 / CPython API |
| std::map | `CMMap` | absl::btree_map / robin_map |

### 4.2 序列化宏

所有序列化操作必须通过 FLY_* 宏实现，不得直接调用 bitsery 原始 API。这样可以确保未来切换序列化后端（如 cereal）时无需修改业务代码。

#### 4.2.1 serialize() 声明

两种形式，按需选择：

```cpp
// 1. 简洁形式（所有字段存在于所有版本中）
struct Simple {
    int32_t id;
    CMString name;
    FLY_SERIALIZE(id, name)              // 直接传入字段名，无需 s/o 参数
};

// 2. 完整形式（需要版本判断逻辑）
struct IndexEntry {
    FLY_SERIALIZE_BEGIN(2)               // Version 2
        FLY_FIELD(object_name);          // 直接传入字段名
        FLY_FIELD(offset);
        if (version >= 2) {              // v2 新增字段
            FLY_FIELD(compression_type);
        }
    FLY_SERIALIZE_END
};
```

`FLY_SERIALIZE(...)` 使用 Boost.PP 遍历参数，展开为：
```cpp
template<typename S> void serialize(S& s) {
    s.ext(*this, fly::Version<1>{}, [](S& s, auto& o, size_t) {
        FLY_FIELD(id);
        FLY_FIELD(name);
        //        ↑ 每个字段名自动展开为类型检测 + 正确序列化方法
    });
}
```

`FLY_SERIALIZE_BEGIN(N) / FLY_SERIALIZE_END` 展开为：
```cpp
template<typename S> void serialize(S& s) {
    s.ext(*this, fly::Version<N>{}, [](S& s, auto& o, size_t version) {
        // 用户代码（可访问 version 变量）
        // s 和 o 由 lambda 提供，宏内部硬编码使用
    });
}
```

#### 4.2.2 字段序列化宏

**推荐使用 `FLY_FIELD(field)`（统一宏）**——自动检测字段类型并分发到正确的序列化方式：

```cpp
// 一行搞定任何类型的字段（无需 s/o 参数）
FLY_FIELD(id);           // int32_t → auto-sized value
FLY_FIELD(name);         // string → text (1b length encoding)
FLY_FIELD(scores);       // vector<int> → bulk container(bulk copy)
FLY_FIELD(children);     // vector<Obj> → per-element container(object)
FLY_FIELD(tags);         // map<string,int> → StdMap(key→text, val→value)
FLY_FIELD(grouped);      // map<string,vector<Obj>> → StdMap(auto nested)
FLY_FIELD(inner);        // Obj → object(serialize)
```

`FLY_FIELD(field)` 内部使用 `s` 和 `o`（来自 `FLY_SERIALIZE_BEGIN` lambda），通过 type traits 自动分发：

| 字段类型 | 分发目标 | 说明 |
|----------|----------|------|
| `int32_t`, `double` 等 fundamental | `fly_ser::value(s, o.field)` | 自动 sizeof 推导 1/2/4/8b |
| `std::string` | `fly_ser::text(s, o.field)` | 1字节长度编码 |
| `std::vector<int>` | `fly_ser::container(s, o.field)` | bulk copy（连续内存） |
| `std::vector<Obj>` | `s.container(..., [](auto& s, E& e) { s.object(e); })` | 逐个 serialize() |
| `std::map<K,V>` | `s.ext(StdMap{...}, [](auto& s, k, v) { ... })` | key+val 自动嵌套分发 |
| 具有 `serialize()` 的类型 | `s.object(o.field)` | 递归序列化 |

**仅当需要自定义 lambda 时才使用其他宏：**

| 宏 | 用途 | 示例 |
|----|------|------|
| `FLY_VEC_F(field, lambda)` | 容器字段（自定义元素序列化） | `FLY_VEC_F(strs, [](auto& s, auto& e) { fly_ser::text(s, e); })` |
| `FLY_MAP(field, lambda)` | map 字段（自定义 key/val 序列化） | `FLY_MAP(m, [](auto& s, auto& k, auto& v) { fly_ser::text(s, k); fly_ser::value(s, v); })` |
| `FLY_BOOL(field)` | bool 值 | `FLY_BOOL(flag)` |
| `FLY_VAL, FLY_STR, FLY_VEC, FLY_OBJ` | 精确控制（极少需要） | `FLY_VAL(count)` |

**lambda 内变量辅助函数**（在 `FLY_VEC_F` 等 lambda 内部使用，用于序列化变量而非 struct 字段）：

| 函数 | 用途 |
|------|------|
| `fly_ser::text(s, var)` | 序列化字符串变量 |
| `fly_ser::value(s, var)` | 序列化定长值变量（自动 sizeof） |
| `fly_ser::container(s, var)` | 序列化容器变量（自动 dispatch） |
| `fly_ser::object(s, var)` | 序列化对象变量 |

**生产代码对比**（IndexData：`map<string, vector<IndexEntry>>` 的序列化）：

```cpp
// 之前：5行 + bitsery API 暴露
FLY_SERIALIZE_BEGIN(1) {
    s.ext(o.entries, bitsery::ext::StdMap{FLY_MAX_SIZE},
        [](auto& s2, CMString& key, CMVector<IndexEntry>& val) {
            s2.text1b(key, FLY_MAX_SIZE);
            s2.container(val, FLY_MAX_SIZE, [](auto& s3, IndexEntry& e) {
                s3.object(e);
            });
        });
} FLY_SERIALIZE_END

// 现在：1行 + 隐藏所有细节
FLY_SERIALIZE_BEGIN(1) {
    FLY_FIELD(entries);    // 自动处理 nested map<string, vector<IndexEntry>>
} FLY_SERIALIZE_END
```

#### 4.2.3 编解码宏

```cpp
// 编码到 CMString（char 类型，适用于文件/网络传输）
CMString bytes;
FLY_ENCODE(myStruct, bytes);

// 从 CMString 解码
MyStruct decoded;
FLY_DECODE(bytes, MyStruct, decoded);

// 编码到 FlyBuffer（uint8_t 类型，Python 绑定使用）
FlyBuffer buf;
FLY_ENCODE_TO_BYTES(obj, buf);

// 从 FlyBuffer 解码
FLY_DECODE_FROM_BYTES(buf, MyType, decoded);
```

### 4.3 导出宏

所有导出宏基于 `NB_MODULE(module_name, m)` 约定，宏内部直接使用 `m`，无需传入 `module_var` 参数。

#### 4.3.1 模块定义

```cpp
// 模块入口宏 — 用户手动写大括号，无需 _BEGIN/_END
FLY_EXPORT_MODULE(_fly_module) {
    // 所有导出代码在此大括号内
}
```

#### 4.3.2 类导出

所有类导出宏**必须显式传入 Python 导出名称**（格式：`EX<ModuleAbbr><TypeName>`）：

| 宏 | 用途 | 示例 |
|----|------|------|
| `FLY_EXPORT_CLASS(Type, "name")` | 导出普通类 | `FLY_EXPORT_CLASS(Database, "EXStgDatabase")` |
| `FLY_EXPORT_CLASS_SHARED_PTR(Type, "name")` | 导出支持 shared_ptr 的类 | `FLY_EXPORT_CLASS_SHARED_PTR(StorageManager, "EXStgStorageManager")` |

**类成员导出宏**（链式调用，用户必须传入导出名称）：

| 宏 | 用途 | 示例 |
|----|------|------|
| `FLY_EXPORT_INIT(...)` | 导出构造函数 | `FLY_EXPORT_INIT()` 或 `FLY_EXPORT_INIT(CMString, int)` |
| `FLY_EXPORT_DEF("name", lambda)` | Lambda/复杂方法绑定 | `FLY_EXPORT_DEF("_write_typed", [](Database& db, ...) { ... })` |
| `FLY_EXPORT_ATTR("name", &Type::field)` | 可读写成员变量 | `FLY_EXPORT_ATTR("config", &Config::value)` |
| `FLY_EXPORT_READONLY_ATTR("name", &Type::field)` | 只读成员变量 | `FLY_EXPORT_READONLY_ATTR("db_id", &DbMeta::db_id)` |
| `FLY_EXPORT_METHOD("name", &Type::func)` | 成员方法 | `FLY_EXPORT_METHOD("freeze", &Database::freeze)` |
| `FLY_EXPORT_STATIC_METHOD("name", &Type::func)` | 静态方法 | `FLY_EXPORT_STATIC_METHOD("instance", &Config::instance)` |
| `FLY_EXPORT_PROPERTY("name", getter, setter)` | 计算属性（读写） | `FLY_EXPORT_PROPERTY("count", &List::get_count, &List::set_count)` |
| `FLY_EXPORT_READONLY_PROPERTY("name", getter)` | 计算属性（只读） | `FLY_EXPORT_READONLY_PROPERTY("size", &Buffer::get_size)` |

**序列化导出**（仅 `FLY_EXPORT_SERIALIZE`，已废弃 `FLY_EXPORT_PICKLE`）：

```cpp
FLY_EXPORT_SERIALIZE(IndexEntry)  // 自动添加 __getstate__/__setstate__ + is_cpp 属性
```

`FLY_EXPORT_SERIALIZE` 展开为：
- `__getstate__`: 将对象编码为 bytes
- `__getstate_buffer__`: 编码为 FlyBuffer（shared_ptr 形式）
- `__setstate__`: 从 bytes 解码恢复对象（自动识别 FLY_OBJECT_MAGIC 头走解压路径）
- `_write_to_db`: 实例方法，调 `db.write_object<Cls>` 写入（对称读取用 `_read_from_db`）
- `_read_from_db`: 静态方法，调 `db.read_object<Cls>` 读取（走 C++ ObjectCache high 层，省反序列化）
- `is_cpp` 属性：返回 `True`（用于 Python wrapper 判断对象来源 + 缓存分派）

**完整类导出示例**：

```cpp
FLY_EXPORT_CLASS(IndexEntry, "EXStgIndexEntry")
    FLY_EXPORT_INIT()                                    // 无参构造
    FLY_EXPORT_READONLY_ATTR("object_name", &IndexEntry::object_name)
    FLY_EXPORT_READONLY_ATTR("offset", &IndexEntry::offset)
    FLY_EXPORT_METHOD("some_method", &IndexEntry::some_method)
    FLY_EXPORT_SERIALIZE(IndexEntry);                    // Pickle 支持

FLY_EXPORT_CLASS(Database, "EXStgDatabase")
    FLY_EXPORT_DEF("_write_typed", [](Database& db, const CMString& name,
                                       fly_export::bytes data, const CMString& py_name) {
        CMString str_data(data.c_str(), data.size());
        return db.write_object_typed(name, str_data, py_name);
    })
    FLY_EXPORT_METHOD("freeze", &Database::freeze)
    FLY_EXPORT_METHOD("get_db_id", &Database::get_db_id);
```

#### 4.3.3 枚举导出

```cpp
FLY_EXPORT_ENUM(CompressionType, "EXStgCompressionType")
    FLY_EXPORT_ENUM_VALUE("NONE", CompressionType::NONE)
    FLY_EXPORT_ENUM_VALUE("LZ4", CompressionType::LZ4)
    FLY_EXPORT_ENUM_VALUE("ZSTD", CompressionType::ZSTD);
```

注意：`FLY_EXPORT_ENUM_VALUE` 用户需传入**完全限定值**（如 `CompressionType::NONE`），而非短名。

#### 4.3.4 函数导出

| 宏 | 用途 | 示例 |
|----|------|------|
| `FLY_EXPORT_FUNCTION("name", lambda)` | 导出函数（默认返回值策略） | `FLY_EXPORT_FUNCTION("create_database", [](...) { ... })` |
| `FLY_EXPORT_FUNCTION("name", lambda)` | 导出函数（返回引用） | `FLY_EXPORT_FUNCTION("get_storage_manager", []() -> StorageManager& { ... })` |

**示例**：

```cpp
FLY_EXPORT_FUNCTION("compression_type_from_name", [](const CMString& name) {
    return CompressorFactory::type_from_name(name);
});

FLY_EXPORT_FUNCTION("get_storage_manager", []() -> StorageManager& {
    return StorageManager::instance();
});
```

#### 4.3.5 设计要点

1. **无 `_WITH_NAME` 变体**：所有宏始终要求用户传入导出名称，不存在自动 stringify 版本
2. **无 `module_var` 参数**：`NB_MODULE(module_name, m)` 固定定义 `m`，宏直接使用
3. **用户写大括号**：`FLY_EXPORT_MODULE(name) { }` 不需要 `_BEGIN/_END`
4. **FLY_EXPORT_SERIALIZE 是唯一 pickle 宏**：已删除 `FLY_EXPORT_PICKLE/PICKLE_SHARED_PTR`（不暴露 `__getstate__/__setstate__` 为 Python 方法，导致 FlyDatabase wrapper 无法工作）

#### 4.5.6 类导出规范
1. **仅小对象** 允许使用copy导出
2. **任意大对象类型/copy存在性能问题/python侧需要修改数据并体现在原始数据上的类型** 必须使用shared_ptr的方式导出

---

## 5. C++20 技术决策

### 5.1 Python 绑定模块

**使用传统 headers，不使用 C++20 Modules**

原因：
- Python C 扩展需要 `PyInit_*` 在共享库（`.so`）
- C++20 Modules 生成 `.pcm` 文件（不兼容）
- `extern "C"` 链接与 C++20 module export 语义冲突
- 整个 Python 绑定生态（nanobind/pybind11/Boost.Python）均不支持

### 5.2 纯 C++ 模块

后续可迁移至 C++20 Modules（需 Bazel 9.0+、Clang 17+）

---

## 6. 测试规范

### 6.1 C++ 测试 (gtest)

**文件命名**: `<module>_test.cpp`

```cpp
#include <gtest/gtest.h>
#include <module/cpp/module.h>

TEST(ModuleTest, FunctionName) {
    // 测试内容
    EXPECT_EQ(expected, actual);
}
```

**BUILD 配置**:
```python
cc_test(
    name = "module_test",
    srcs = ["module_test.cpp"],
    deps = [
        "@com_google_googletest//:gtest_main",
        "//src/module/cpp:fly_module_cpp",
    ],
    copts = ["-std=c++20"],
)
```

### 6.2 Python 测试 (pytest)

**文件命名**: `<module>_test.py`

```python
import pytest
from _fly_module import get_module

def test_module_function():
    module = get_module()
    assert module.get_value() == expected
```

### 6.3 项目级测试

- 模块测试：`src/<module>/tests/`
- 项目集成测试：`qa/`

### 6.4 测试稳定性要求（零容忍）

**项目原则：不容忍任何不稳定的测试（flaky test）。所有测试必须每次运行都通过。**

禁止的写法：
```cpp
// ❌ 固定延时后断言 — 在高负载机器上会随机失败
worker.start();
std::this_thread::sleep_for(std::chrono::milliseconds(300));
ASSERT_TRUE(worker.is_registered());
```

正确的写法：
```cpp
// ✅ 轮询等待条件满足 — 容忍机器性能差异
worker.start();
bool registered = false;
for (int i = 0; i < 30; ++i) {
    if (worker.is_registered()) { registered = true; break; }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
}
ASSERT_TRUE(registered);
```

规则：
- **禁止 `sleep(Xms); assert(condition)` 模式** — 必须用轮询循环
- **网络/进程/线程相关的异步操作** — 一律使用轮询等待，超时上限 1.5 秒（30 次 × 50ms）
- **禁止删除失败测试来"通过"** — 必须修复根因
- **禁止 `time.sleep()` 作为 Python 测试中的同步手段** — 使用 `wait_for_*` 轮询方法
- **QA 测试同样适用** — `./qa/run_qa_tests.sh` 必须 100% 稳定通过，不接受任何 flaky

---

## 7. Bazel 构建规范

### 7.1 .bazelrc 配置

```
build --cxxopt=-std=c++20
build --host_cxxopt=-std=c++20
build --enable_bzlmod=false
build --action_env=PATH
test --test_output=errors
```

### 7.2 Visibility 规范

所有库默认公开：
```python
package(default_visibility = ["//visibility:public"])
```

### 7.3 依赖声明格式

```python
deps = [
    "@external_lib//:lib_target",      # 外部依赖
    "//src/module/cpp:fly_module_cpp", # 内部依赖
]
```

---

## 8. clangd LSP 配置

### 8.1 .clangd 文件

```yaml
CompileFlags:
  Add: [-fcolor-diagnostics, -Wno-unused-command-line-argument]

Diagnostics:
  Suppress:
    - unused-parameter
    - unused-variable

Completion:
  HeaderInsertion: Never

Index:
  Background: Build
```

### 8.2 compile_commands.json 生成

```bash
bazel run //:refresh_compile_commands
```

BUILD 文件变更后需重新运行。

---

## 9. Git 规范

### 9.1 .gitignore

```
bazel-bin
bazel-fly
bazel-out
bazel-testlogs
compile_commands.json
.cache
__pycache__
.pytest_cache
```

### 9.2 提交消息格式

```
<type>: <subject>

[可选的详细说明]

[可选的测试状态]
```

Type 类型：
- `feat`: 新功能
- `fix`: 修复
- `refactor`: 重构
- `docs`: 文档
- `test`: 测试

---

## 10. 设计决策记录

| 决策 | 原因 |
|------|------|
| nanobind 而非 pybind11 | 更小、更快、C++20 兼容 |
| bitsery 而非 zpp_bits/cereal | header-only、版本化支持、稳定、性能优良 |
| CM前缀容器别名 | CM=Common，便于替换高效实现 |
| headers 而非 C++20 Modules | Python 绑定不兼容 |
| 模块式 include 路径 | 避免相对路径，便于重构 |

---

## 11. 快速参考

### 11.1 新模块创建模板

```
src/new_module/
├── cpp/
│   ├── new_module.h      # #pragma once, 使用 CMString/CMMap 等
│   ├── new_module.cpp    # #include <module/cpp/new_module.h>
│   └── BUILD             # name="fly_new_module_cpp", strip_include_prefix="/src"
├── export/
│   ├── new_module_export.cpp  # FLY_EXPORT_MODULE_BEGIN(_fly_new_module)
│   └── BUILD             # cc_binary, name="_fly_new_module.so"
├── py/
│   ├── __init__.py       # from _fly_new_module import ...
│   └── BUILD             # py_library
└── tests/
    ├── new_module_test.cpp   # TEST(NewModuleTest, ...)
    ├── new_module_test.py    # pytest 测试
    └── BUILD             # cc_test / py_test
```

### 11.2 常用命令

```bash
# 构建（必须使用 fly.sh，自动更新 clangd — 禁止直接 bazel build）
./fly.sh build [target...]

# 测试（自动刷新 clangd）
./fly.sh test [target...]

# 仅构建（不刷新 clangd）
./fly.sh buildonly [target...]

# 单独刷新 clangd compile_commands.json
./fly.sh refresh

# 构建 + 测试 + 刷新
./fly.sh check

# 安装到 build/ 目录（创建 symlink 到 bazel-bin 产物，用于 QA 测试和部署）
./fly.sh install

# clangd 配置（同 ./fly.sh refresh）
bazel run //:refresh_compile_commands
```

---

## 12. 开发教训与常见陷阱

### 12.1 compile_commands.json 未更新

**问题**：直接使用 `bazel build` 而不通过 `fly.sh build`，导致 `compile_commands.json` 没有刷新，clangd 报大量找不到头文件的错误。

**根因**：`compile_commands.json` 由 `hedron_compile_commands` 工具生成，需要先注册所有目标到顶层 BUILD 文件，然后运行 `bazel run //:refresh_compile_commands`。`fly.sh` 自动完成这些步骤。

**规范**：统一使用 `./fly.sh build` 替代 `bazel build`。若已使用 `bazel build` 直接构建，事后运行 `./fly.sh refresh` 补刷新。

### 12.2 BUILD 文件中遗漏依赖

**问题**：新增头文件引用（如 `#include <serialization/cpp/object_header.h>`）后，未在 BUILD 文件的 `deps` 中添加对应依赖，导致编译失败。

**规范**：
1. 新增 `#include` 时，同步更新 `deps` 列表
2. 使用 `bazel build //target` 验证依赖完整
3. 检查工具：`bazel query "kind('cc_library', deps(//target))"` 可查看所有传递依赖

### 12.3 字段宏签名变更

**历史背景**：早期版本使用 `FLY_FIELD(s, o, field)` 签名，需要用户传入 `s` 和 `o` 参数。当前版本已简化为 `FLY_FIELD(field)`，`s` 和 `o` 由 `FLY_SERIALIZE_BEGIN` lambda 内部硬编码提供。

**规范**：
- **禁止**使用旧签名 `FLY_FIELD(s, o, field)`
- 始终使用简化签名：
  ```cpp
  FLY_SERIALIZE_BEGIN(1) {
      FLY_FIELD(id);       // 正确 — 仅传字段名
      FLY_FIELD(name);
  } FLY_SERIALIZE_END
  ```
- `s` 和 `o` 在 lambda 内自动可用，无需手动传递

### 12.4 测试代码与生产代码序列化风格不一致

**问题**：测试文件中的 `serialize()` 直接调用 `s.value4b(value)` 而非宏，导致未来切换序列化后端时测试代码也需要修改。

**规范**：测试代码与生产代码使用相同的宏模式。所有 `serialize()` 方法必须使用 `FLY_SERIALIZE_BEGIN(N) { ... } FLY_SERIALIZE_END` 声明，内部使用 `FLY_FIELD` 字段宏。

---

**文档更新历史**:

- 2026-05-14: 初版创建，整合目录结构、命名规范、宏抽象、测试规范
- 2026-05-14: 更新序列化宏文档（FLY_SERIALIZE + Boost.PP），新增开发教训章节
- 2026-05-14: 新增 `fly-build` skill — 构建必须使用 `./fly.sh`，禁止裸 `bazel build`
- 2026-05-15: 重构导出宏文档（Section 4.3）：移除 module_var 参数，用户写大括号，新增命名规范 Section 2.4
- 2026-05-15: 修正序列化宏签名：`FLY_FIELD(field)` 替代 `FLY_FIELD(s, o, field)`，移除重复 Section 4.2.2