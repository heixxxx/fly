# Container 模块 — 容器别名层与自定义容器

## 模块概述

**位置**: `src/container/`（2026-09-06 新建）

容器的完整闭环：**容器类型别名层**（当前绑定 std 实现，可整体替换为自定义实现）
+ **自定义容器实现**（领域数据结构）。依赖 common(types) 与 common(serialization)。

## 容器别名层（`cpp/container_aliases.h`）

`CM*` 容器别名的当前绑定，**可替换别名层**——业务代码只使用 `CM*` 名字，替换
底层实现（如自定义 string、带分配器的 vector）时仅改本头绑定，业务零改动：

| 别名 | 当前绑定 | 别名 | 当前绑定 |
|------|---------|------|---------|
| `CMString` | `std::string` | `CMList<T>` | `std::list<T>` |
| `CMVector<T>` | `std::vector<T>` | `CMDeque<T>` | `std::deque<T>` |
| `CMMap<K,V>` | `std::map<K,V>` | `CMQueue<T>` | `std::queue<T>` |
| `CMUnorderedMap<K,V>` | `std::unordered_map<K,V>` | `CMStack<T>` | `std::stack<T>` |
| `CMSet<T>` | `std::set<T>` | `CMMapKV<K,V>` | `std::pair<K,V>` |
| `CMUnorderedSet<T>` | `std::unordered_set<T>` | | |

附带 `std::hash<std::tuple<...>>` 特化（支撑 `CMUnorderedSet<std::tuple<...>>`）。

本头聚合 include `common/types/cpp/pointer_aliases.h`（智能指针族）——使用方
include 本头即可获得全部 `CM*` 符号。

## 自定义容器（`cpp/lookup_table.h`）

`CMLookupTable` / `CMLookupTableTemplate`——框架级公共查找表（Liberty 等 EDA
查找表族的统一保存与插值查询）：

- 两级组织：模板（轴变量名 + 各轴默认索引，库内共享）+ 表（values 行优先展平 +
  模板引用或自足轴定义，表级索引可按轴覆盖）；
- `resolve_template(tmpl)` 展开为可插值状态（校验 values 数量与轴长度乘积）；
- `interpolate(coords)` N 维多线性插值（维度 ≤ 3，坐标超界 clamp；单点轴合法），
  算法引擎 C++ 直调零开销；
- `FLY_SERIALIZE` 序列化 + `FLY_EXPORT_SERIALIZE_PICKLE` 绑定——可作为 fly db
  对象持久化（pickle 协议等价落库）。

首个使用方：EMIR lib 库 db（Liberty 功耗/时序表入库）；后续 timing/power/
current/switching 各 db 复用。

## Python 绑定（`_fly_container.so`）

| 导出名 | C++ 类型 |
|--------|---------|
| `EXCMLookupTable` | `fly::CMLookupTable`（pickle + resolve_template/interpolate/is_ready） |
| `EXCMLookupTableTemplate` | `fly::CMLookupTableTemplate`（pickle） |

Python 侧：`from container import EXCMLookupTable`（包根 re-export）。

## BUILD 目标

| target | 说明 |
|--------|------|
| `fly_container_aliases` | 别名层（纯头，依赖 fly_common_types）——新底座 |
| `fly_container` | 自定义容器（依赖 fly_serialization） |
| `fly_container_so` | 共享库形态 |
| `_fly_container.so` | nanobind 绑定（main data 接线，`import _fly_container`） |
