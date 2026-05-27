# Issue #002: throw → error code 重构评审及修复方案

**状态**: Open
**严重程度**: Critical — 存在 3 个潜在 crash 点
**影响模块**: `storage`（database, data_reader, data_writer, compressors）、`agent`（master_agent, worker_agent）、`network`、`serialization`、`core`
**创建日期**: 2026-05-27
**关联重构**: 当前进行中的 throw → error code 重构（39 文件变更）

---

## 1. 问题描述

当前重构将 `storage/` 和 `agent/` 层的所有 `throw std::runtime_error` 替换为 error code（`std::pair<bool, ReadResult>` / `std::pair<CMString, TaskErrorType>`），方向正确。但实现中存在 **3 个 crash 级 bug**、**多处错误传播丢失**，以及 **18 处 throw 未处理**。

---

## 2. 当前重构覆盖范围

| 模块 | 文件 | throw 处理状态 |
|------|------|---------------|
| `storage/` | data_service.cpp, database.cpp, data_reader.cpp, data_writer.cpp | ✅ 已替换 |
| `storage/` | lz4_compressor.cpp, zlib_compressor.cpp, zstd_compressor.cpp, compressor.cpp, compression_utils.cpp | ✅ 已替换 |
| `agent/` | master_agent.cpp, worker_agent.cpp | ✅ 已替换 |
| `common/` | worker_context.h | ✅ 已替换 |
| `network/` | tcp_transport.cpp | ❌ **9 处未处理** |
| `serialization/` | object_header.cpp | ❌ **4 处未处理** |
| `serialization/` | serialization_macros.h | ❌ **2 处未处理**（FLY_DECODE 宏） |
| `core/` | config.cpp | ❌ **3 处未处理** |
| **总计** | | **18 处 throw 残留** |

---

## 3. 严重问题（必须修复）

### 🔴 P0-1: `database.h` 模板 `read_object<T>` — 空 buffer 解码 crash

**文件**: `src/storage/cpp/database.h:79-86`

```cpp
template<typename T>
CMSharedPtr<T> read_object(const CMString& object_name) {
    CMString full = full_name(object_name);
    auto result = fly::DataService::instance().read_raw(full);
    auto obj = CMMakeShared<T>();
    FLY_DECODE_FROM_BYTES(result.data_buffer, T, *obj);  // ← 空 buffer 时 throw/crash
    return obj;
}
```

**根因**: `read_raw()` 现在失败返回空 `ReadResult{}`，但模板方法不检查就直接传给 `FLY_DECODE_FROM_BYTES`。

**修复**:
```cpp
template<typename T>
CMSharedPtr<T> read_object(const CMString& object_name) {
    CMString full = full_name(object_name);
    auto result = fly::DataService::instance().read_raw(full);
    if (result.data_buffer.empty()) {
        ERR("read_object<T>: empty data for '{}'", full);
        return nullptr;
    }
    auto obj = CMMakeShared<T>();
    FLY_DECODE_FROM_BYTES(result.data_buffer, T, *obj);
    return obj;
}
```

---

### 🔴 P0-2: `decompress_data` — `nullptr` 解引用

**文件**: `src/storage/cpp/data_reader.cpp:153-157`

```cpp
auto compressor = CompressorFactory::create(static_cast<CompressionType>(compression_type));
return compressor->decompress(chunk.uncompressed_size, chunk.data);  // ← nullptr deref!
```

**根因**: `CompressorFactory::create` 对未知类型返回 `nullptr`（原为 throw），但调用方不做 null check。

**修复**:
```cpp
auto compressor = CompressorFactory::create(static_cast<CompressionType>(compression_type));
if (!compressor) {
    ERR("decompress_data: unknown compression type {}", compression_type);
    return {};
}
return compressor->decompress(chunk.uncompressed_size, chunk.data);
```

**同样问题出现在**: `data_reader.cpp:67-68`（`read_object_data` 中）

---

### 🔴 P0-3: `create_new_file` — 文件打开失败后状态不一致

**文件**: `src/storage/cpp/data_writer.cpp:250-263`

```cpp
void DataWriter::create_new_file() {
    if (file_stream_.is_open()) {
        file_stream_.close();  // ← 先关闭旧文件
    }
    // ...
    file_stream_.open(file_path, std::ios::binary);
    if (!file_stream_.is_open()) {
        ERR("Failed to create data file: {}", file_path); return;  // ← closed_ 未标记
    }
}
```

**根因**: 旧文件已关闭，新文件打开失败，但 `closed_` 仍为 `false`。后续 `write_object` 等检查 `closed_` 通过，但 `file_stream_` 无效，导致静默数据丢失。

**修复**:
```cpp
if (!file_stream_.is_open()) {
    ERR("Failed to create data file: {}", file_path);
    closed_ = true;
    return;
}
```

---

## 4. 中等问题

### 🟡 P1-1: `request_object_remove` 错误不传播

**文件**: `src/storage/cpp/database.h:108` → `src/agent/cpp/worker_agent.cpp:714-745`

```cpp
// database.h
void remove_object(const CMString& object_name);  // ← void，无法感知失败

// worker_agent.cpp:730-744
if (!pending->cv.wait_for(...)) {
    ERR("Remove request timed out: {}", full);
    return;  // ← 调用方不知道超时
}
if (!pending->success) {
    ERR("Remove request failed: {}", full);  // ← 调用方不知道失败
}
```

**建议**: `remove_object` 返回 `bool`，`request_object_remove` 返回 `bool`。

---

### 🟡 P1-2: `register_database` 重复注册静默失败

**文件**: `src/storage/cpp/data_service.cpp:44-48`

```cpp
for (const auto& [existing_id, paths] : db_paths_) {
    if (paths.base_path == base_path) {
        ERR("base_path '{}' already registered by database '{}'", base_path, existing_id);
        return;  // ← 调用方不知道失败
    }
}
```

**建议**: 返回 `bool` 表示注册是否成功。

---

### 🟡 P1-3: `load_meta()` 代码格式

**文件**: `src/storage/cpp/database.cpp:300-313`

```cpp
ERR("Cannot open meta file: {}", meta_path); return {};
```

`ERR` 和 `return` 写在同一行，违反项目代码风格。3 处都是这样。

**建议**: 拆分为两行。

---

### 🟡 P1-4: `WriteRegistrationError` 类已废弃但未删除

**文件**: `src/common/cpp/worker_context.h:16-25`

该类定义存在但所有 throw 点已替换为 return，不再使用。

**建议**: 删除或标记 `[[deprecated]]`。

---

### 🟡 P1-5: `Database` 析构函数空 catch

**文件**: `src/storage/cpp/database.cpp:61-65`

```cpp
Database::~Database() {
    try {
        fly::DataService::instance().drain_write_back();
        fly::DataService::instance().unregister_database(db_id_);
    } catch (...) {
    }  // ← 空 catch
}
```

**建议**: 既然已消除 throw，移除 try-catch。如保留，至少加 ERR log。

---

### 🟡 P1-6: `on_idx_load_command` 仍 catch `std::exception`

**文件**: `src/agent/cpp/worker_agent.cpp:662-690`

```cpp
try {
    // ... idx loading logic
} catch (const std::exception& e) {
    ack.success = false;
    ack.error_message = e.what();
}
```

与整体消除 throw 方向不一致。

**建议**: 确认内部是否仍有 throw（`LocalIndex::load`、`DataService::restore_entries` 等），如有则保留但加注释；如无则移除。

---

## 5. 未处理的 18 处 throw

### 5.1 `network/tcp_transport.cpp` — 9 处

| 行号 | 上下文 | 建议 |
|------|--------|------|
| 18 | `epoll_create` 失败 | 返回 `std::expected<void, ErrorCode>` |
| 34 | `socket()` 失败 | 同上 |
| 49 | `bind()` 失败 | 同上 |
| 56 | `listen()` 失败 | 同上 |
| 66 | `epoll_ctl` 失败 | 同上 |
| 81 | 客户端 `socket()` 失败 | 同上 |
| 93 | `connect()` 失败 | 同上 |
| 102 | 客户端 `epoll_ctl` 失败 | 同上 |
| 356 | `create_transport` 未知类型 | 返回 `nullptr` + ERR |

**注意**: `TCPTransport` 的构造函数 throw 影响最大，因为调用方（`main.cpp`）没有 catch，直接触发 `terminate_handler` → `_exit(77)`。**这些属于不可恢复的启动错误，保留 throw 也是合理的。** 建议至少将 `create_transport` factory 改为返回 `nullptr`。

### 5.2 `serialization/object_header.cpp` — 4 处

| 行号 | 上下文 | 建议 |
|------|--------|------|
| 41 | 数据不足解析固定头 | 返回 `bool`，输出参数填充 header |
| 48 | magic number 不匹配 | 同上 |
| 55 | 不支持的版本 | 同上 |
| 72 | 数据不足解析 py_name | 同上 |

### 5.3 `serialization_macros.h` — 2 处

| 行号 | 上下文 | 建议 |
|------|--------|------|
| 269 | `FLY_DECODE` 反序列化失败 | 改为返回 `bool` |
| 288 | `FLY_DECODE_FROM_BYTES` 反序列化失败 | 改为返回 `bool` |

**注意**: 这是宏，改动影响全局。当前有 7 个 `catch(...)` 块依赖这些 throw。建议分阶段处理：先保持宏不变，后续单独重构。

### 5.4 `core/config.cpp` — 3 处

| 行号 | 上下文 | 建议 |
|------|--------|------|
| 15 | `set_int` 在 workers 启动后调用 | 返回 `bool` |
| 22 | `set_str` 在 workers 启动后调用 | 返回 `bool` |
| 32 | 未知 config key | 返回 `std::optional<value>` |

---

## 6. 做得好的地方

1. **Callback 签名统一改为 `std::pair<bool, ReadResult>`** — 清晰区分成功/失败
2. **`register_write` 返回 `std::pair<CMString, TaskErrorType>`** — 保留了错误类型信息
3. **`check_frozen()` 改为 `bool`** — 调用方可以用 `if` 判断
4. **测试同步更新** — `EXPECT_THROW` → 检查空返回值
5. **`MasterAgent::on_task_failed` 识别 fatal error 并触发 graceful shutdown** — 正确的不可恢复错误处理
6. **QA 测试改为 subprocess-per-phase 模式** — 避免了 C++ singleton 状态污染
7. **`WorkerAgentContext::register_func` 从 C 函数指针改为 `std::function`** — 支持 lambda 捕获，消除了 trampoline 函数

---

## 7. 修复方案

### 阶段 1：紧急修复（P0）

修复 3 个 crash 点，确保不引入新 bug：

1. `database.h` 模板 `read_object<T>` — 空 buffer 检查
2. `data_reader.cpp` `decompress_data` + `read_object_data` — null check
3. `data_writer.cpp` `create_new_file` — 设置 `closed_` 标记

**预计改动**: 3 文件，约 15 行

### 阶段 2：错误传播修复（P1）

1. `remove_object` → 返回 `bool`
2. `register_database` → 返回 `bool`
3. `load_meta()` 格式修复
4. 删除 `WriteRegistrationError` 或标记 deprecated
5. 移除 `Database` 析构函数空 catch

**预计改动**: 5 文件，约 30 行

### 阶段 3：剩余 throw 处理（分批）

**批次 A**（高优先级）: `create_transport` factory → 返回 `nullptr`

**批次 B**（中优先级）: `config.cpp` → 返回 `bool` / `std::optional`

**批次 C**（低优先级）: `object_header.cpp` → 返回 `bool` + 输出参数

**批次 D**（需全局协调）: `FLY_DECODE` 宏 → 改为返回 `bool`（需同步更新 7 个 catch 块）

---

## 8. 优雅退出机制

### 8.1 现有退出路径评估

当前重构在不可恢复错误下的退出路径：

```
Worker 写入冻结 DB
  → register_write 返回 TaskErrorType::WRITE_TO_FROZEN_DB
  → WorkerAgent::poll_task 设置 last_error_type_
  → TaskFailedMessage 发送到 Master
  → MasterAgent::on_task_failed 检测到 fatal error
  → fatal_error_ = true
  → check_shutdown_request 触发 drain
  → persist_pending_tasks → do_drain_and_stop
  → 优雅退出
```

**评估**: ✅ 路径完整，符合设计要求。

### 8.2 新增 `graceful_exit()` 全局函数

**问题**: 没有可以从任意位置（storage、network、Python 层）调用的全局优雅退出入口。
- `terminate_handler()` / `sig_handler()` — crash 时使用，直接 `_exit(77/78)`，不优雅
- `MasterAgent::stop()` / `WorkerAgent::stop()` — 需要实例引用

**实现**: 新增 `fly::graceful_exit()` 函数（`src/core/cpp/graceful_exit.h/cpp`）

```cpp
// 从任意位置调用：
fly::graceful_exit("disk full", TaskErrorType::EXECUTION_ERROR, 1);

// Python 层调用：
from _fly_core import graceful_exit
graceful_exit("unrecoverable error", 1, 1)  # reason, error_type, exit_code
```

**Shutdown 序列**:
1. `fprintf(stderr, ...)` — 输出退出原因到 stderr
2. 调用注册的 shutdown callback（agent drain + persist + Logger::shutdown）
3. `_exit(exit_code)` — 跳过 C++ 析构，避免双重清理

**线程安全**: `std::atomic<bool> exit_initiated` 保证幂等，多次调用只执行一次。

**Agent 注册**（在 `start()` 中）:
```cpp
// MasterAgent::start()
register_shutdown_callback([this]() {
    this->stop();          // drain + persist + cleanup
    fly::Logger::shutdown();
});

// WorkerAgent::start()
register_shutdown_callback([this]() {
    this->stop();          // initiate_shutdown + do_cleanup
    fly::Logger::shutdown();
});
```

**Python 导出**: 通过 `_fly_core.graceful_exit(reason, error_type, exit_code)` 暴露。

**新增文件**:
| 文件 | 说明 |
|------|------|
| `src/core/cpp/graceful_exit.h` | 函数声明 |
| `src/core/cpp/graceful_exit.cpp` | 实现（atomic guard + callback + _exit） |
| `src/core/export/core_export.cpp` | nanobind 导出 |

**修改文件**:
| 文件 | 修改 |
|------|------|
| `src/core/cpp/BUILD` | 添加 graceful_exit.cpp |
| `src/agent/cpp/master_agent.cpp` | start() 中注册 shutdown callback |
| `src/agent/cpp/worker_agent.cpp` | start() 中注册 shutdown callback |

---

## 9. 关联文件

| 文件 | 问题 |
|------|------|
| `src/storage/cpp/database.h:79-86` | P0-1: 空 buffer 解码 |
| `src/storage/cpp/data_reader.cpp:67-68, 153-157` | P0-2: nullptr 解引用 |
| `src/storage/cpp/data_writer.cpp:250-263` | P0-3: 状态不一致 |
| `src/storage/cpp/database.cpp:300-313` | P1-3: 格式问题 |
| `src/storage/cpp/database.cpp:61-65` | P1-5: 空 catch |
| `src/agent/cpp/worker_agent.cpp:714-745` | P1-1: 错误不传播 |
| `src/agent/cpp/worker_agent.cpp:662-690` | P1-6: 不一致 catch |
| `src/storage/cpp/data_service.cpp:44-48` | P1-2: 静默失败 |
| `src/common/cpp/worker_context.h:16-25` | P1-4: 废弃类 |
| `src/network/cpp/tcp_transport.cpp` | 9 处 throw 未处理 |
| `src/serialization/cpp/object_header.cpp` | 4 处 throw 未处理 |
| `src/serialization/cpp/serialization_macros.h` | 2 处 throw 未处理 |
| `src/core/cpp/config.cpp` | 3 处 throw 未处理 |
