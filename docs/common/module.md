# Common 模块族 — 公共基础层

## 模块概述

**位置**: `src/common/`（模块族，2026-09-06 重组：原平铺 `common/cpp/` 按类型组织为子模块，顶层 serialization 模块并入为 `serialization` 子模块）

全项目公共基础层，按类型组织为七个子模块。依赖方向：子模块间自底向上（types 最底层），common 不依赖任何上层模块。

## 子模块结构

| 子模块 | 内容 | 依赖 |
|--------|------|------|
| `types/` | 基础别名：智能指针族（pointer_aliases.h） | 无 |
| `buffer/` | FlyBuffer 字节缓冲（fly_buffer.h）+ data_checksum 校验稳定包装层（独立 `.so` 单一定义点，libisal） | types, container(别名) |
| `concurrent/` | ConcurrentMap/ConcurrentUnorderedMap/ConcurrentUnorderedSet（concurrent_map.h）、ConcurrentQueue（concurrent_queue.h）、WriterPrefRwLock（writer_pref_rwlock.h） | container(别名) |
| `io/` | FdHandle 文件描述符所有权原语（fd_handle.h，issue 011 M1）、ChunkSource 拉取式流式输入源（chunk_source.h） | container(别名) |
| `runtime/` | WriterID（writer_id.h）、WorkerAgentContext（worker_context.h）、write_context_hash、WriteErrorType（error_types.h） | buffer, container(别名) |
| `testing/` | 跨模块测试设施（test_helpers.h：wait_for 等） | container(别名) |
| `serialization/` | 序列化宏 serialization_macros.h、对象头 object_header.h（FLY_OBJECT_MAGIC）、bitsery 版本化扩展（bitsery_ext/version.h） | buffer, types, container(别名), bitsery, boost_pp |

## 容器类型别名（已迁至 container 模块）

`CMString`/`CMVector`/`CMMap` 等 **CM\* 容器别名** 现位于 **`src/container/` 模块**的
`container/cpp/container_aliases.h`（可替换别名层：当前绑定 std 实现，随时可整体
替换为自定义容器实现）。该头聚合了 `common/types` 的指针别名——include 一个头
即可获得全部 `CM*` 符号：

```cpp
#include <container/cpp/container_aliases.h>   // 容器别名 + 智能指针别名（聚合）
```

各别名与 std 的绑定关系表（CMString/CMVector/CMMap/CMUnorderedMap/CMSet/
CMUnorderedSet/CMList/CMDeque/CMQueue/CMStack/CMMapKV）见
[container/module.md](../container/module.md)。

## 智能指针别名（types 子模块）

| 别名 | 原始类型 | 用途 |
|------|----------|------|
| `CMSharedPtr<T>` | `std::shared_ptr<T>` | 共享所有权指针 |
| `CMUniquePtr<T>` | `std::unique_ptr<T>` | 独占所有权指针 |
| `CMWeakPtr<T>` | `std::weak_ptr<T>` | 弱引用指针 |

工厂与转换工具：`CMMakeShared`/`CMMakeUnique`/`CMStaticPointerCast`/
`CMDynamicPointerCast`/`CMConstPointerCast`/`CMReinterpretPointerCast`。

## serialization 子模块

序列化机制（原顶层 serialization 模块，2026-09-06 并入）：

- **`serialization_macros.h`**——两层宏：
  - 类型能力声明（写在类定义体内）：`FLY_SERIALIZE(字段...)` 一段式；
    `FLY_SERIALIZE_BEGIN(N)` + `FLY_FIELD` + `FLY_SERIALIZE_END` 版本化形式；
  - 使用点操作：`FLY_ENCODE`/`FLY_DECODE`（CMString 载荷）、
    `FLY_ENCODE_TO_BUFFER`/`FLY_DECODE_FROM_STREAM` 等。
  - 字段类型自动分派（`fly_ser::`）：map（StdMap ext）、vector（含嵌套递归）、
    string、fundamental、其他走 `s.object`（要求元素有 serialize 成员）。
- **`object_header.h/.cpp`**——`FLY_OBJECT_MAGIC` 对象头：db 对象持久化格式
  与 Python pickle 入口的魔数识别（含 DecompressingStreamBuf 解压分流）。
- 后端选择：`FLY_SERIALIZATION_BACKEND`（默认 bitsery）；`FLY_MAX_SIZE` +
  `FlyTrustedConfig`（可信数据跳过校验）。

宏使用规范见 [DEVELOPMENT_GUIDELINES.md](../DEVELOPMENT_GUIDELINES.md)（FLY_SERIALIZE_*
与 FLY_EXPORT_* 章节）；对象头校验算法契约见 data_checksum.h 头注释。

## BUILD 目标

| target | 位置 | 说明 |
|--------|------|------|
| `fly_common_types` | common/types | 指针别名（纯头） |
| `fly_common_buffer` / `data_checksum` / `data_checksum_so` | common/buffer | 缓冲（纯头）+ 校验实现（唯一 cc 实现，`.so` 单一定义点） |
| `fly_common_concurrent` / `fly_common_io` / `fly_common_runtime` / `fly_common_testing` | 各子模块 | 纯头 |
| `fly_serialization` / `fly_serialization_so` | common/serialization | 序列化机制（`_so` 被 main dynamic_deps 引用） |
