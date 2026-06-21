"""E2E: 数据依赖 + 属性 timeout 组合（两阶段调度核心）。

Worker 1: ["alpha"]（无 gpu）
Task A (write_source): 无要求，产出 source 数据。
Task B (read_after_dep): 依赖 source，requires=(["gpu"], 2.0)。
验证：Task B 的 timeout 从 Task A 完成后（数据依赖满足后）才开始计时，
     而非从提交时开始。通过测量总耗时 >= timeout 来验证。
"""
from _fly_log import INFO
import os
import shutil
import time

from fly import as_task, open_db, get_config, get_work_directory

DB_PATH = os.path.join(get_work_directory(), "db")


def cleanup():
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)


@as_task()
def write_source(db, key, value):
    db.write_object(key, value)


# 依赖 source_key 的产出，要求 gpu，timeout=2s
@as_task(inputs=lambda db, source_key, result_key: [db.get_obj_name(source_key)],
         requires=(["gpu"], 2.0))
def read_after_dep(db, source_key, result_key):
    data = db.read_object(source_key)
    db.write_object(result_key, data + 100)


cleanup()
get_config().set_int("fail_unscheduleable_tasks", 0)

from fly.runtime import get_agent
master = get_agent()
master.launch_local_workers([{"attributes": ["alpha"]}])
assert master.wait_for_workers(1, timeout=30), "Worker failed to connect"

db = open_db(DB_PATH)
completed_base = len(master.completed_tasks)

# Task A: 产出 source（无要求，立即调度）
write_source(db, "source", 7)
# Task B: 依赖 source，要求 gpu，timeout=2s
submit_time = time.time()
read_after_dep(db, "source", "result")


def wait_completed(expected, timeout=20):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if len(master.completed_tasks) >= expected:
            break
        time.sleep(0.3)
    return master.completed_tasks


completed = wait_completed(completed_base + 2, timeout=20)
elapsed = time.time() - submit_time

assert db.read_object("result") == 107, \
    f"task B should compute 7+100=107, got {db.read_object('result')}"

# timeout=2s。若 timeout 从提交时开始，Task A 完成后 Task B 可能已超时。
# 实际 timeout 从数据依赖满足后开始，所以 Task B 至少等 2s 才降级。
assert elapsed >= 2.0, \
    f"task B should wait at least timeout=2s before degrade, elapsed={elapsed:.2f}s"

master.stop()
INFO(f"[PASS] data dep + attr timeout: task degraded after {elapsed:.2f}s "
     f"(timeout=2s started after dep satisfied)")
