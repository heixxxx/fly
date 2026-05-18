# Serialization 模块 — 序列化基础设施

## 模块概述

**位置**: `src/serialization/cpp/`

提供全项目统一的序列化宏层，底层使用 bitsery 实现。所有 C++ 序列化操作必须通过此模块的宏完成，不得直接调用 bitsery API。

---

## 核心文件

| 文件 | 说明 |
|------|------|
| `serialization_macros.h` | 序列化宏定义（FLY_SERIALIZE, FLY_ENCODE 等） |
| `bitsery_ext/version.h` | bitsery 版本扩展 |
| `object_header.h` | 对象头结构（用于存储层标记 Python 类型名） |

---

## 宏体系

### 声明宏

#### FLY_SERIALIZE（简洁形式）

适用于所有字段存在于版本 1 的场景：

```cpp
struct Simple {
    int32_t id;
    CMString name;
    FLY_SERIALIZE(id, name);  // 展开为 FLY_SERIALIZE_BEGIN(1) { ... } FLY_SERIALIZE_END
};
```

#### FLY_SERIALIZE_BEGIN / FLY_SERIALIZE_END（完整形式）

适用于需要版本判断逻辑的场景：

```cpp
struct IndexEntry {
    FLY_SERIALIZE_BEGIN(2)
        FLY_FIELD(object_name);
        FLY_FIELD(offset);
        if (version >= 2) {
            FLY_FIELD(compression_type);
        }
    FLY_SERIALIZE_END
};
```

### 字段宏

| 宏 | 用途 | 分发目标 |
|----|------|----------|
| `FLY_FIELD(field)` | **统一宏（推荐）** | 自动检测类型并分发 |
| `FLY_VAL(field)` | 定长值 | `fly_ser::value()` |
| `FLY_STR(field)` | 字符串 | `fly_ser::text()` |
| `FLY_VEC(field)` | 容器 | `fly_ser::container()` |
| `FLY_VEC_F(field, fn)` | 容器（自定义元素序列化） | `s.container()` |
| `FLY_MAP(field, fn)` | 映射 | `fly_ser::map()` |
| `FLY_OBJ(field)` | 对象 | `s.object()` |
| `FLY_BOOL(field)` | 布尔值 | `s.boolValue()` |

### FLY_FIELD 自动分发逻辑

```
FLY_FIELD(field)
  ├── is_map_v → StdMap + 自动嵌套
  ├── is_vector_v → container (POD bulk / object per-elem)
  ├── is_string_v → text1b
  ├── is_fundamental || is_enum → value (auto sizeof)
  └── else → s.object() (递归 serialize)
```

### 编解码宏

| 宏 | 操作 | 用途 |
|----|------|------|
| `FLY_ENCODE(obj, out_str)` | 编码到 CMString | 文件/网络传输 |
| `FLY_DECODE(str, Type, obj)` | 从 CMString 解码 | 读取 |
| `FLY_ENCODE_TO_BYTES(obj, buf)` | 编码到 FlyBuffer | Python 绑定 |
| `FLY_DECODE_FROM_BYTES(buf, Type, obj)` | 从 FlyBuffer 解码 | Python 绑定 |

---

## 类型系统

### FlyTrustedConfig

```cpp
struct FlyTrustedConfig {
    static constexpr bool CheckDataErrors = false;  // 跳过 maxSize 验证
};
```

内部数据使用 TrustedConfig，禁用大小验证。`FLY_MAX_SIZE` 为占位符，不实际校验。

### fly_ser 辅助命名空间

```cpp
namespace fly_ser {
    // 类型特征
    constexpr bool is_map_v<T>;
    constexpr bool is_vector_v<T>;
    constexpr bool is_string_v<T>;

    // 操作函数
    void value(S& s, T& v);       // sizeof 自动分发
    void text(S& s, T& str);      // text1b
    void container(S& s, T& c);   // 自动 POD/object 分发
    void object(S& s, T& obj);    // 递归 serialize
    void map_elem(S& s, T& e);    // map 元素自动分发
}
```

---

## 实现流程

### 编码流程

```
FLY_ENCODE(obj, out_str)
  → bitsery::serialize() → FlyOutputAdapter → bytes → CMString
```

### 解码流程

```
FLY_DECODE(str, Type, obj)
  → FlyInputAdapter(str) → bitsery::deserialize() → obj
```

### FLY_FIELD 展开流程

```
FLY_FIELD(name)
  → auto& fly_v_ = o.name;
  → using fly_T_ = decay_t<decltype(fly_v_)>;
  → if constexpr (is_map_v) → StdMap + map_elem
  → if constexpr (is_vector_v) → container
  → if constexpr (is_string_v) → text
  → if constexpr (is_fundamental) → value
  → else → object
```

---

## 设计决策

| 决策 | 原因 |
|------|------|
| bitsery 后端 | header-only、版本化支持、性能优良、gcc12 兼容 |
| 宏抽象层 | 业务代码不依赖 bitsery API，未来可替换为 cereal/protobuf |
| FLY_FIELD 统一宏 | 减少样板代码，从 5 行降至 1 行 |
| TrustedConfig | 内部数据可信，跳过大小验证提升性能 |
| Boost.PP 遍历 | 支持变参 FLY_SERIALIZE(a, b, c)，无需编号宏 |
