# Fly QA 测试指南

## 1. 运行测试

```bash
# 构建fly二进制
./fly.sh build //src/main/cpp:fly

# 运行全部QA测试
./qa/run_qa_tests.sh

# 运行指定测试
./qa/run_qa_tests.sh qa/test_remove_object.py
```

### 测试运行机制

`run_qa_tests.sh` 对每个 `test_*.py` 文件：

1. 用 `bazel-bin/src/main/cpp/fly --log-dir <dir> <test_file>` 启动一个**独立进程**
2. fly 二进制初始化全新的 C++ 运行时（DataService、StorageManager 等单例都是全新的）
3. 执行 Python 测试脚本
4. 通过进程退出码判断 pass/fail（0=pass, 非0=fail）
5. 日志写入 `qa/logs/<test_name>/` 目录

---

## 2. 创建新测例

### 基本模板

```python
"""E2E test: <简要描述>."""
import time
import sys
import os
import shutil

DB_PATH = "/tmp/fly_e2e_<test_name>_db"

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '..', 'src'))

from e2e_tasks import write_data
from fly import open_db
from fly.config import get_config


def cleanup():
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)


def test_<feature>():
    cleanup()
    get_config().set_int("fail_unscheduleable_tasks", 0)

    from fly.runtime import get_agent
    master = get_agent()
    if not master._running:
        master.start()

    master.launch_local_workers([{}], mode="process")
    for i in range(40):
        if master._agent.get_connection_count() >= 1:
            break
        time.sleep(0.5)
    assert master._agent.get_connection_count() >= 1

    db = open_db(DB_PATH)

    # ... test logic ...

    master.stop()
    print(f"[PASS] test_<feature>", file=sys.stderr)


if __name__ == "__main__":
    test_<feature>()
    print("\nAll tests passed!")
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

### Worker 模式：必须使用 `mode="process"`

```python
# ✅ 正确
master.launch_local_workers([{}], mode="process")

# ❌ 错误 — QA 测试必须使用进程模式
master.launch_local_workers([{}], mode="thread")
```

**原因**：
- Process 模式模拟真实分布式场景（独立进程、独立内存空间）
- Thread 模式共享 DataService 单例，掩盖单例状态泄漏等 bug
- Worker 进程通过 `subprocess.Popen` 启动 `fly --worker` 子进程

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
├── run_qa_tests.sh              # 测试运行器
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
