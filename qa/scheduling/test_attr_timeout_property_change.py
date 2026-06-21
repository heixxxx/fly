"""E2E: worker 属性变化及时触发 waiting task 调度。

Worker 1: ["alpha"]，Worker 2: ["beta"]（均无 gpu）
Task A: requires=(["gpu"], 30.0)，timeout 远大于测试时间，确保不会因超时降级。
Task B: 无要求，执行时动态添加 gpu 属性到所在 worker。
验证：Task A 在 Task B 添加 gpu 属性后立即被调度（而非等 30s 超时）。
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


# 要求 gpu，timeout=30s（远大于测试时间，确保不会因超时降级）
@as_task(requires=(["gpu"], 30.0))
def waiting_gpu_write(db, key, value):
    db.write_object(key, value)


# 无要求，执行时动态添加 gpu 属性
@as_task()
def add_gpu_property(db, key, value):
    from fly.runtime import get_agent
    get_agent().set_worker_property("gpu")
    db.write_object(key, value)


cleanup()
get_config().set_int("fail_unscheduleable_tasks", 0)

from fly.runtime import get_agent
master = get_agent()
master.launch_local_workers([
    {"attributes": ["alpha"]},
    {"attributes": ["beta"]},
])
assert master.wait_for_workers(2, timeout=30), "Workers failed to connect"

db = open_db(DB_PATH)
completed_base = len(master.completed_tasks)

# 提交 waiting_gpu_write：集群无 gpu，应进入 waiting 状态
waiting_gpu_write(db, "gpu_result", 77)

# 确认 task 仍 waiting（未完成）
time.sleep(1)
assert len(master.completed_tasks) == completed_base, \
    "gpu task should still be waiting (no gpu worker yet)"

# 提交 add_gpu_property：在某 worker 上动态添加 gpu 属性
add_gpu_property(db, "trigger", "x")

# waiting_gpu_write 应在属性添加后立即被调度（不需要等 30s 超时）


def wait_completed(expected, timeout=15):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if len(master.completed_tasks) >= expected:
            break
        time.sleep(0.3)
    return master.completed_tasks


completed = wait_completed(completed_base + 2, timeout=15)
assert db.read_object("gpu_result") == 77, \
    f"gpu task should be scheduled after property change"

master.stop()
INFO("[PASS] attr property change: waiting task scheduled immediately after gpu added")
