# Common 模块 — 公共类型定义

## 模块概述

**位置**: `src/common/cpp/`

提供全项目统一使用的容器类型别名，所有模块依赖此基础层。

---

## 核心文件

| 文件 | 说明 |
|------|------|
| `common_types.h` | 容器类型别名定义 |
| `BUILD` | Bazel 构建配置 |

---

## 类型别名

所有类型在 `fly` 命名空间内定义，同时提供全局 `using` 声明。

| 别名 | 原始类型 | 用途 |
|------|----------|------|
| `CMString` | `std::string` | 统一字符串类型 |
| `CMVector<T>` | `std::vector<T>` | 动态数组 |
| `CMMap<K,V>` | `std::map<K,V>` | 有序映射 |
| `CMUnorderedMap<K,V>` | `std::unordered_map<K,V>` | 无序映射 |
| `CMSet<T>` | `std::set<T>` | 有序集合 |
| `CMUnorderedSet<T>` | `std::unordered_set<T>` | 无序集合 |
| `CMList<T>` | `std::list<T>` | 双向链表 |
| `CMDeque<T>` | `std::deque<T>` | 双端队列 |
| `CMQueue<T>` | `std::queue<T>` | 队列 |
| `CMStack<T>` | `std::stack<T>` | 栈 |
| `CMMapKV<K,V>` | `std::pair<K,V>` | 键值对 |

---

## 使用方式

```cpp
#include <common/cpp/common_types.h>

CMString name = "hello";
CMVector<int> ids = {1, 2, 3};
CMMap<CMString, int64_t> config_values;
```

---

## 设计决策

| 决策 | 原因 |
|------|------|
| `CM` 前缀 (Common) | 统一命名空间，便于全局搜索和替换底层实现 |
| 全局 `using` 声明 | 允许直接使用 `CMString` 而无需 `fly::` 前缀 |
| 无运行时依赖 | 纯头文件类型别名，零开销抽象 |

---

## 实现流程

```
common_types.h
  ├── 包含标准库头文件 (map, vector, string, ...)
  ├── namespace fly { 定义类型别名 }
  └── 全局 using 声明 (using fly::CMString; ...)
```

此模块无 `.cpp` 文件，纯头文件实现。
