# Fly Layer 0: 项目基础设施 + 构建系统

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 搭建Fly项目的Bazel构建系统、C++基础设施（Config单例、序列化宏、导出宏），确保每个模块的`cpp/`编译为独立`.a`/`.so`，`export/`编译为独立`.so`供Python运行时`import`加载。

**Architecture:** Bazel workspace + 顶层BUILD文件管理C++和Python目标。每个模块三层结构：`cpp/`（纯C++库）→ `export/`（独立pybind11 `.so`模块）→ `py/`（Python包，通过`import _fly_xxx`加载`.so`）。Config为全局单例（C++实现+`_fly_core.so`导出）。序列化宏和导出宏为头文件only库。运行时Python通过`import _fly_core`动态加载C++模块。

**Module .so Naming Convention:**
- `src/core/export/` → `_fly_core.so` (Config, Database, StorageManager等)
- `src/master/export/` → `_fly_master.so` (MasterReactor等)
- `src/worker/export/` → `_fly_worker.so` (WorkerReactor等)
- `src/serialization/export/` → `_fly_serialization.so` (消息序列化)
- 每个模块的`py/__init__.py`负责`from _fly_xxx import *`

**Tech Stack:** C++17, Bazel 7.x, gtest, pybind11, cereal

---

## 文件结构

```
fly/
├── WORKSPACE                    # Bazel workspace + 外部依赖声明
├── .bazelrc                     # Bazel编译选项
├── BUILD                         # 顶层BUILD
├── src/
│   ├── core/                    # 核心基础模块
│   │   ├── cpp/                  # 纯C++库 → libfly_core_cpp.a
│   │   │   ├── config.h         # Config单例类声明
│   │   │   ├── config.cpp        # Config单例实现
│   │   │   └── BUILD            # cc_library: fly_core_cpp
│   │   ├── export/              # pybind11导出 → _fly_core.so
│   │   │   ├── core_export.cpp   # pybind11导出Config等
│   │   │   └── BUILD            # pybind11.cc_pybind_extension: _fly_core
│   │   ├── py/                  # Python包，import _fly_core
│   │   │   ├── __init__.py      # from _fly_core import *
│   │   │   └── BUILD
│   │   └── tests/
│   │       ├── config_test.cpp  # gtest: Config单例测试
│   │       ├── config_test.py   # pytest: Config Python接口测试
│   │       └── BUILD
│   │
│   ├── serialization/           # 序列化模块
│   │   ├── cpp/                  # 纯C++头文件库 → header-only
│   │   │   ├── serialization_macros.h   # FLY_SERIALIZE_* 宏
│   │   │   └── BUILD            # cc_library: fly_serialization_macros
│   │   ├── export/              # pybind11导出 → _fly_serialization.so (后续层)
│   │   └── tests/
│   │       ├── serialization_test.cpp   # gtest: 宏编译和基本序列化测试
│   │       └── BUILD
│   │
│   └── export/                   # 导出宏模块（header-only）
│       ├── cpp/
│       │   ├── export_macros.h          # FLY_EXPORT_* 宏
│       │   └── BUILD            # cc_library: fly_export_macros
│       └── tests/
│           ├── export_test.cpp           # gtest: 导出宏编译测试
│           └── BUILD
├── third_party/
│   └── cereal/                  # cereal库（或通过http_archive）
└── qa/                          # 暂时为空，后续层使用
```

**编译产物对应关系：**

| 源目录 | C++库 | pybind11 .so | Python导入 |
|--------|-------|-------------|-----------|
| `src/core/cpp/` | `libfly_core_cpp.a` | — | — |
| `src/core/export/` | — | `_fly_core.so` | `import _fly_core` |
| `src/serialization/cpp/` | header-only | — | — |
| `src/master/export/` (后续) | — | `_fly_master.so` | `import _fly_master` |
| `src/worker/export/` (后续) | — | `_fly_worker.so` | `import _fly_worker` |

**关键架构约束：**
- `cpp/` 编译为 `.a` 静态库，不包含任何pybind11代码
- `export/` 编译为独立 `.so`，仅包含pybind11绑定代码，链接对应的 `.a`
- `py/__init__.py` 通过 `from _fly_xxx import *` 加载 `.so`，不包含核心逻辑
- Python运行时按需 `import`，各 `.so` 独立加载

---

### Task 1: Bazel Workspace + 构建配置

**Files:**
- Create: `WORKSPACE`
- Create: `.bazelrc`
- Create: `BUILD`
- Create: `src/core/cpp/BUILD`
- Create: `src/serialization/cpp/BUILD`
- Create: `src/export/cpp/BUILD`

- [ ] **Step 1: 创建WORKSPACE文件**

```python
# WORKSPACE
workspace(name = "fly")

# pybind11
load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

http_archive(
    name = "pybind11",
    strip_prefix = "pybind11-2.12.0",
    sha256 = "1afedea0b1fd0ee8a2be3cf8f1e4e43c1d6e8d0bc9b63e9e0e2247e4SOMEHASH",
    urls = ["https://github.com/pybind/pybind11/archive/refs/tags/v2.12.0.tar.gz"],
)

load("@pybind11//:defs.bzl", "pybind11_dep")

# cereal (serialization library)
http_archive(
    name = "cereal",
    strip_prefix = "cereal-1.3.2",
    sha256 = "1a57a1e3f3e5d849d48f2fc380b3f4e7d398e00e13d3b3ee4a1c8d5bquery",
    urls = ["https://github.com/USCiLab/cereal/archive/refs/tags/v1.3.2.tar.gz"],
    build_file = "@//third_party:cereal.BUILD",
)

# googletest
http_archive(
    name = "com_google_googletest",
    strip_prefix = "googletest-1.14.0",
    urls = ["https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz"],
)
```

实际使用时应填写正确的sha256值。先留占位，后续编译时修正。

- [ ] **Step 2: 创建.bazelrc**

```
# .bazelrc
build --cxxopt=-std=c++17
build --host_cxxopt=-std=c++17
build --enable_bzlmod=false
build --action_env=PATH
test --test_output=errors
```

- [ ] **Step 3: 创建顶层BUILD文件**

```python
# BUILD
package(default_visibility = ["//visibility:public"])

cc_library(
    name = "fly_core",
    deps = [
        "//src/core/cpp:config",
        "//src/serialization/cpp:serialization_macros",
        "//src/export/cpp:export_macros",
    ],
)
```

- [ ] **Step 4: 创建各子目录的空BUILD文件**

为每个子目录创建最小BUILD文件，确保 `bazel build //...` 可以通过。

```python
# src/core/cpp/BUILD
package(default_visibility = ["//visibility:public"])

cc_library(
    name = "fly_core_cpp",
    srcs = ["config.cpp"],
    hdrs = ["config.h"],
    deps = [
        "@com_google_googletest//:gtest",
    ],
)
```

```python
# src/core/export/BUILD
package(default_visibility = ["//visibility:public"])

load("@pybind11//:defs.bzl", "pybind_extension")

pybind_extension(
    name = "_fly_core",
    srcs = ["core_export.cpp"],
    deps = [
        "//src/core/cpp:fly_core_cpp",
        "//src/serialization/cpp:fly_serialization_macros",
        "//src/export/cpp:fly_export_macros",
        "@pybind11//:pybind11",
    ],
)
```

```python
# src/serialization/cpp/BUILD
package(default_visibility = ["//visibility:public"])

cc_library(
    name = "fly_serialization_macros",
    hdrs = ["serialization_macros.h"],
    deps = ["@cereal//:cereal"],
)
```

```python
# src/export/cpp/BUILD
package(default_visibility = ["//visibility:public"])

cc_library(
    name = "fly_export_macros",
    hdrs = ["export_macros.h"],
    deps = [
        "@pybind11//:pybind11",
        "//src/serialization/cpp:fly_serialization_macros",
    ],
)
```

- [ ] **Step 5: 创建third_party/cereal.BUILD**

```python
# third_party/cereal.BUILD
package(default_visibility = ["//visibility:public"])

cc_library(
    name = "cereal",
    hdrs = glob(["include/cereal/**/*.hpp"]),
    includes = ["include"],
)
```

- [ ] **Step 6: 验证Bazel构建**

```bash
bazel build //src/core/cpp:fly_core_cpp
bazel build //src/serialization/cpp:fly_serialization_macros
bazel build //src/export/cpp:fly_export_macros
bazel build //src/core/export:_fly_core.so
```

Expected: 全部编译成功，无错误。`_fly_core.so` 生成可以在Python中 `import` 的共享库。

- [ ] **Step 7: Commit**

```bash
git add -A
git commit -m "feat: initialize Bazel workspace with WORKSPACE, .bazelrc, BUILD files"
```

---

### Task 2: Config单例实现 + pybind11导出

**Files:**
- Create: `src/core/cpp/config.h`
- Create: `src/core/cpp/config.cpp`
- Create: `src/core/export/core_export.cpp`
- Create: `src/core/export/BUILD`
- Create: `src/core/tests/config_test.cpp`
- Create: `src/core/tests/config_test.py`
- Create: `src/core/tests/BUILD`
- Create: `src/core/py/__init__.py`
- Modify: `src/core/cpp/BUILD`

- [ ] **Step 1: 编写Config单例的gtest测试**

```cpp
// src/core/tests/config_test.cpp
#include "config.h"
#include <gtest/gtest.h>
#include <string>

TEST(ConfigTest, SingletonReturnsSameInstance) {
    Config& c1 = Config::instance();
    Config& c2 = Config::instance();
    EXPECT_EQ(&c1, &c2);
}

TEST(ConfigTest, SetAndGetInt) {
    Config& config = Config::instance();
    config.set_int("heartbeat_timeout", 120);
    EXPECT_EQ(config.get_int("heartbeat_timeout"), 120);
}

TEST(ConfigTest, SetAndGetString) {
    Config& config = Config::instance();
    config.set_str("transport_type", "tcp");
    EXPECT_EQ(config.get_str("transport_type"), "tcp");
}

TEST(ConfigTest, GetDefaultInt) {
    // 默认值来自INT_DEFAULTS
    EXPECT_EQ(Config::instance().get_int("master_port"), 8000);
    EXPECT_EQ(Config::instance().get_int("heartbeat_timeout"), 120);
}

TEST(ConfigTest, GetDefaultString) {
    EXPECT_EQ(Config::instance().get_str("transport_type"), "tcp");
}

TEST(ConfigTest, SetBeforeLaunchAllowed) {
    Config& config = Config::instance();
    // 默认未标记为launched，应该可以设置
    config.set_int("backup_threshold", 200);
    EXPECT_EQ(config.get_int("backup_threshold"), 200);
}

TEST(ConfigTest, SetAfterLaunchThrows) {
    Config& config = Config::instance();
    config.mark_workers_launched();
    EXPECT_THROW(config.set_int("some_key", 123), std::runtime_error);
}
```

- [ ] **Step 2: 运行gtest确认编译失败（Config未实现）**

```bash
bazel build //src/core/tests:config_test
```

Expected: FAIL — config.h not found or missing symbols.

- [ ] **Step 3: 实现Config单例**

```cpp
// src/core/cpp/config.h
#pragma once
#include <map>
#include <mutex>
#include <string>
#include <cstdint>
#include <stdexcept>

class Config {
public:
    static Config& instance();

    void set_int(const std::string& key, int64_t value);
    void set_str(const std::string& key, const std::string& value);
    int64_t get_int(const std::string& key) const;
    const std::string& get_str(const std::string& key) const;

    void mark_workers_launched();
    bool is_workers_launched() const { return workers_launched_; }

    // Reset for testing purposes
    void reset();

private:
    Config();
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;

    std::map<std::string, int64_t> int_values_;
    std::map<std::string, std::string> str_values_;
    bool workers_launched_ = false;

    static const std::map<std::string, int64_t> INT_DEFAULTS;
    static const std::map<std::string, std::string> STR_DEFAULTS;
};
```

```cpp
// src/core/cpp/config.cpp
#include "config.h"

Config& Config::instance() {
    static Config config;
    return config;
}

Config::Config() {
    int_values_ = INT_DEFAULTS;
    str_values_ = STR_DEFAULTS;
}

void Config::set_int(const std::string& key, int64_t value) {
    if (workers_launched_) {
        throw std::runtime_error("Config must be set before workers are launched");
    }
    int_values_[key] = value;
}

void Config::set_str(const std::string& key, const std::string& value) {
    if (workers_launched_) {
        throw std::runtime_error("Config must be set before workers are launched");
    }
    str_values_[key] = value;
}

int64_t Config::get_int(const std::string& key) const {
    auto it = int_values_.find(key);
    auto default_it = INT_DEFAULTS.find(key);
    if (it != int_values_.end()) return it->second;
    if (default_it != INT_DEFAULTS.end()) return default_it->second;
    throw std::runtime_error("Unknown config key: " + key);
}

const std::string& Config::get_str(const std::string& key) const {
    auto it = str_values_.find(key);
    auto default_it = STR_DEFAULTS.find(key);
    if (it != str_values_.end()) return it->second;
    if (default_it != STR_DEFAULTS.end()) return default_it->second;
    static const std::string empty = "";
    return empty;
}

void Config::mark_workers_launched() { workers_launched_ = true; }

void Config::reset() {
    int_values_ = INT_DEFAULTS;
    str_values_ = STR_DEFAULTS;
    workers_launched_ = false;
}

const std::map<std::string, int64_t> Config::INT_DEFAULTS = {
    {"master_port", 8000},
    {"heartbeat_timeout", 120},
    {"heartbeat_interval", 5},
    {"backup_threshold", 100},
    {"aggregation_threshold", 1048576},
    {"large_file_threshold", 10485760},
    {"block_size", 134217728},
    {"track_writes", 0},
    {"data_server_threads", 1},
};

const std::map<std::string, std::string> Config::STR_DEFAULTS = {
    {"transport_type", "tcp"},
};
```

- [ ] **Step 4: 编译并运行gtest**

```bash
bazel test //src/core/tests:config_test
```

Expected: 7/7 PASS

- [ ] **Step 5: 实现pybind11导出**

```cpp
// src/core/export/core_export.cpp
#include <pybind11/pybind11.h>
#include "config.h"

namespace py = pybind11;

PYBIND11_MODULE(_fly_core, m) {
    m.doc() = "Fly core C++ module";

    py::class_<Config>(m, "Config")
        .def("set_int", [](Config& c, const std::string& key, int64_t value) {
            c.set_int(key, value);
        })
        .def("set_str", [](Config& c, const std::string& key, const std::string& value) {
            c.set_str(key, value);
        })
        .def("get_int", &Config::get_int)
        .def("get_str", &Config::get_str)
        .def("mark_workers_launched", &Config::mark_workers_launched);

    m.def("get_config", []() { return &Config::instance(); });
}
```

- [ ] **Step 6: 创建Python测试和__init__.py**

```python
# src/core/py/__init__.py
from _fly_core import Config, get_config

__all__ = ["Config", "get_config"]
```

```python
# src/core/tests/config_test.py
import pytest

def test_config_singleton():
    from _fly_core import get_config
    c1 = get_config()
    c2 = get_config()
    assert c1 is c2

def test_config_set_get_int():
    from _fly_core import get_config
    config = get_config()
    config.set_int("heartbeat_timeout", 60)
    assert config.get_int("heartbeat_timeout") == 60

def test_config_set_get_str():
    from _fly_core import get_config
    config = get_config()
    config.set_str("transport_type", "rdma")
    assert config.get_str("transport_type") == "rdma"

def test_config_defaults():
    from _fly_core import get_config
    config = get_config()
    assert config.get_int("master_port") == 8000
    assert config.get_str("transport_type") == "tcp"

def test_config_import_from_package():
    """验证可以通过Python包import，而不是直接import .so"""
    from fly.core import get_config
    config = get_config()
    assert config.get_int("master_port") == 8000
```

- [ ] **Step 7: 运行Python测试**

```bash
bazel test //src/core/tests:config_py_test
```

Expected: 4/4 PASS

- [ ] **Step 8: Commit**

```bash
git add -A
git commit -m "feat: implement Config singleton with pybind11 export and tests"
```

---

### Task 3: 序列化宏 (serialization_macros.h)

**Files:**
- Create: `src/serialization/cpp/serialization_macros.h`
- Create: `src/serialization/tests/serialization_test.cpp`
- Create: `src/serialization/tests/BUILD`
- Create: `third_party/cereal.BUILD`（如果Task 1未完善）

- [ ] **Step 1: 编写序列化宏的gtest测试**

```cpp
// src/serialization/tests/serialization_test.cpp
#include "serialization_macros.h"
#include <gtest/gtest.h>
#include <string>

struct TestMessage {
    uint64_t id = 0;
    std::string name;
    int32_t value = 0;

    FLY_SERIALIZE_DECLARE() {
        FLY_SERIALIZE_FIELDS(id, name, value);
    }
};

TEST(SerializationTest, EncodeAndDecode) {
    TestMessage original;
    original.id = 42;
    original.name = "hello";
    original.value = -7;

    std::string encoded;
    FLY_ENCODE(original, encoded);
    EXPECT_GT(encoded.size(), 0u);

    TestMessage decoded;
    FLY_DECODE(encoded, TestMessage, decoded);
    EXPECT_EQ(decoded.id, 42u);
    EXPECT_EQ(decoded.name, "hello");
    EXPECT_EQ(decoded.value, -7);
}

TEST(SerializationTest, EncodeEmptyString) {
    TestMessage original;
    original.id = 1;
    original.name = "";
    original.value = 0;

    std::string encoded;
    FLY_ENCODE(original, encoded);

    TestMessage decoded;
    FLY_DECODE(encoded, TestMessage, decoded);
    EXPECT_EQ(decoded.name, "");
}

struct BaseMessage {
    uint32_t msg_type = 0;
    FLY_SERIALIZE_DECLARE() {
        FLY_SERIALIZE_FIELDS(msg_type);
    }
};

struct DerivedMessage : BaseMessage {
    std::string payload;

    FLY_SERIALIZE_DECLARE() {
        FLY_SERIALIZE_BASE(BaseMessage);
        FLY_SERIALIZE_FIELDS(payload);
    }
};

TEST(SerializationTest, InheritanceSerialize) {
    DerivedMessage original;
    original.msg_type = 5;
    original.payload = "test_payload";

    std::string encoded;
    FLY_ENCODE(original, encoded);

    DerivedMessage decoded;
    FLY_DECODE(encoded, DerivedMessage, decoded);
    EXPECT_EQ(decoded.msg_type, 5u);
    EXPECT_EQ(decoded.payload, "test_payload");
}
```

- [ ] **Step 2: 运行测试确认失败**

```bash
bazel build //src/serialization/tests:serialization_test
```

Expected: FAIL — serialization_macros.h not found.

- [ ] **Step 3: 实现serialization_macros.h**

```cpp
// src/serialization/cpp/serialization_macros.h
#pragma once

#include <cereal/archives/binary.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/vector.hpp>
#include <cereal/types/map.hpp>
#include <cereal/types/polymorphic.hpp>
#include <sstream>
#include <string>

// 序列化函数声明
#define FLY_SERIALIZE_DECLARE() \
    template<class Archive> \
    void serialize(Archive& ar)

// 序列化多个字段
#define FLY_SERIALIZE_FIELDS(...) ar(__VA_ARGS__);

// 序列化基类
#define FLY_SERIALIZE_BASE(base_class) \
    ar(cereal::base_class<base_class>(this));

// 编码消息为字符串
#define FLY_ENCODE(msg, output) \
    do { \
        std::ostringstream oss; \
        cereal::BinaryOutputArchive archive(oss); \
        archive(msg); \
        output = oss.str(); \
    } while(0)

// 解码消息从字符串
#define FLY_DECODE(data, msg_type, output) \
    do { \
        std::istringstream iss(data); \
        cereal::BinaryInputArchive archive(iss); \
        msg_type msg; \
        archive(msg); \
        output = msg; \
    } while(0)

// 流式编码
#define FLY_ENCODE_STREAM(file_stream, msg) \
    do { \
        cereal::BinaryOutputArchive archive(file_stream); \
        archive(msg); \
    } while(0)

// 流式解码
#define FLY_DECODE_STREAM(file_stream, msg_type, output) \
    do { \
        cereal::BinaryInputArchive archive(file_stream); \
        msg_type msg; \
        archive(msg); \
        output = msg; \
    } while(0)
```

- [ ] **Step 4: 运行测试确认通过**

```bash
bazel test //src/serialization/tests:serialization_test
```

Expected: 3/3 PASS

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "feat: implement serialization macros with cereal and gtest"
```

---

### Task 4: 导出宏 (export_macros.h)

**Files:**
- Create: `src/export/cpp/export_macros.h`
- Create: `src/export/tests/export_test.cpp`
- Create: `src/export/tests/BUILD`

- [ ] **Step 1: 编写导出宏的gtest测试**

测试需要一个mock结构体来验证宏是否编译通过。

```cpp
// src/export/tests/export_test.cpp
// 注意：导出宏的完整测试需要pybind11编译为so，这里只验证C++部分结构体可编译
#include "export_macros.h"
#include "serialization_macros.h"
#include <gtest/gtest.h>

struct MockData {
    int a = 0;
    double b = 0.0;
    std::string c;

    FLY_SERIALIZE_DECLARE() {
        FLY_SERIALIZE_FIELDS(a, b, c);
    }
};

TEST(ExportMacrosTest, StructWithSerializeCompiles) {
    MockData d;
    d.a = 1;
    d.b = 2.5;
    d.c = "test";
    EXPECT_EQ(d.a, 1);
    EXPECT_EQ(d.b, 2.5);
    EXPECT_EQ(d.c, "test");
}

TEST(ExportMacrosTest, SerializeRoundTrip) {
    MockData original;
    original.a = 42;
    original.b = 3.14;
    original.c = "hello";

    std::string encoded;
    FLY_ENCODE(original, encoded);

    MockData decoded;
    FLY_DECODE(encoded, MockData, decoded);
    EXPECT_EQ(decoded.a, 42);
    EXPECT_DOUBLE_EQ(decoded.b, 3.14);
    EXPECT_EQ(decoded.c, "hello");
}
```

- [ ] **Step 2: 实现export_macros.h**

```cpp
// src/export/cpp/export_macros.h
#pragma once

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <memory>
#include <string>

namespace py = pybind11;

// ==================== Pickle导出宏 ====================

#define FLY_EXPORT_PICKLE(class_type) \
    .def(py::pickle( \
        [](const class_type& obj) { \
            std::string serialized; \
            FLY_ENCODE(obj, serialized); \
            return py::bytes(serialized); \
        }, \
        [](py::bytes bytes) { \
            std::string data = bytes; \
            class_type obj; \
            FLY_DECODE(data, class_type, obj); \
            return obj; \
        } \
    ))

#define FLY_EXPORT_PICKLE_SHARED_PTR(class_type) \
    .def(py::pickle( \
        [](const std::shared_ptr<class_type>& obj) { \
            std::string serialized; \
            FLY_ENCODE(*obj, serialized); \
            return py::bytes(serialized); \
        }, \
        [](py::bytes bytes) { \
            std::string data = bytes; \
            auto obj = std::make_shared<class_type>(); \
            FLY_DECODE(data, class_type, *obj); \
            return obj; \
        } \
    ))

// ==================== 类导出宏 ====================

#define FLY_EXPORT_CLASS_WITH_NAME(module, class_type, export_name, ...) \
    py::class_<class_type>(module, export_name) \
        .def(py::init<>()) \
        __VA_ARGS__ \
        FLY_EXPORT_PICKLE(class_type)

#define FLY_EXPORT_CLASS(module, class_type, ...) \
    FLY_EXPORT_CLASS_WITH_NAME(module, class_type, #class_type, __VA_ARGS__)

#define FLY_EXPORT_CLASS_SHARED_PTR_WITH_NAME(module, class_type, export_name, ...) \
    py::class_<class_type, std::shared_ptr<class_type>>(module, export_name) \
        .def(py::init<>()) \
        __VA_ARGS__ \
        FLY_EXPORT_PICKLE_SHARED_PTR(class_type)

#define FLY_EXPORT_CLASS_SHARED_PTR(module, class_type, ...) \
    FLY_EXPORT_CLASS_SHARED_PTR_WITH_NAME(module, class_type, #class_type, __VA_ARGS__)

// ==================== 属性与方法导出宏 ====================

#define FLY_EXPORT_ATTR(name, member) \
    .def_readwrite(#name, member)

#define FLY_EXPORT_ATTR_WITH_NAME(name, member) \
    .def_readwrite(name, member)

#define FLY_EXPORT_METHOD(name, func) \
    .def(#name, func)

#define FLY_EXPORT_METHOD_WITH_NAME(name, func) \
    .def(name, func)
```

- [ ] **Step 3: 运行测试**

```bash
bazel test //src/export/tests:export_test
```

Expected: 2/2 PASS

- [ ] **Step 4: Commit**

```bash
git add -A
git commit -m "feat: implement export macros and verify with gtest"
```

---

### Task 5: 端到端冒烟测试

**Files:**
- Create: `qa/smoke_test.py`

验证Config、序列化宏、导出宏三者联合工作。

- [ ] **Step 1: 编写Python冒烟测试**

```python
# qa/smoke_test.py
"""Layer 0 smoke test: Config + serialization + export integration"""
import pytest

def test_config_and_core_module():
    """Verify _fly_core module loads and Config works end-to-end"""
    from _fly_core import get_config, Config

    config = get_config()
    assert config is not None
    assert config.get_int("master_port") == 8000
    assert config.get_str("transport_type") == "tcp"

    config.set_int("heartbeat_timeout", 99)
    assert config.get_int("heartbeat_timeout") == 99

    config.set_str("transport_type", "rdma")
    assert config.get_str("transport_type") == "rdma"

def test_config_immutable_after_launch():
    """Verify Config throws when set after workers launched"""
    from _fly_core import get_config
    config = get_config()
    config.mark_workers_launched()
    with pytest.raises(RuntimeError):
        config.set_int("any_key", 1)
```

- [ ] **Step 2: 运行冒烟测试**

```bash
bazel test //qa:smoke_test
```

Expected: 2/2 PASS

- [ ] **Step 3: Commit**

```bash
git add -A
git commit -m "test: add Layer 0 smoke test for Config + serialization + export"
```

---

## 验收标准

Layer 0 完成条件：
1. `bazel build //...` 编译通过
2. `bazel test //...` 所有测试通过
3. Config单例可在C++和Python中正常使用
4. 序列化宏可编码/解码结构体
5. 导出宏可编译（pybind11导出需要后续层验证Python层功能）
6. 所有代码已commit到git