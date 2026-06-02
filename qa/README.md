# Fly QA 测试指南

## 1. 运行测试

```bash
# 构建fly二进制
./fly.sh build //src/main/cpp:fly

# 运行全部QA测试
./qa/runqa

# 运行指定测试（支持 -j 控制并发数）
./qa/runqa -j2 qa/test_remove_object.py

# 兼容旧入口
./qa/run_qa_tests.sh
```

### 测试运行机制

`runqa`（Python 运行器）对每个 `test_*.py` 文件：

1. 用 `bazel-bin/src/main/cpp/fly --log-dir <dir> <test_file>` 启动一个**独立进程**
2. fly 二进制初始化全新的 C++ 运行时（DataService、StorageManager 等单例都是全新的）
3. 执行 Python 测试脚本
4. 通过进程退出码判断 pass/fail（0=pass, 非0=fail）
5. 日志写入 `qa/logs/<test_name>/` 目录

---

## 2. 创建新测例

### 基本模板

```python
import time
import sys
import os
import shutil

DB_PATH = "/tmp/fly_e2e_<test_name>_db"

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '..', 'src'))

from e2e_tasks import write_data
from fly import open_db, get_config


def cleanup():
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)


def wait_for(condition, timeout=20.0, interval=0.5):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if condition():
            return True
        time.sleep(interval)
    return False


cleanup()
get_config().set_int("fail_unscheduleable_tasks", 0)

from fly.runtime import get_agent
master = get_agent()

master.launch_local_workers([{}])
assert wait_for(lambda: master._agent.get_connection_count() >= 1)

db = open_db(DB_PATH)

# ... test logic ...

master.stop()
print(f"[PASS] test_<feature>", file=sys.stderr)
```

### 多进程测试（协调器模式）

如果测试需要跨进程重启（如 load_db），使用协调器模式：

```
qa/
├── test_load_db.py          # 协调器（QA runner 发现此文件）
├── load_db_run1.py          # 辅助脚本（不是test_前缀，不会被QA runner直接运行）
└── load_db_run2.py          # 辅助脚本
```

协调器通过 `subprocess.run` 运行辅助脚本：

```python
import subprocess

FLY_BIN = os.path.join(PROJECT_ROOT, "bazel-bin", "src", "main", "cpp", "fly")

result = subprocess.run(
    [FLY_BIN, "--log-dir", log_dir, script_path],
    capture_output=True, text=True, timeout=120, cwd=PROJECT_ROOT,
)
assert result.returncode == 0, f"Failed: {result.stderr}"
```

### 任务函数

所有任务函数定义在 `/root/fly/src/e2e_tasks.py`。常用任务：

| 任务 | 说明 |
|------|------|
| `write_data(db, key, value)` | 写入数据（无依赖） |
| `read_data(db, key, deps)` | 读取数据（有依赖） |
| `gpu_write(db, key, value)` | 需要 gpu 属性 |
| `write_and_remove(db, key, value)` | 写入后删除 |
| `compute_sum(db, read_a, read_b, result)` | 读取两个值，写入和 |

如需新任务，在 `src/e2e_tasks.py` 中添加。

---

## 3. 必须遵循的原则

### 扁平脚本，不要用 `__main__` 或 `main()` 包装

QA 测试脚本是**扁平的 Python 脚本**，代码从上到下直接执行，像写 bash 一样。

```python
# ✅ 正确 — 代码直接执行
cleanup()
master = get_agent()
master.start()
db = open_db(DB_PATH)
write_data(db, "key", 42)
master.stop()
print("[PASS]", file=sys.stderr)

# ❌ 错误 — 不要用 if __name__ 或 main() 包装
def main():
    cleanup()
    master = get_agent()
    ...

if __name__ == "__main__":
    main()
```

**原因**：QA runner 通过 `fly --log-dir <dir> <script>` 启动独立进程执行脚本，脚本内容会被 `exec()` 直接执行。不需要 `__main__` 守卫，也不需要 `main()` 函数。

### Worker 模式

`launch_local_workers` 始终使用进程模式启动 Worker：

```python
# Worker 进程通过 subprocess.Popen 启动 fly --worker 子进程
master.launch_local_workers([{}])
```

**架构**：
- Worker 是独立进程，拥有独立的 DataService 单例和内存空间
- Worker 进程通过 TCP 连接 Master，实现真正的进程隔离
- 生产环境与测试环境使用完全一致的通信方式

### 不要在测试脚本中定义 `@as_task()` 函数

```python
# ❌ 错误 — Worker 进程无法看到此函数
@as_task()
def my_task(db, key):
    db.write_object(key, "value")

# ✅ 正确 — 使用 e2e_tasks.py 中的任务
from e2e_tasks import write_data
write_data(db, "key", "value")
```

**原因**：Process worker 是独立进程，只导入 `e2e_tasks.py` 中的任务。内联定义的 `@as_task()` 函数在 Worker 进程中不可见，会导致任务执行失败。

如需新任务，统一添加到 `src/e2e_tasks.py`。

### 只使用公共 Python API

QA 测试是高层集成测试，必须只使用公共 Python API。底层 C++ 测试属于 unit test（`src/*/tests/`），不属于 QA。

```python
# ✅ 正确 — 使用公共 API
master = get_agent()
master.launch_local_workers([{}])
master.wait_for_workers(1)
assert master.worker_count >= 1
assert len(master.completed_tasks) >= 1

# ❌ 错误 — 使用内部 API
master._agent.get_connection_count()
master._agent.submit_task_with_deps(...)
master._worker_procs[0].kill()
import _fly_storage as storage
storage.ex_stg_get_data_service()
```

**公共 API 速查**：

| API | 说明 |
|-----|------|
| `get_agent()` | 获取 Master/Worker 单例 |
| `master.launch_local_workers(configs)` | 启动 Worker 进程 |
| `master.wait_for_workers(n, timeout=30)` | 等待 N 个 Worker 连接，返回 True/False |
| `master.worker_count` | 当前连接的 Worker 数量 |
| `master.is_running()` | Master 是否在运行 |
| `master.get_worker_pids()` | 获取 Worker 进程 PID 列表 |
| `master.port` | Master 监听端口 |
| `master.completed_tasks` | 已完成任务 ID 列表 |
| `master.failed_tasks` | 失败任务 ID 列表 |
| `master.pending_tasks` | 等待中任务 ID 列表 |
| `master.stop()` | 停止 Master 和所有 Worker |
| `open_db(path)` | 打开/创建数据库 |
| `load_db(path)` | 加载已有数据库 |
| `db.read_object(name, cache="low")` | 读取数据 |
| `db.write_object(name, obj, save_to_db=True)` | 写入数据 |
| `db.remove_object(name)` | 删除数据 |

### 每个测试文件只测试一个场景

一个文件 = 一个场景 = 一次 agent 生命周期。需要多次启停 agent 的场景，使用协调器模式（subprocess）。

```python
# ✅ 正确 — 单场景单文件
cleanup()
master = get_agent()
master.launch_local_workers([{}])
# ... test logic ...

# ❌ 错误 — 多个测试函数共享 agent
def test_part1():
    master = get_agent()
    master.launch_local_workers([{}])
    # ...

def test_part2():
    master = get_agent()  # 复用 Part1 的 agent！
    # ...
```

### 文件命名

| 类型 | 命名 | 说明 |
|------|------|------|
| 测试文件 | `test_<name>.py` | QA runner 自动发现并运行 |
| 辅助脚本 | `<name>.py`（非 test_ 前缀） | 由测试文件通过 subprocess 调用 |

### DB 路径

使用 `/tmp/fly_e2e_<test_name>_db` 格式，测试前清理，测试后不清理（方便调试）。

### 日志输出

使用 `print(..., file=sys.stderr)` 输出测试进度。fly 二进制将 stdout/stderr 重定向到 `qa/logs/`。

### 资源清理

- 每个 test 函数开头调用 `cleanup()` 删除旧 DB
- 测试结束时调用 `master.stop()`
- 不要在 `finally` 中清理 DB 目录（保留现场便于调试）

### 等待模式

```python
# ✅ 推荐 — 使用 wait_for 辅助函数
def wait_for(condition, timeout=20.0, interval=0.5):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if condition():
            return True
        time.sleep(interval)
    return False

assert wait_for(lambda: master._agent.get_connection_count() >= 1)

# 或使用 master 内置方法
completed = master.wait_for_all_tasks(expected=3, timeout=30)

# ❌ 错误 — 硬编码 sleep
time.sleep(5)
```

### 不要使用 `from fly.runtime import reset`

QA 测试由 fly 二进制以独立进程运行。`reset()` 用于单进程内模拟重启，不适合 QA 测试。如需跨进程测试，使用 subprocess 协调器模式。

---

## 4. 目录结构

```
qa/
├── README.md                    # 本文档
├── runqa                       # Python 测试运行器（-j 并发、计时、排序）
├── run_qa_tests.sh             # 兼容入口（薄 wrapper → runqa）
├── .gitignore                   # 忽略 logs/
├── BUILD                        # Bazel 构建定义
│
├── test_*.py                    # 测试文件（QA runner 自动发现）
├── <helper>.py                  # 辅助脚本（由 test_*.py 调用）
│
└── logs/                        # 测试日志（gitignore）
    └── <test_name>/
        ├── master.log
        └── ...
```

---

## 5. 常见问题

**Q: 测试超时怎么办？**

检查 `qa/logs/<test_name>/master.log` 和 `worker*.log`，确认 Worker 是否连接成功、任务是否被调度。

**Q: Worker 连接失败？**

确认 `./fly.sh build //src/main/cpp:fly` 已执行，fly 二进制存在且可执行。

**Q: 测试间互相干扰？**

每个测试使用独立的 `DB_PATH`（`/tmp/fly_e2e_<name>_db`），不应互相干扰。如果使用了相同的路径，修改为唯一路径。

---

## 6. 压力测试

### 覆盖场景

| 测试文件 | 场景 | 验证内容 |
|----------|------|----------|
| `test_stress_concurrent_write.py` | 2 Worker 并发写入同一 DB | 50 对象写入完整性、无数据丢失 |
| `test_stress_dep_chain.py` | 20 步串行依赖链 | `increment` 任务逐步 +1，最终值正确 |
| `test_stress_readwrite.py` | 并发读写混合 | 20 写 + 20 读同时进行，读结果一致 |
| `test_stress_cross_db.py` | 跨 DB 数据传输 | 3 DB 间 cross_db_copy + triple_db_sum 链 |
| `test_stress_freeze_reject.py` | freeze 后写入拒绝 | 10 写完成 → freeze → 10 写全部失败 |
| `test_stress_multi_db.py` | 多 DB 并行操作 | 4 DB × 10 写并行，互不干扰 |
| `test_stress_stability.py` | 长时间稳定性 | 100 写 + 50 compute_sum + 2×5MB 大对象 |

### 未覆盖场景

以下两种场景暂未覆盖测试：

1. **Worker 写入中途 crash**: Worker 在 `write_object` 落盘过程中崩溃（数据可能写入 data 文件但 idx 未持久化）。当前 `test_worker_crash.py` 测试的是 Worker 连接断开后的任务恢复，不涉及写半完成状态。
2. **失败 task 重跑写覆盖**: `restart_failed_tasks()` 重新执行曾写入过数据的 task，导致同一 key 的数据被重复写入。已知影响：`find_entry()` 返回旧数据、`restore_entries()` 不去重导致 `read_from_entries` 拼接。待实现 `find_entry() → back()` 和 `restore_entries()` 去重后补充测试。
