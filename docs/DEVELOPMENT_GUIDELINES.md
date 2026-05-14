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

```cpp
// 声明序列化支持
FLY_SERIALIZE_DECLARE() {
    FLY_SERIALIZE_FIELDS(id, name, value);
}

// 编解码操作
CMString serialized;
FLY_ENCODE(message, serialized);
FLY_DECODE(serialized, MessageType, decoded);
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
# 构建
bazel build //src/...

# 测试
bazel test //src/...
pytest qa/smoke_test.py -v

# clangd 配置
bazel run //:refresh_compile_commands

# Git 操作
git status
git add -A
git commit -m "feat: add new feature"
git push
```

---

**文档更新历史**:

- 2026-05-14: 初版创建，整合目录结构、命名规范、宏抽象、测试规范
- 后续新增规范请在此处记录更新历史