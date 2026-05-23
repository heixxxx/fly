# Test 模块 — 测试基础设施

## 模块概述

**位置**: `src/test/`

Test 模块不是框架功能模块，而是 **测试基础设施**——提供 QA 集成测试和单元测试共用的 C++ 测试对象、Python 导出和任务定义。

---

## 核心文件

| 文件 | 说明 |
|------|------|
| `cpp/test_object.h` | 可序列化的测试用 C++ 结构体 `TestObject`（`value` + `name`） |
| `export/test_export.cpp` | nanobind 导出：`EXTestObject` 类 + `ex_test_parallel_read` 多线程并发读取函数 |
| `py/e2e_tasks.py` | QA 集成测试用的 @as_task 任务集合 |
| `py/test_tasks.py` | Bazel 单元测试用的 @as_task 任务集合（使用 EXTestObject） |

---

## C++ 测试对象

### TestObject

```cpp
class TestObject {
public:
    int64_t value = 0;
    CMString name;

    TestObject() = default;
    TestObject(int64_t v, const CMString& n = "") : value(v), name(n) {}

    FLY_SERIALIZE(value, name);
};
```

最简单的可序列化对象，用于验证 Database 写入/读取/跨 Worker 传输的正确性。

### ex_test_parallel_read

```python
total, local_count, remote_count = ex_test_parallel_read(db, names)
```

多线程并发读取验证函数：对 `names` 中的每个对象名起一个线程并发读取，返回 `(总和, 本地读取数, 远程读取数)`。用于测试 DataService 并发读取的正确性。

---

## Python 任务集合

### e2e_tasks.py — QA 集成测试任务

QA 测试脚本（`qa/` 目录）使用的 @as_task 任务定义，覆盖以下场景：

| 任务 | 用途 |
|------|------|
| `write_data(db, key, value)` | 基础写入 |
| `failing_task(db, key, error_msg)` | 任务失败场景 |
| `write_data_needs_phantom(db, key, value)` | 不可解析依赖（依赖不存在的 phantom） |
| `freeze_db(db, dep_keys)` | 冻结数据库 |
| `read_data(db, key, deps)` | 依赖驱动的读取 |
| `fanout_write(db, keys, values)` | 扇出写入 |
| `gpu_write(db, key, value)` | 需要 GPU capability 的写入 |
| `add_gpu_property(db, key, value)` | 运行时添加 GPU 属性 |
| `remove_gpu_property(db, key)` | 运行时删除 GPU 属性 |
| `alpha_write/beta_write/gamma_write` | 多 capability 调度测试 |
| `shared_write` + `add/remove_shared_on_*` | 动态属性增删调度 |
| `write_and_removed(db, key, value)` | 写后删除 |
| `read_after_remove(db, key, deps)` | 删除后读取 |
| `write_after_freeze(db, key, value)` | freeze 后写入（预期失败） |
| `increment(db, read_key, write_key, deps)` | 读取 +1 写入（依赖链） |
| `compute_sum(db, read_a, read_b, result)` | 读取-计算-写入管道 |
| `cross_db_copy/cross_db_sum` | 跨 DB 依赖 |
| `triple_db_sum` | 三 DB 聚合 |

### test_tasks.py — 单元测试任务

Bazel pytest 使用的 @as_task 任务定义，使用 `EXTestObject`（而非原生 Python 类型）：

| 任务 | 用途 |
|------|------|
| `write_data_task(db, idx)` | 写入 EXTestObject |
| `concurrent_read_task(db, names, out_key)` | 并发读取 + 汇总 |
| `freeze_db_task(db, dep_keys)` | 冻结 |
| `entry_task(db)` | 完整流程：扇出写入 → 并发读取 → 冻结 |

---

## 与其他模块的关系

```
src/test/ 依赖:
  ├── src/storage/    — Database, DataService（读写验证）
  ├── src/serialization/ — 序列化宏（TestObject 序列化）
  ├── src/common/     — CM* 类型别名
  └── src/fly/        — as_task 装饰器（任务定义）

被依赖:
  └── qa/             — QA 集成测试脚本 import e2e_tasks
  └── src/fly/tests/  — Python 单元测试 import test_tasks
```

---

## 构建目标

```bash
# C++ 测试对象库
//src/test/cpp:fly_test_object

# nanobind 导出
//src/test/export:_fly_test.so

# Python 包
//src/test/py:fly_test_py

# C++ 单元测试
//src/test/tests:test_object_test
```
