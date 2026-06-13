# Export 模块 — Python 导出宏

## 模块概述

**位置**: `src/export/cpp/`

提供统一的 nanobind 导出宏层，所有 C++ 到 Python 的绑定必须通过此模块的宏完成。

---

## 核心文件

| 文件 | 说明 |
|------|------|
| `export_macros.h` | 导出宏定义 |

---

## 宏体系

### 模块定义

```cpp
FLY_EXPORT_MODULE(_fly_module) {
    // 所有导出代码
}
```

内部展开为 `NB_MODULE(_fly_module, m)`，宏直接使用 `m` 变量。

### 类导出

| 宏 | 用途 |
|----|------|
| `FLY_EXPORT_CLASS(Type, "name")` | 导出普通类 |
| `FLY_EXPORT_CLASS_SHARED_PTR(Type, "name")` | 导出支持 shared_ptr 的类 |

### 类成员导出

| 宏 | 用途 |
|----|------|
| `FLY_EXPORT_INIT(...)` | 导出构造函数 |
| `FLY_EXPORT_DEF("name", lambda)` | Lambda/复杂方法绑定 |
| `FLY_EXPORT_ATTR("name", &Type::field)` | 可读写属性 |
| `FLY_EXPORT_READONLY_ATTR("name", &Type::field)` | 只读属性 |
| `FLY_EXPORT_METHOD("name", &Type::func)` | 成员方法 |
| `FLY_EXPORT_STATIC_METHOD("name", &Type::func)` | 静态方法 |
| `FLY_EXPORT_PROPERTY("name", getter, setter)` | 计算属性（读写） |
| `FLY_EXPORT_READONLY_PROPERTY("name", getter)` | 计算属性（只读） |
| `FLY_EXPORT_SERIALIZE(Type)` | Pickle 支持 (`__getstate__`/`__setstate__` + `is_cpp`) |

### 枚举/函数导出

| 宏 | 用途 |
|----|------|
| `FLY_EXPORT_ENUM(EnumType, "name")` | 导出枚举 |
| `FLY_EXPORT_ENUM_VALUE("name", value)` | 导出枚举值 |
| `FLY_EXPORT_FUNCTION("name", lambda)` | 导出函数（值返回） |
| `FLY_EXPORT_FUNCTION("name", lambda)` | 导出函数（引用返回） |

---

## 命名规范

### 导出类型命名

**格式**: `EX<ModuleAbbr><TypeName>`

| 模块 | 缩写 | 示例 |
|------|------|------|
| storage | Stg | `EXStgDatabase`, `EXStgIndexEntry` |
| core | Core | `EXCoreConfig` |
| network | Net | `EXNetTransportEvent` |
| agent | Agent | `EXAgentMaster`, `EXAgentWorker` |

### 导出函数命名

**格式**: `ex_<module_abbr>_<function_name>`

| 示例 | 说明 |
|------|------|
| `ex_stg_create_database` | 创建 Database |
| `ex_stg_get_data_service` | 获取 DataService 单例 |
| `ex_core_get_config` | 获取 Config 单例 |

---

## FLY_EXPORT_SERIALIZE 实现

```cpp
#define FLY_EXPORT_SERIALIZE(Cls) \
    .def("__getstate__", [](const Cls& obj) -> fly_export::bytes { \
        std::string serialized; \
        FLY_ENCODE(obj, serialized); \
        return fly_export::bytes(serialized.data(), serialized.size()); \
    }) \
    .def("__setstate__", [](Cls& obj, fly_export::bytes b) { \
        std::string data(b.c_str(), b.size()); \
        ::new (&obj) Cls(); \
        FLY_DECODE(data, Cls, obj); \
    }) \
    .def_prop_ro("is_cpp", [](const Cls&) { return true; })
```

- `__getstate__`: FLY_ENCODE → bytes
- `__setstate__`: placement new + FLY_DECODE
- `is_cpp` 属性: Python wrapper 判断对象是否为 C++ 导出类型

---

## 设计决策

| 决策 | 原因 |
|------|------|
| 始终要求导出名称 | 避免 C++ 符号泄露到 Python，统一命名空间 |
| 无 module_var 参数 | NB_MODULE 固定定义 `m`，宏直接使用 |
| 用户写大括号 | `FLY_EXPORT_MODULE(name) { }` 简洁直观 |
| placement new __setstate__ | 确保未初始化对象安全反序列化 |
