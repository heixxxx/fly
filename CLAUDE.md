# Fly 分布式任务框架 — Agent 工作指南

> 本文档为 OpenCode 等 Agent 提供项目概览、工作规范和关键参考。

---

## 1. 项目概述

**Fly** 是一个分布式任务执行框架，采用 C++ 核心 + Python 流程控制 + nanobind 桥接的架构。

### 技术栈

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

### 架构分层

```
┌─────────────────────────────────────────┐
│  Python 流程控制 (py/)                   │
├─────────────────────────────────────────┤
│  nanobind 导出层 (export/)               │
├─────────────────────────────────────────┤
│  C++ 核心模块 (cpp/)                     │
│  - Agent (Master/Worker)                 │
│  - Task (调度/依赖图)                    │
│  - Network (Reactor/TCP)                 │
│  - Storage (Database)                   │
│  - Serialization (bitsery)              │
└─────────────────────────────────────────┘
```

---

## 2. 目录结构

```
fly/
├── fly.sh                    # 构建脚本 (必须使用!)
├── BUILD                     # 顶层 BUILD (自动生成)
├── .bazelrc                  # Bazel 配置
├── WORKSPACE                 # Bazel 工作区
├── MODULE.bazel              # Bazel 模块定义
│
├── src/                      # 源代码
│   ├── common/               # 公共类型定义
│   │   └── cpp/common_types.h  # CMString, CMVector, CMMap 等
│   │
│   ├── core/                 # 核心基础模块
│   │   └── cpp/config.h/cpp  # 配置管理
│   │
│   ├── serialization/        # 序列化模块
│   │   └── cpp/serialization_macros.h  # FLY_SERIALIZE, FLY_ENCODE (bitsery 后端)
│   │
│   ├── export/               # 导出宏定义
│   │   └── cpp/export_macros.h  # FLY_EXPORT_* 宏
│   │
│   ├── storage/              # 存储层 (Layer 1)
│   │   ├── cpp/database.h/cpp
│   │   ├── cpp/data_writer.h/cpp
│   │   ├── cpp/data_reader.h/cpp
│   │   ├── cpp/data_service.h/cpp  # 统一内存索引 (local/remote idx)
│   │   ├── cpp/storage_manager.h/cpp
│   │   └── export/storage_export.cpp
│   │
│   ├── network/             # 网络层 (Layer 2)
│   │   ├── cpp/reactor.h/cpp
│   │   ├── cpp/transport.h/cpp
│   │   ├── cpp/tcp_transport.cpp
│   │   ├── cpp/message_protocol.h/cpp
│   │   └── cpp/message_types.h
│   │
│   ├── task/                # 任务系统层 (Layer 3)
│   │   ├── cpp/dependency_graph.h/cpp
│   │   ├── cpp/worker_manager.h/cpp
│   │   ├── cpp/task_scheduler.h/cpp
│   │   └── cpp/metadata_manager.h/cpp
│   │
│   ├── agent/               # Agent 层 (Layer 4)
│   │   ├── cpp/master_agent.h/cpp
│   │   ├── cpp/worker_agent.h/cpp
│   │   ├── cpp/task_executor.h/cpp
│   │   └── export/agent_export.cpp
│   │
│   └── log/                 # 日志模块 (fmt 格式化 + 自定义类型宏)
│       └── cpp/logger.h/cpp  # vlog(), DBG/INFO/WARN/ERR, CM_FORMAT_*
│
├── qa/                      # 项目级集成测试
├── docs/                    # 设计文档
│   └── superpowers/plans/   # Layer 状态文档
└── scripts/                 # 辅助脚本
```

---

## 3. 构建系统

### 关键约束

**必须使用 `./fly.sh` 而非裸 `bazel` 命令！**

直接使用 `bazel build` 不会刷新 `compile_commands.json`，导致 clangd 无法工作。

### 常用命令

```bash
# 构建 + 刷新 clangd
./fly.sh build [target...]

# 测试 + 刷新 clangd
./fly.sh test [target...]

# 仅构建，不刷新 clangd
./fly.sh buildonly [target...]

# 仅刷新 clangd
./fly.sh refresh

# 构建 + 测试 + 刷新
./fly.sh check
```

### Bazel 配置 (.bazelrc)

```
build --cxxopt=-std=c++20
build --host_cxxopt=-std=c++20
build --enable_bzlmod=false
test --test_output=errors
```

---

## 4. 代码规范

### C++ 类型别名

所有代码使用标准库容器时，使用 `CM*` 前缀的类型别名（定义于 `common_types.h`）：

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
// 正确
#include <core/cpp/config.h>
#include <storage/cpp/database.h>

// 错误
#include "../cpp/config.h"
```

### 命名规范

| 类型 | 命名示例 |
|------|----------|
| Bazel target | `fly_storage_cpp` |
| Python so | `_fly_storage.so` |
| 导出类型 | EX+模块缩写+类型名，例:`EXStgDatabase` (storage → Stg) |
| 导出函数 | ex_模块缩写_函数名, 例:`ex_stg_create_database` |

---

## 5. 序列化宏

### FLY_SERIALIZE 声明

```cpp
// 简洁形式 (所有字段存在于版本 1)
struct Simple {
    int32_t id;
    CMString name;
    FLY_SERIALIZE(id, name);
};

// 完整形式 (需要版本判断)
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

### FLY_ENCODE / FLY_DECODE

```cpp
// 编码到 CMString
CMString bytes;
FLY_ENCODE(myStruct, bytes);

// 解码
MyStruct decoded;
FLY_DECODE(bytes, MyType, decoded);

// 编码到 FlyBuffer (uint8_t)
FlyBuffer buf;
FLY_ENCODE_TO_BYTES(obj, buf);

// 解码 from FlyBuffer
FLY_DECODE_FROM_BYTES(buf, MyType, decoded);
```

---

## 6. 日志与格式化宏

### 日志宏 (DBG/INFO/WARN/ERR)

基于 fmt 库的格式化日志，支持编译时格式检查：

```cpp
#include <log/cpp/logger.h>

DBG("connection established to {}:{}", host, port);
INFO("task {} completed in {}ms", task_id, elapsed);
WARN("retry attempt {}/{}", attempt, max_retries);
ERR("failed to read {}: {}", path, error_msg);
```

### 自定义类型格式化

#### CM_FORMAT_CLASS — 结构体格式化

必须在全局作用域使用（C++ 约束：`fmt::formatter` 特化必须在 `fmt` 命名空间外）：

```cpp
namespace fly {
struct Point { double x, y; };
}

CM_FORMAT_CLASS(fly::Point, "({}, {})", v.x, v.y);
```

#### CM_FORMAT_ENUM — 枚举自动 stringify

```cpp
CM_FORMAT_ENUM(fly::Color, RED, GREEN, BLUE);
// 输出: "RED", "GREEN", "BLUE"
```

#### CM_FORMAT_ENUM_EX — 枚举自定义字符串

使用 Boost PP 括号平衡机制，`(VALUE, "str")` 为一个元组：

```cpp
CM_FORMAT_ENUM_EX(fly::Status, (PENDING, "P"), (RUNNING, "R"), (DONE, "D"));
// 输出: "P", "R", "D"
```

---

## 7. Python 导出宏

### 模块定义

```cpp
FLY_EXPORT_MODULE(_fly_module) {
    // 导出代码
}
```

### 类导出

```cpp
FLY_EXPORT_CLASS(Database, "EXStgDatabase")
    FLY_EXPORT_INIT()
    FLY_EXPORT_READONLY_ATTR("db_id", &Database::get_db_id)
    FLY_EXPORT_METHOD("freeze", &Database::freeze)
    FLY_EXPORT_SERIALIZE(Database);
```

### 函数导出

```cpp
FLY_EXPORT_FUNCTION("ex_stg_create_database", [](const CMString& path) {
    return std::make_shared<Database>(path);
});

FLY_EXPORT_FUNCTION_REF("ex_stg_get_storage_manager", []() -> StorageManager& {
    return StorageManager::instance();
});
```

### 枚举导出

```cpp
FLY_EXPORT_ENUM(CompressionType, "EXStgCompressionType")
    FLY_EXPORT_ENUM_VALUE("NONE", CompressionType::NONE)
    FLY_EXPORT_ENUM_VALUE("LZ4", CompressionType::LZ4)
    FLY_EXPORT_ENUM_VALUE("ZSTD", CompressionType::ZSTD);
```

---

## 8. 关键模块

### 存储层 (src/storage/)

| 文件 | 职责 |
|------|------|
| `database.h/cpp` | 统一存储接口，写时通知 DataService，读时走 DataService 内存索引，`remove_object()` 删除对象索引 |
| `data_writer.h/cpp` | 单线程写入聚合器，小文件聚合 + 大文件分块 |
| `data_reader.h/cpp` | 数据读取，支持 `read_from_entries()` 直接按索引读取 |
| `data_service.h/cpp` | **统一内存索引：local_idx + remote_idx + worker_registry**，支持 `remove_local_index()` / `remove_remote_index()` |
| `local_index.h/cpp` | **增量持久化索引**：`IdxOpType(ADD/REMOVE)` + body 格式追加写入，`load()` 自动迁移旧格式，`compact()` 全量压缩 |
| `storage_manager.h/cpp` | Database 生命周期管理，单例 |

### 网络层 (src/network/)

| 文件 | 职责 |
|------|------|
| `reactor.h/cpp` | 单线程事件循环 |
| `transport.h/cpp` | TransportLayer 抽象 |
| `tcp_transport.cpp` | POSIX TCP 实现 |
| `message_protocol.h/cpp` | 二进制帧协议 |
| `message_types.h` | 消息结构定义 (24 种消息类型，含 WorkerPropertyUpdate type=23, ObjectRemoved type=24) |

### 任务系统层 (src/task/)

| 文件 | 职责 |
|------|------|
| `dependency_graph.h/cpp` | 任务依赖管理，支持 `is_data_ready()` / `get_task_dependencies()` 查询 |
| `worker_manager.h/cpp` | Worker 状态管理，动态属性 `update_capabilities()` / `has_worker_with_all_capabilities()` |
| `task_scheduler.h/cpp` | 任务调度器，基于 Worker capabilities 匹配 |
| `metadata_manager.h/cpp` | 任务元数据 (仅 task lifecycle，数据位置已迁移至 DataService) |
| `heartbeat_monitor.h/cpp` | 心跳监控 |

### Agent 层 (src/agent/)

| 文件 | 职责 |
|------|------|
| `master_agent.h/cpp` | Master 节点管理，失败任务持久化 + `restart_failed_tasks()` |
| `worker_agent.h/cpp` | Worker 节点执行，动态属性 `set/remove/get_worker_property()` |
| `task_executor.h/cpp` | 任务执行器 |

### 日志模块 (src/log/)

| 文件 | 职责 |
|------|------|
| `logger.h/cpp` | fmt 格式化后端 `vlog()`，前端模板 `log_write()`，日志宏 `DBG/INFO/WARN/ERR`，自定义类型格式化宏 `CM_FORMAT_CLASS` / `CM_FORMAT_ENUM` / `CM_FORMAT_ENUM_EX` |

---

## 9. 测试规范

### C++ 测试 (gtest)

```cpp
#include <gtest/gtest.h>
#include <storage/cpp/database.h>

TEST(DatabaseTest, WriteRead) {
    Database db("/tmp/test");
    db.write_object("key", "value");
    auto result = db.read_object<CMString>("key");
    EXPECT_EQ(result, "value");
}
```

BUILD 配置：
```python
cc_test(
    name = "database_test",
    srcs = ["database_test.cpp"],
    deps = [
        "@com_google_googletest//:gtest_main",
        "//src/storage/cpp:fly_storage_database",
    ],
)
```

### Python 测试 (pytest)

```python
import pytest
from _fly_storage import Database

def test_database():
    db = Database("/tmp/test")
    db.write_object("key", "value")
    assert db.read_object("key") == "value"
```

### 运行测试

```bash
# 运行所有测试
./fly.sh test //src/...

# 运行特定模块测试
./fly.sh test //src/storage/tests:database_test
```

---

## 10. 动态 Worker 属性与任务调度

### 属性管理 API

Worker 可在 Task 执行过程中动态增/删/查属性，实时同步至 Master：

```python
# Worker 端 (在 task 函数内)
from fly.runtime import get_agent
agent = get_agent()
agent.set_worker_property("gpu")          # 添加属性
agent.remove_worker_property("gpu")       # 移除属性
props = agent.get_worker_properties()     # 查询属性列表

# Master 端: 打印 WARN + no-op
```

### 消息流

1. Worker Task 调用 `set_worker_property("gpu")` → `WorkerAgent` 发送 `WorkerPropertyUpdateMessage`（type=23）
2. Master 收到 → `WorkerManager.update_capabilities()` 更新属性 → `schedule_tasks()` 重新调度

### Capability 匹配调度

Task 通过 `@as_task(requires=["gpu"])` 声明所需属性，调度器仅分配给拥有全部所需属性的 Worker。

### fail_unscheduleable_tasks 配置

| 值 | 行为 |
|----|------|
| `1`（默认）| 永远无法调度的 Task 立即标记 FAILED 并持久化（见 §11） |
| `0` | 永远无法调度的 Task 保持等待状态 |

---

## 11. 失败任务持久化与重启

### 持久化机制

Task 失败时（capability 不匹配或数据依赖不可解析），Master 自动将完整任务信息序列化到 `log_dir/failed_tasks.bin`：

- **FailedTaskRecord** 结构体（FLY_SERIALIZE）：task_id, name, module, args, inputs, outputs, required_capabilities, error_message
- 持久化格式：增量追加 `[int64_t body_size][bitsery-encoded FailedTaskRecord]`，每次失败追加一条记录
- 删除记录时（`remove_persisted_task`）：读取全部记录 → 过滤目标 → 重写文件
- 重启时（`restart_failed_tasks`）：按 `[body_size][body]` 逐条增量读取并反序列化
- 失败日志打印 bin 文件路径和 API 用法

### 不可解析依赖检测

当 `fail_unscheduleable_tasks=1` 时，`schedule_tasks()` 执行两项检查：
1. **Capability 检查**：ready_tasks 中无匹配 Worker 的 task → FAILED
2. **依赖检查**：仅 pending_tasks 残留（无 ready、无 running）→ 依赖永远无法满足 → FAILED

### restart_failed_tasks API

```python
# 用户修复问题后（写入缺失数据、启动新 Worker）
master.restart_failed_tasks("/path/to/failed_tasks.bin")
```

流程：
1. 按 `[body_size][body]` 逐条增量读取并反序列化 FailedTaskRecord 列表
2. 删除 bin 文件（避免新 fail record 被误删）
3. 对每个 record 的 inputs，通过 DataService 三阶段读取检查数据可用性 → `mark_data_ready`
4. 重新 `submit_task()` → `schedule_tasks()` 调度
5. 若 task 仍无法调度（如仍缺少 Worker capability）→ 重新 fail 并持久化到新 bin 文件

### 依赖命名规范

Task 的 inputs 必须使用 `db.get_obj_name()` 获取 full name（`db_id:object_name`），与 DataService / `mark_data_ready` 命名空间一致：

```python
# 正确
@as_task(inputs=lambda db, key: [db.get_obj_name("phantom")])
def my_task(db, key):
    ...

# 错误 — 短名无法匹配 DataService 索引
@as_task(inputs=lambda db, key: ["phantom"])
def my_task(db, key):
    ...
```

---

## 12. 对象删除 (remove_object)

### API

```python
# Worker 端（task 函数内，自动通知 Master）
db.remove_object("object_name")

# Master 端（需额外调用 broadcast 通知所有 Worker）
db.remove_object("object_name")
master._agent.broadcast_object_removed(db.get_db_id(), "object_name")
```

### 删除流程

1. `Database::remove_object()` 执行本地清理：
   - `check_frozen()` — 冻结后不允许删除
   - `removed_objects_` 集合记录待删除对象（freeze 时磁盘清理）
   - `DataWriter::remove_entry()` 从 LocalIndex 中删除索引条目
   - `DataService::remove_local_index()` 从内存索引 local_idx 中删除

2. Worker 端通过 `WorkerAgentContext` trampoline 自动通知：
   - `notify_removed_trampoline` → 发送 `ObjectRemovedMessage` (type=24) 给 Master
   - Master 收到后：`DataService::remove_remote_index()` → 广播给所有其他 Worker
   - 其他 Worker 收到后：清理 `local_idx_` + `remote_idx_`

3. `freeze()` 时磁盘清理（占位符，当前未实现）：
   - 聚合文件包含多个对象，删除单个需要重写整个文件
   - 完整实现需等待数据压缩 (compaction) 功能

### ObjectRemovedMessage (type=24)

```
Worker → Master: { object_name: "db_id:obj_name", db_id: "xxx" }
Master → Worker (broadcast): 同上，转发给所有其他 Worker
```

---

## 13. 项目状态

### Layer 实现进度

| Layer | 状态 | 测试数 | 核心产出 |
|-------|------|--------|----------|
| Layer 0 | ✅ 完成 | 5 | WORKSPACE, BUILD, 宏定义 |
| Layer 1 | ✅ 完成 | 45 | Database, DataService, StorageManager |
| Layer 2 | ✅ 完成 | 35 | Reactor, TCP, 消息协议 |
| Layer 3 | ✅ 完成 | 28 | DependencyGraph, 调度器 |
| Layer 4 | ✅ 完成 | 48 | MasterAgent, WorkerAgent |
| Layer 5 | ✅ 完成 | - | Python API, DataService, 三层读取流程 |

**总测试**: 32 Bazel targets pass (1 data_service_test + 31 unit) + QA + E2E

### DataService 架构 (Layer 1 核心)

DataService 是进程级单例，Master 和 Worker 通用：
- **local_idx**: 本地写入的对象索引 (write_object 时更新)
- **remote_idx**: 远程对象位置缓存 (Master 接收 DataReady/TaskComplete 时更新；Worker 远程读取成功后缓存)
- **worker_registry**: Worker 注册信息
- **transfer_server**: IOThreadPool 线程池，处理数据传输请求的文件 I/O (可配置线程数 `data_server_threads`，默认 1)

数据传输架构 (Worker B 响应数据请求):
- Reactor 收到 `DataRequestMessage` → `DataService.submit_transfer(conn_id, object_name)` (非阻塞入队)
- IOThreadPool 线程执行文件 I/O (`try_read_local`)
- 完成回调通过 `process_completions()` 在 Reactor 线程执行 → `reactor_->send(response)` (线程安全)

Worker A 读取 (DataClient 独立连接):
- `DataClient::request_data(host, port, object_name)` — 阻塞 TCP socket，独立于主 Reactor
- 每次请求创建独立连接，避免多线程读冲突

读取流程 (三层降级，所有路径统一经过 DataService):
1. `DataService.try_read_local()` → 内存索引 → DataReader.read_from_entries() (含 ObjectHeader py_name 提取)
2. `DataService.lookup_remote_idx()` → 有缓存 → `DataClient::request_data()` 直连目标 Worker
3. `request_remote_data()` → 查 Master (via Reactor) → `DataClient::request_data()` 直连目标 Worker (最多 3 次重试)

DB 路径查询: WorkerAgent.request_db_path(db_id) → 向 Master 查询 → 自动创建 Database 实例
消息类型: 24 种 (含 DB_PATH_REQUEST/DB_PATH_RESPONSE, WorkerPropertyUpdate type=23, ObjectRemoved type=24)

---

## 14. Agent 工作指南

### 必须遵循

1. **使用 `./fly.sh` 而非裸 bazel**
2. **TDD 流程**: 先写测试，再写实现，测试通过后提交
3. **C++20 标准**: 使用 `--std=c++20`
4. **gcc12 编译器**: 非 clang
5. **模块式 include 路径**: 使用 `<module/cpp/file.h>` 格式

### 禁止事项

1. 禁止直接使用 `bazel build` 或 `bazel test`
2. 禁止使用相对路径 include
3. 禁止直接调用 bitsery/nanobind 原始 API（必须通过宏）
4. 禁止跳过测试直接提交

### 新模块创建模板

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
│   └── BUILD             # py_library
└── tests/
    ├── new_module_test.cpp
    ├── new_module_test.py
    └── BUILD
```

---

## 15. 快速参考

### 常用命令

```bash
# 构建整个项目
./fly.sh build //src/...

# 运行测试
./fly.sh test //src/...

# 快速检查
./fly.sh check

# 刷新 clangd
./fly.sh refresh
```

### 关键文件

- `docs/DEVELOPMENT_GUIDELINES.md` - 开发规范（详细）
- `docs/superpowers/plans/2026-05-17-progress-and-roadmap.md` - 当前状态与路线图
- `docs/superpowers/plans/2026-05-17-network-and-message-flow.md` - 网络与消息流程
- `docs/superpowers/plans/2026-05-16-layer5-python-api-design.md` - Layer 5 设计

---

*文档更新日期: 2026-05-22*