# Issue 004: FlyStream commit_stream GIL 死锁

## 概述

**日期**: 2026-06-23
**现象**: worker 进程在 `write_and_remove` task 执行时永久卡住，`remove_object` 的 `RemoveRequest` 30s 超时
**根因**: FlyBufferPtr 跨越 Python↔C++ 边界后携带 nanobind `py_deleter`，在非 Python 线程析构时请求 GIL 导致死锁
**修复**: `commit_stream` 创建纯 C++ FlyBuffer 副本，剥离 py_deleter

---

## 现象

启用 FlyStream 替换 `write_object` 的 Python 对象序列化路径后，以下测试全部失败（超时）：

- `test_remove_object.py` — Phase 2 子进程超时
- `test_mapreduce_*.py` — 全部超时
- `test_ras_graph.py` / `test_golden_*.py` — 全部超时
- `test_stress_stability.py` — 超时

所有失败共同特征：worker 上的 task 执行 `write_object`（FlyStream 路径）后，后续的 `remove_object` 或远程读取操作卡住。

---

## 排查过程

### 阶段 1：初步定位（日志方式）

在 `write_and_remove` 的每一步加日志：
```
A: start → B: stream created → C: pickle dumped → D: flushed
→ E: finished → F: stream deleted → G: committed → H: removed
```

**结果**：worker 日志显示执行到 `G: committed` 后卡住。`remove_object` 内部的 `request_remove` 发送了 `RemoveRequest`，等 `RemoveAck` 超时。

**误判**：以为是 `RemoveRequest` 消息丢失或 master 未处理。但 master 日志显示 `RemoveRequest completed` — master 正常发送了 `RemoveAck`。

### 阶段 2：对比新旧路径（静态分析方式）

对比 `_write_pickle_bytes`（旧路径）和 `_commit_stream`（新路径）的差异：

| 方面 | 旧路径 | 新路径 |
|------|--------|--------|
| 序列化 | `pickle.dumps` → bytes | `FlyStream` + `pickle.dump` |
| 压缩 | `compress_buffered_data` → FlyBuffer | FlyStream 内部 `CompressingStreamBuf` → FlyBuffer |
| 提交 | `commit_write` | `commit_write` |
| FlyBuffer 创建方 | C++ `CMMakeShared<FlyBuffer>()` | FlyStream 的 `write_buf_`（通过 nanobind 返回给 Python） |
| 数据格式 | 相同（已验证字节级一致） | 相同 |

**误判**：两者 `commit_write` 完全相同，数据格式相同，差异应该在 FlyStream 对象的生命周期上。

### 阶段 3：FlyStream 生命周期假设（猜测方式）

猜测 FlyStream 的 `finish_write()` 返回 `write_buf_` 的 shared_ptr 副本后，FlyStream 仍持有 streambuf 链引用同一个 FlyBuffer，可能与 WriteBackQueue 的磁盘写入线程竞争。

**尝试**：在 `finish_write()` 中 `reset()` 所有 streambuf 和 `write_buf_`。

**结果**：泄露消失了，但死锁仍然存在。方向错误。

### 阶段 4：nanobind 序列化假设（猜测方式）

猜测 `database.py` 导入 `FlyStream` 污染了模块全局命名空间，导致 cloudpickle 序列化 task 函数时尝试 pickle `FlyStream` 类（不可 pickle 的 nanobind 对象）。

**尝试**：改用延迟导入。

**结果**：错误信息确实出现在内联 `@as_task` 函数测试中，但 `e2e_tasks.py` 的模块级 task 函数不受影响。这是一个真实但独立的问题，不是死锁根因。

### 阶段 5：错误的归因

多次将失败归因于"pre-existing bug"（如 `stress_stability` 的 `kMaxCompletedTasks=100` 限制）。虽然该 bug 确实存在，但它不是 FlyStream 死锁的原因。

### 阶段 6：GDB 线程栈分析（正确方法）

放弃猜测，使用 GDB 直接获取卡住 worker 的线程栈：

```bash
WORKER_PID=$(pgrep -f "worker_id" | head -1)
gdb -batch -ex "thread apply all bt" -p $WORKER_PID
```

**Thread 7（reactor 线程）栈**：
```
DataService::remove_local_index
  → ObjectCache::remove
    → unordered_map::erase
      → std::any::~any()
        → shared_ptr<FlyBuffer>::~shared_ptr()
          → py_deleter::operator()
            → gil_scoped_acquire         ← 请求 GIL！
              → PyGILState_Ensure         ← 阻塞等待 GIL
```

**Thread 3（task executor 线程）栈**：
```
request_remove → cv_.wait   ← 持有 GIL，等待 reactor 处理 RemoveAck
```

**根因立即明确**：AB-BA 死锁。

---

## 根因

```
Task executor 线程（持有 GIL）：
  write_and_remove task 执行
  → db.remove_object(key)
  → request_remove 发送 RemoveRequest
  → cv_.wait 等待 RemoveAck（仍然持有 GIL）
  → 阻塞...

Reactor 线程（需要 GIL）：
  收到 RemoveCommand
  → on_remove_command
  → remove_local_index
  → ObjectCache::remove
  → 析构缓存条目中的 FlyBufferPtr
  → py_deleter 请求 GIL     ← Task executor 持有 GIL，阻塞！
  → 阻塞...

Task executor 等 reactor 处理 RemoveAck
Reactor 等 task executor 释放 GIL
= 死锁
```

**为什么旧路径没有这个问题**：`_write_pickle_bytes` 在 C++ 内部创建 FlyBuffer（`CMMakeShared<FlyBuffer>()`），这个 shared_ptr 没有自定义 deleter。ObjectCache 析构时直接释放内存，不需要 GIL。

**为什么新路径有这个问题**：FlyStream 的 `finish_write()` 返回 `write_buf_` 给 Python（通过 nanobind 的 `_commit_stream` 参数绑定）。nanobind 为跨越 Python 边界的 `shared_ptr<FlyBuffer>` 附加了 `py_deleter`，以确保 Python 对象引用计数归零时正确清理 C++ 对象。`commit_write` 将这个带 py_deleter 的 shared_ptr 存入 ObjectCache。当 reactor 线程删除缓存条目时，py_deleter 尝试获取 GIL → 死锁。

---

## 修复

在 `commit_stream` 中创建纯 C++ FlyBuffer 副本：

```cpp
fly::WriteErrorType Database::commit_stream(...) {
    // 创建纯 C++ 副本，剥离 nanobind py_deleter
    auto pure_record = CMMakeShared<FlyBuffer>();
    pure_record->write(record->data(), record->size());
    // ... 解析 header，调 commit_write(pure_record) ...
}
```

这个副本没有 py_deleter，ObjectCache 析构时不需要 GIL。

---

## 经验教训

### 1. 猜测 vs 证据

**错误做法**：花了数小时猜测根因（生命周期、序列化、时序、DataService mutex 死锁），每次都添加更多日志验证假设。**这些猜测全部错误。**

**正确做法**：用 GDB 获取线程栈，5 分钟内看到 `PyGILState_Ensure` 在 `py_deleter` 中调用，立即定位根因。

**原则**：当进程卡住（而非崩溃或逻辑错误）时，**第一时间用 GDB/pstack 获取线程栈**，不要先加日志。

### 2. nanobind 对象生命周期的陷阱

nanobind 导出的 C++ 对象，其 `shared_ptr` 跨越 Python 边界后会携带 `py_deleter`。如果这些 shared_ptr 被存储在 C++ 容器中（如 ObjectCache），且容器在非 Python 线程中被析构，就会触发 GIL 死锁。

**预防规则**：任何从 Python 接收的 `shared_ptr<T>`（nanobind 导出类型），如果要长期存储在 C++ 侧，必须先创建纯 C++ 副本。

### 3. 不要归因于"pre-existing bug"

在排查过程中多次将失败归因于"pre-existing"问题（如 `kMaxCompletedTasks`），浪费了时间。虽然这些问题确实存在，但它们不是当前 bug 的原因。**用户的改动引入的 bug，必须在用户的代码中寻找根因。**

### 4. 正确的排查顺序

1. **复现**：构造最小可复现 case
2. **日志**：在关键路径加日志，缩小范围
3. **GDB**（死锁/卡住时）：直接获取线程栈，不要继续加日志
4. **对比**：对比新旧路径的完整调用链差异
5. **修复**：基于证据最小修复，全量测试验证
