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
| 序列化 | `FLY_SERIALIZE_` | `FLY_SERIALIZE_DECLARE()` |
| 序列化操作 | `FLY_ENCODE` / `FLY_DECODE` | `FLY_ENCODE(msg, out)` |
| 导出模块 | `FLY_EXPORT_MODULE_` | `FLY_EXPORT_MODULE_BEGIN()` |
| 导出类 | `FLY_EXPORT_CLASS` | `FLY_EXPORT_CLASS_NO_INIT()` |
| 导出方法 | `FLY_EXPORT_METHOD` | `FLY_EXPORT_METHOD(name, func)` |

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
| zpp_bits | `FLY_SERIALIZE_*` | cereal / protobuf |
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
    FLY_SERIALIZE(1, &Simple::id, &Simple::name)
    //                          ↑ 成员指针语法 &Type::field
};

// 2. 完整形式（需要版本判断逻辑）
struct IndexEntry {
    FLY_SERIALIZE_BEGIN(2)                   // Version 2
        FLY_FIELD(s, o, object_name);
        FLY_FIELD(s, o, offset);
        if (version >= 2) {                  // v2 新增字段
            FLY_FIELD(s, o, compression_type);
        }
    FLY_SERIALIZE_END
};
```

`FLY_SERIALIZE(N, ...)` 展开为：
```cpp
template<typename S> void serialize(S& s) {
    s.ext(*this, fly::Version<N>{}, [](S& s, auto& o, size_t) {
        fly_ser::serialize_fields(s, o, &Type::field1, &Type::field2, ...);
        //          ↑ C++17 fold expression: ((field_single(s, o.*member)), ...)
    });
}
```

`FLY_SERIALIZE_BEGIN(N) / FLY_SERIALIZE_END` 展开为：
```cpp
template<typename S> void serialize(S& s) {
    s.ext(*this, fly::Version<N>{}, [](S& s, auto& o, size_t version) {
        // 用户代码（可访问 version 变量）
    });
}
```

#### 4.2.2 字段序列化宏

`FLY_FIELD(s, o, field)` 是统一宏，自动检测字段类型并分发：

| 字段类型 | 分发目标 | 说明 |
|----------|----------|------|
| `int32_t`, `double` 等 fundamental | `fly_ser::value()` | 自动 sizeof 推导 1/2/4/8b |
| `std::string` | `fly_ser::text()` | 1字节长度编码 |
| `std::vector<int>` | `fly_ser::container()` | bulk copy (连续内存) |
| `std::vector<Obj>` | `fly_ser::container()` | 逐个 serialize() |
| `std::map<K,V>` / `unordered_map` | `s.ext(StdMap{...})` | key+val 自动分发 |
| 具有 `serialize()` 的类型 | `s.object()` | 递归序列化 |

**当需要自定义 lambda 时使用：**

| 宏 | 用途 |
|----|------|
| `FLY_VEC_F(s, o, field, lambda)` | vector 自定义元素序列化 |
| `FLY_MAP(s, o, field, lambda)` | map 自定义 key/val 序列化 |
| `FLY_BOOL(s, o, field)` | bool（单独处理，非 fundamental） |

**lambda 内变量辅助函数**（用于序列化变量而非 `o.field`）：

| 函数 | 用途 |
|------|------|
| `fly_ser::text(s, var)` | 字符串变量 |
| `fly_ser::value(s, var)` | 定长值变量 |
| `fly_ser::container(s, var)` | 容器变量（自动 dispatch） |
| `fly_ser::object(s, var)` | 对象变量 |

**生产代码对比**（IndexData：`map<string, vector<IndexEntry>>`）：

```
// 原始（直接 bitsery API）:
s.ext(o.entries, bitsery::ext::StdMap{FLY_MAX_SIZE},
    [](auto& s2, CMString& key, CMVector<IndexEntry>& val) {
        s2.text1b(key, FLY_MAX_SIZE);
        s2.container(val, FLY_MAX_SIZE, [](auto& s3, IndexEntry& e) {
            s3.object(e);
        });
    });

// 现在（1 行）:
FLY_FIELD(s, o, entries);
```

`FLY_SERIALIZE_BEGIN(N)` 展开为：
```cpp
template<typename S> void serialize(S& s) {
    s.ext(*this, fly::Version<N>{}, [](S& s, auto& o, size_t version) {
```

`FLY_SERIALIZE_END` 展开为 `);}`，关闭 `s.ext()` 调用和函数。

`o` = struct 引用（通过 `fly::Version` 传入），`version` = 当前数据版本号。

#### 4.2.2 字段序列化宏

**推荐使用 `FLY_FIELD`（统一宏）**——自动检测字段类型并分发到正确的序列化方式：

```cpp
// 一行搞定任何类型的字段
FLY_FIELD(s, o, id);           // int32_t → auto-sized value
FLY_FIELD(s, o, name);         // string → text (1b length encoding)
FLY_FIELD(s, o, scores);       // vector<int> → bulk container(copy)
FLY_FIELD(s, o, children);     // vector<Obj> → per-element container(object)
FLY_FIELD(s, o, tags);         // map<string,int> → StdMap(key→text, val→value)
FLY_FIELD(s, o, grouped);      // map<string,vector<Obj>> → StdMap(key→text, val→container(auto))
FLY_FIELD(s, o, inner);        // Obj → object(serialize)
```

**仅当需要自定义 lambda 时才使用其他宏：**

| 宏 | 用途 | 示例 |
|----|------|------|
| `FLY_VEC_F(s, o, f, lambda)` | 容器字段（自定义元素序列化） | `FLY_VEC_F(s, o, strs, [](auto& s, auto& e) { fly_ser::text(s, e); })` |
| `FLY_MAP(s, o, f, lambda)` | map 字段（自定义 key/val 序列化） | `FLY_MAP(s, o, m, [](auto& s, auto& k, auto& v) { ... })` |
| `FLY_BOOL(s, o, f)` | bool 值 | `FLY_BOOL(s, o, flag)` |
| `FLY_VAL, FLY_STR, FLY_VEC, FLY_OBJ` | 精确控制（极少需要） | 个别场景 |

**lambda 内变量辅助函数**（在 `FLY_VEC_F` 等 lambda 内部使用，用于序列化变量而非 struct 字段）：

| 函数 | 用途 |
|------|------|
| `fly_ser::text(s, var)` | 序列化字符串变量 |
| `fly_ser::value(s, var)` | 序列化定长值变量（自动 sizeof） |
| `fly_ser::container(s, var)` | 序列化容器变量（自动 dispatch） |
| `fly_ser::object(s, var)` | 序列化对象变量 |

**生产代码对比**（IndexData：`map<string, vector<IndexEntry>>` 的序列化）：

```
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

// 之后：1行 + 隐藏所有细节
FLY_SERIALIZE_BEGIN(1) {
    FLY_FIELD(s, o, entries);
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

```cpp
// 模块定义
FLY_EXPORT_MODULE_BEGIN(_fly_module)

// 类导出（无构造函数，如单例）
FLY_EXPORT_CLASS_NO_INIT(m, Config,
    FLY_EXPORT_METHOD(get_int, &Config::get_int)
    FLY_EXPORT_METHOD(set_int, &Config::set_int)
);

// 函数导出（返回引用）
FLY_EXPORT_FUNCTION_REF_WITH_NAME(m, "get_config", []() { return &Config::instance(); });

FLY_EXPORT_MODULE_END()
```

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
| zpp_bits 而非 cereal | 单头文件、14x 更快、C++20 原生 |
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

### 12.3 *this 在宏中的 GCC 兼容性问题

**问题**：宏 `FLY_VAL4B(s, *this, value)` 展开为 `s.value4b((*this).value)`，但在 GCC 模板上下文中编译失败：
```
error: request for member 'value' in '(TestData*)this'
```

**根因**：GCC 在模板成员函数中对 `*this` 的处理与其他编译器不同，宏展开后的 `(*this).value` 被错误解析为 `this->value`。

**规范**：
- **禁止**在 `FLY_*` 字段宏中使用 `*this` 作为对象参数
- 始终使用 `fly::Version<N>` 包裹字段序列化：
  ```cpp
  FLY_SERIALIZE_DECLARE() {
      s.ext(*this, fly::Version<1>{}, [](auto& s, auto& o, size_t) {
          FLY_VAL4B(s, o, field);  // o 是命名引用，非 *this
      });
  }
  ```
- 对于 `StdMap` 等 lambda 中直接传入变量（非 struct 成员）的场景，使用直接的 bitsery API 调用，因 `FLY_*` 宏设计为 `obj.field` 模式。

### 12.4 测试代码与生产代码序列化风格不一致

**问题**：测试文件中的 `serialize()` 直接调用 `s.value4b(value)` 而非宏，导致未来切换序列化后端时测试代码也需要修改。

**规范**：测试代码与生产代码使用相同的宏模式。所有 `serialize()` 方法必须使用 `FLY_SERIALIZE_BEGIN(N) { ... } FLY_SERIALIZE_END` 声明，内部使用 `FLY_VAL4B`、`FLY_TEXT` 等字段宏。

---

**文档更新历史**:

- 2026-05-14: 初版创建，整合目录结构、命名规范、宏抽象、测试规范
- 2026-05-14: 更新序列化宏文档（FLY_SERIALIZE/DECLARE + BOOST_PP），新增开发教训章节
- 2026-05-14: 新增 `fly-build` skill — 构建必须使用 `./fly.sh`，禁止裸 `bazel build`