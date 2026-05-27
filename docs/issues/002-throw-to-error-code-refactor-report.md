# throw → error code 全面重构报告

**日期**: 2026-05-27
**范围**: storage, agent, common, serialization, core, network
**测试**: 41 unit + 36 QA，10 轮零 flaky

---

## 1. 改动总结

### 1.1 架构变更

| 变更 | 文件 | 说明 |
|------|------|------|
| `write_object*()` 返回空 CMString | database.cpp/h | 失败时 ERR log + return {}，不再 throw |
| `read_object*()` 返回空 ReadResult | database.cpp/h, data_reader.cpp | 同上 |
| DataService 回调返回 `pair<bool, ReadResult>` | data_service.h, worker_agent, master_agent | RemoteReadCallback / DirectReadCallback 签名变更 |
| `register_write` 返回 `pair<CMString, TaskErrorType>` | worker_context.h | 消除 WriteRegistrationError 异常类 |
| `check_frozen()` 返回 bool | database.cpp/h | 调用方 if 判断，不再 throw |
| Compressor 返回空 CMString / nullptr | lz4/zstd/zlib/compressor.cpp | 第三方库错误改为 ERR log + return {} |
| `WorkerAgentContext::register_func_` 改为 `std::function` | worker_context.h | 消除 C 函数指针 + trampoline |
| 25 个 try-catch 块移除 | data_service.cpp, database.cpp/h, 等 | DataReader/DataWriter 不再 throw |

### 1.2 新增功能

| 功能 | 文件 |
|------|------|
| `@wait_obj` 装饰器 | task.py |
| `DataService::try_read_remote` | data_service.cpp/h |
| Worker probe 机制（Tier 3） | data_service.cpp, worker_agent |
| `MasterAgent::on_task_failed` fatal 窄化 | master_agent.cpp |

### 1.3 构建变更

| 变更 | 文件 |
|------|------|
| 移除 hedron_compile_commands | MODULE.bazel, fly.sh |
| 本地 refresh_compile_commands 工具 | tools/refresh_compile_commands/ |

---

## 2. 优雅退出路径

```
Worker write 失败
  → register_write 返回 TaskErrorType::WRITE_REGISTRATION_TIMEOUT
  → WorkerAgentContext::set_last_error_type(...)
  → Database 返回空 CMString（ERR 日志已记录）
  → poll_task 检查 SUCCESS 分支中的 last_error_type_
  → 若非 UNKNOWN → TaskFailedMessage 发送到 Master
  → Master::on_task_failed 检查 error_type
  → WRITE_REGISTRATION_TIMEOUT / EXECUTION_ERROR → fatal_error_ = true
  → check_shutdown_request → drain → do_drain_and_stop → 优雅退出
```

**不触发优雅退出**：
- `WRITE_TO_FROZEN_DB` — 上游 freeze 广播，预期行为
- `WRITE_REGISTRATION_FAILED` — 重复写入已成功，无害
- Read 失败 — 任务级错误，任务系统负责传播

---

## 3. 遇到的错误与修复

### P0-1: `database.h` 模板 `read_object<T>` 空 buffer crash

**现象**: `read_raw()` 返回空 `ReadResult{}`，模板直接传给 `FLY_DECODE_FROM_BYTES` → 段错误。

**修复**: 在模板中增加 `result.data_buffer.empty()` 检查，返回 nullptr。

### P0-2: `CompressorFactory::create()` 返回 nullptr 解引用

**现象**: `create()` 对未知类型返回 `nullptr`（原为 throw），`data_reader.cpp` 两处直接用 `compressor->decompress()`。

**修复**: `read_object_data` 和 `decompress_data` 增加 null 检查 + ERR log。

### P0-3: `DataWriter::create_new_file` 文件打开失败状态不一致

**现象**: 旧文件已关闭，新文件打开失败，但 `closed_` 未标记。后续 `write_object` 检查通过但 `file_stream_` 无效。

**修复**: 文件打开失败时设置 `closed_ = true`。

### P1-1: QA 测试单进程反复启停 agent

**现象**: `test_remove_object.py` 多 phase 复用同一 Python Master 单例，Phase 1 的 dead Worker 污染 Phase 2 调度。

**修复**: 改为 subprocess-per-phase 模式（参照 `test_load_db.py`），每个 phase 独立 fly 二进制。

**根因修复**: `MasterAgent::start()` 每次重建 `graph_` / `worker_manager_`。

### P1-2: 写路径隐蔽 gap

**现象**: `register_write` 失败后 `last_error_type_` 已设置，但 executor 返回 SUCCESS（无 Python 异常），`poll_task` 未检查 SUCCESS 分支 → 错误类型丢失。

**修复**: 
- `begin_task` 清空 `last_error_type_`（防止跨 task 污染）
- `poll_task` SUCCESS 分支检查 `get_last_error_type()`，非 UNKNOWN 时覆盖为 TaskFailedMessage

### P1-3: `FLY_EXPORT_METHOD` 宏 + structured binding 逗号冲突

**现象**: `auto [success, result] = ...` 中逗号被宏解析为参数分隔符 → 编译错误。

**修复**: 改用 `.first` / `.second` 访问 `pair` 成员。

### P1-4: `ERR()` Python binding 不支持多参数

**现象**: `ERR("msg {}", arg1, arg2)` 报 `TypeError: incompatible function arguments`。`_fly_log.ERR()` 是 nanobind 导出的单字符串参数函数。

**修复**: 使用 f-string 预格式化。

---

## 4. 已废弃

| 符号 | 原位置 | 原因 |
|------|--------|------|
| `WriteRegistrationError` | worker_context.h | 所有 throw 点已替换为 return |
| `register_write_trampoline` (Worker) | worker_agent.h/cpp | 改为 lambda + std::function |
| `master_register_write_trampoline` (Master) | master_agent.h/cpp | 同上 |
| `hedron_compile_commands` | MODULE.bazel | Bazel 9 移除 native.py_binary |

---

## 5. 测试稳定性

```
10 轮 × (41 unit + 36 QA) = 770 次测试
Unit: 0 failures    QA: 0 failures
```
