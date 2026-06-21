"""E2E: 多个 waiting task 竞争同一属性。

Worker 1: ["alpha"]，Worker 2: ["beta"]（均无 gpu）
提交 3 个 requires=(["gpu"], 30.0) 的 task，全部 waiting（集群无 gpu）。
然后某 worker 动态添加 gpu 属性。
验证：属性变化触发调度，waiting task 被调度执行（至少 1 个立即调度）。
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


NUM_TASKS = 3


# 动态 requires：要求 gpu，timeout=30s
@as_task(requires=lambda db, key, value: (["gpu"], 30.0))
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

# 提交 NUM_TASKS 个要求 gpu 的 task，全部应 waiting
for i in range(NUM_TASKS):
    waiting_gpu_write(db, f"compete_{i}", i)

# 确认全部 waiting
time.sleep(1)
assert len(master.completed_tasks) == completed_base, \
    "all gpu tasks should be waiting (no gpu worker)"

# 某 worker 动态添加 gpu 属性
add_gpu_property(db, "trigger", "x")

# 属性变化触发调度：至少 1 个 waiting task 应被调度


def wait_completed(expected, timeout=30):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if len(master.completed_tasks) >= expected:
            break
        time.sleep(0.3)
    return master.completed_tasks


completed = wait_completed(completed_base + NUM_TASKS + 1, timeout=30)
for i in range(NUM_TASKS):
    assert db.read_object(f"compete_{i}") == i, \
        f"task compete_{i} should complete with value {i}"

master.stop()
INFO(f"[PASS] multi compete: {NUM_TASKS} waiting tasks all completed after gpu added")
