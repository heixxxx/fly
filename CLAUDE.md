# Fly 分布式任务框架 — Agent 工作指南

> 本文档为 Agent 提供项目概览、工作规范和关键设计约束。
> 实现细节见 `docs/*/module.md`，设计历史见 `docs/superpowers/plans/`。

---

## 1. 项目概述

**Fly** 是一个分布式任务执行框架，采用 C++ 核心 + Python 流程控制 + nanobind 桥接的架构。

| 组件 | 技术选型 |
|------|----------|
| C++ 标准 | C++20 |
| 编译器 | gcc12 |
| Python 绑定 | nanobind |
| 序列化 | bitsery (header-only, 版本化支持) |
| 构建系统 | Bazel + fly.sh |
| 测试框架 | gtest + pytest |
| 压缩库 | LZ4 / ZLIB / ZSTD |
| 格式化库 | fmt (header-only) |

架构分层：

```
┌─────────────────────────────────────────┐
│  Python 流程控制 (src/fly/)              │
├─────────────────────────────────────────┤
│  nanobind 导出层 (src/*/export/)         │
├─────────────────────────────────────────┤
│  C++ 核心模块 (src/*/cpp/)               │
│  - Agent (Master/Worker)                 │
│  - Task (调度/依赖图)                    │
│  - Network (Reactor/TCP)                 │
│  - Storage (Database)                   │
│  - Serialization (bitsery)              │
└─────────────────────────────────────────┘
```

---

## 2. 构建与测试

**必须使用 `./fly.sh` 而非裸 `bazel` 命令！** 直接使用 `bazel build` 不会刷新 `compile_commands.json`，导致 clangd 无法工作。

```bash
./fly.sh build [target...]     # 构建 + 刷新 clangd
./fly.sh test [target...]      # 测试 + 刷新 clangd
./fly.sh buildonly [target...] # 仅构建，不刷新
./fly.sh refresh               # 仅刷新 clangd
./fly.sh check                 # 构建 + 测试 + 刷新
./fly.sh install               # 创建 build/ 目录，symlink 到 bazel-bin 产物

# 单元测试
./fly.sh test //src/...

# QA 测试（需先构建并安装）
./fly.sh build //src/main/cpp:fly
./fly.sh install
bash qa/run_qa_tests.sh
```

### 测试稳定性（零容忍）

**所有测试必须每次运行都通过。**

- **禁止 `sleep(Xms); assert(condition)` 模式** — 异步操作必须用轮询循环（30 次 × 50ms）
- **禁止删除失败测试**
- **禁止 `time.sleep()` 作为同步手段** — 使用 `wait_for_*` 方法
- **QA 测试同样适用** — `bash qa/run_qa_tests.sh` 必须 100% 稳定通过

---

## 3. 代码规范

### C++ 类型别名

所有代码使用 `CM*` 前缀的类型别名（定义于 `common/cpp/common_types.h`）：

```cpp
#include <common/cpp/common_types.h>
CMString name;           // std::string
CMVector<int> ids;       // std::vector<int>
CMMap<K, V> dict;        // std::map<K, V>
CMUnorderedMap<K, V> h; // std::unordered_map<K, V>
```

### Include 路径

使用模块式路径，不使用相对路径：

```cpp
#include <core/cpp/config.h>       // 正确
#include "../cpp/config.h"         // 错误
```

### 命名规范

| 类型 | 命名示例 |
|------|----------|
| Bazel target | `fly_storage_cpp` |
| Python so | `_fly_storage.so` |
| 导出类型 | `EXStgDatabase` (EX+模块缩写+类型名) |
| 导出函数 | `ex_stg_create_database` (ex_模块缩写_函数名) |

---

## 4. 关键模块

### 存储层 (src/storage/)

| 文件 | 职责 |
|------|------|
| `database.h/cpp` | 统一存储接口，写时通知 DataService，读时走 DataService 内存索引 |
| `data_writer.h/cpp` | 单线程写入聚合器，writer_id 命名 idx/data 文件 |
| `data_reader.h/cpp` | 数据读取，按 writer_id 索引 |
| `data_service.h/cpp` | 统一内存索引：local_idx + remote_idx + db_paths_ + worker_registry |
| `local_index.h/cpp` | 增量持久化索引，IdxOpType(ADD/REMOVE) 追加写入 |
| `storage_manager.h/cpp` | Database 生命周期管理，单例 |
| `py/database.py` | Python Database 类（write_object/read_object） |
| `py/__init__.py` | 导出 C++ 存储类型 |

### 网络层 (src/network/)

| 文件 | 职责 |
|------|------|
| `reactor.h/cpp` | 单线程事件循环 |
| `transport.h/cpp` + `tcp_transport.cpp` | TransportLayer 抽象 + POSIX TCP 实现 |
| `message_protocol.h/cpp` | 二进制帧协议 |
| `message_types.h` | 26 种消息结构定义 |

### 任务系统层 (src/task/)

| 文件 | 职责 |
|------|------|
| `dependency_graph.h/cpp` | 任务依赖管理，is_data_ready() / get_task_dependencies() |
| `worker_manager.h/cpp` | Worker 状态管理，动态属性 update_capabilities() |
| `task_scheduler.h/cpp` | 基于 Worker capabilities 匹配的调度器 |
| `metadata_manager.h/cpp` | 任务元数据（仅 lifecycle） |
| `heartbeat_monitor.h/cpp` | 心跳监控 |
| `py/task.py` | @as_task() 装饰器、task_name()、任务注册 |
| `py/__init__.py` | 导出 as_task, task_name |

### Agent 层 (src/agent/)

| 文件 | 职责 |
|------|------|
| `master_agent.h/cpp` | Master 节点：失败任务持久化、写入注册依赖满足、load_db 恢复、register_worker(0) 自注册 |
| `worker_agent.h/cpp` | Worker 节点：任务执行、动态属性、on_idx_load_command() 按 writer_ids 加载 |
| `task_executor.h/cpp` | 任务执行器 |
| `py/agent.py` | Master/Worker/FlyAgent Python 类 |
| `py/executor.py` | Python 侧任务执行器 |
| `py/__init__.py` | 导出 Master, Worker, FlyAgent |

### 其他模块

| 路径 | 职责 |
|------|------|
| `src/common/cpp/common_types.h` | CM* 类型别名 |
| `src/common/cpp/writer_context.h` | WorkerAgentContext（回调模式） |
| `src/common/cpp/writer_id.h` | generate_writer_id()（8-char hex UUID） |
| `src/core/cpp/config.h/cpp` | 配置管理 |
| `src/core/py/__init__.py` | get_config() + Config 导出（合并了原 config.py） |
| `src/serialization/cpp/serialization_macros.h` | FLY_SERIALIZE, FLY_ENCODE/DECODE |
| `src/export/cpp/export_macros.h` | FLY_EXPORT_* 宏 |
| `src/log/cpp/logger.h/cpp` | DBG/INFO/WARN/ERR 日志宏，CM_FORMAT_CLASS/ENUM |

---

## 5. 宏参考

Agent 禁止直接调用 bitsery/nanobind 原始 API，必须通过以下宏。

### 序列化

```cpp
// 声明（在 struct 内）
FLY_SERIALIZE(field1, field2);
FLY_SERIALIZE_BEGIN(version) FLY_FIELD(x); FLY_SERIALIZE_END

// 编码/解码
FLY_ENCODE(struct_obj, output_string);
FLY_DECODE(input_string, StructType, output_obj);
FLY_ENCODE_TO_BYTES(obj, output_buffer);    // → FlyBuffer
FLY_DECODE_FROM_BYTES(buffer, StructType, output_obj);
```

### 日志

```cpp
#include <log/cpp/logger.h>
DBG("msg {}", arg);   INFO("msg {}", arg);
WARN("msg {}", arg);  ERR("msg {}", arg);

CM_FORMAT_CLASS(ns::Type, "({}, {})", v.x, v.y);  // 全局作用域
CM_FORMAT_ENUM(ns::Enum, A, B, C);                 // 输出 "A", "B", "C"
```

### Python 导出

```cpp
FLY_EXPORT_MODULE(_fly_module) { /* 导出代码 */ }
FLY_EXPORT_CLASS(ClassName, "EXModClassName")
    FLY_EXPORT_INIT()
    FLY_EXPORT_READONLY_ATTR("attr", &Class::getter)
    FLY_EXPORT_METHOD("method", &Class::method)
    FLY_EXPORT_SERIALIZE(Class);
FLY_EXPORT_FUNCTION("ex_mod_func", [](args) { return result; });
FLY_EXPORT_ENUM(EnumType, "EXModEnumType")
    FLY_EXPORT_ENUM_VALUE("NAME", EnumType::NAME);
```

---

## 6. 设计约束

### Python 公共 API

用户通过 `from fly import ...` 使用函数级 API，无需了解 Master/Worker 内部实现：

```python
from fly import open_db, as_task, task_name, launch_workers, wait_tasks
from fly import get_agent  # 进阶：直接访问 Agent 单例
```

**导出列表**: `open_db`, `load_db`, `get_config`, `as_task`, `task_name`, `launch_workers`, `wait_tasks`, `restart_failed_tasks`, `get_task_error`, `completed_tasks`, `pending_tasks`, `running_tasks`, `failed_tasks`, `get_agent`

**不导出**: `Master`, `Worker`, `FlyAgent`（内部类，通过 `agent.agent` 模块可访问但不推荐用户使用）

### 数据命名与依赖

- Task inputs 必须使用 `db.get_obj_name("name")` 获取全名（`db_id:object_name`），短名无法匹配 DataService 索引
- `on_data_ready()` 是唯一数据就绪入口：更新 remote_idx + _DB_META + dependency graph + schedule_tasks()
- `write_object` 开始时即触发依赖满足（无需等异步落盘完成）

### writer_id 解耦

- writer_id = 8-char hex UUID，Database 构造时生成
- idx 文件：`{writer_id}.idx`，data 文件：`data_{writer_id}_{index:03}.dat`
- **load_db 时 Master 不加载任何 idx 到 local_idx**，所有旧数据通过 remote_idx 经 Worker 提供
- Worker 按 hostname 分配 idx 加载任务（含 Master 的 writer_id）
- Master 仍可写新数据，通过 `register_worker(0)` 供 Worker 读取

### 动态 Worker 属性

Worker 在 Task 执行中可动态增/删/查属性，Master 实时重调度。Task 通过 `@as_task(requires=["gpu"])` 声明需求。`fail_unscheduleable_tasks=1`（默认）时，永远无法调度的 Task 立即 FAILED 并持久化。

### 对象删除

Worker 端 `db.remove_object()` 自动通知 Master。Master 端需额外调用 `broadcast_object_removed()`。冻结后不允许删除。

### DataService 核心

进程级单例，Master/Worker 通用：local_idx（本地写入索引）、remote_idx（远程位置缓存）、db_paths_（db_id → 路径注册表）、worker_registry、transfer_server（IOThreadPool）。读取三层降级：try_read_local → lookup_remote_idx → request_remote_data。详见 `docs/storage/module.md`。

### 失败任务

Task 失败时自动序列化到 `log_dir/failed_tasks.bin`。`restart_failed_tasks(path)` 重新提交。详见 `docs/agent/module.md`。

---

## 7. Agent 工作指南

### 必须遵循

1. **使用 `./fly.sh`** 而非裸 bazel
2. **TDD 流程**：先写测试，再写实现，测试通过后提交
3. **C++20 / gcc12**
4. **模块式 include 路径**：`<module/cpp/file.h>` 格式
5. **调试必须加载 `systematic-debugging-analysis` skill**：先加日志运行观察，禁止仅靠静态分析猜测

### 禁止事项

1. 禁止直接使用 `bazel build` 或 `bazel test`
2. 禁止使用相对路径 include
3. 禁止直接调用 bitsery/nanobind 原始 API（必须通过宏）
4. 禁止跳过测试直接提交
5. 禁止无日志调试

### LSP 误报

以下 LSP 错误均为 **编译期虚拟路径误报**，`./fly.sh build` 可正常通过，忽略即可：
- `common/cpp/writer_id.h file not found` — virtual includes 路径
- `No template named 'remove_cvref_t' in namespace 'fmt'` — clangd 解析 bazel 虚拟头文件
- `Import "_fly_*" could not be resolved` — nanobind 动态生成的 .so 类型

### 新模块模板

```
src/new_module/
├── cpp/
│   ├── new_module.h      # #pragma once, 使用 CMString/CMMap
│   ├── new_module.cpp    # #include <module/cpp/new_module.h>
│   └── BUILD             # name="fly_new_module_cpp"
├── export/
│   ├── new_module_export.cpp  # FLY_EXPORT_MODULE(_fly_new_module)
│   └── BUILD             # cc_binary, name="_fly_new_module.so"
├── py/
│   ├── __init__.py
│   ├── new_module.py     # Python 侧封装（如有）
│   └── BUILD             # py_library
└── tests/
    ├── new_module_test.cpp
    ├── new_module_test.py
    └── BUILD
```

---

*文档更新日期: 2026-05-23*
