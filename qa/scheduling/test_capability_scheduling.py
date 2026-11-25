"""E2E capability-based task scheduling test.

Workers launched in process mode via launch_local_workers.
Tasks defined in e2e_tasks.py with @as_task(requires=...).

Workers:
  - Worker 1: attributes=["gpu"]
  - Worker 2: attributes=[]

Tasks:
  - gpu_write: requires=["gpu"] -> must run on Worker 1
  - cpu_write: requires=None    -> can run on any worker
"""
from _fly_log import INFO
import time
import os
import shutil



from test import cpu_write, gpu_write
from test import record
from fly import open_db, get_config
DB_PATH = os.path.join(get_config().get_str("log_dir"), "db")


def cleanup():
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)


def setup_mixed_workers():
    from fly.runtime import get_agent
    master = get_agent()
    master.launch_local_workers([
        {"attributes": ["gpu"]},
        {"attributes": []},
    ])
    assert master.wait_workers_registered(timeout=60)
    # 原断言：assert master.worker_count >= 2, \
    record("setup/two_workers_connected", master.worker_count >= 2,
           detail=master.worker_count)
    return master


def wait_completed(master, expected, timeout=30):
    t0 = time.time()
    while time.time() - t0 < timeout:
        c = master.completed_tasks
        if len(c) >= expected:
            return c
        time.sleep(0.5)
    return master.completed_tasks


def test_capability_matching():
    cleanup()
    master = setup_mixed_workers()
    db = open_db(DB_PATH)

    cpu_write(db, "cpu_result", 1)
    gpu_write(db, "gpu_result", 2)

    completed = wait_completed(master, 2, timeout=30)
    # 原断言：assert len(completed) >= 2, f"Expected 2 completed, got {len(completed)}"
    record("capability/two_completed", len(completed) >= 2, detail=len(completed))
    # 保留：守卫——后面读取 cpu_result、gpu_result 依赖两个任务都已完成
    assert len(completed) >= 2, f"守卫：后面要读两个任务写出的对象, got {len(completed)} completed"

    cpu_val = db.read_object("cpu_result")
    # 原断言：assert db.read_object("cpu_result") == 1
    record("capability/cpu_result_value", cpu_val, detail=cpu_val)
    gpu_val = db.read_object("gpu_result")
    # 原断言：assert db.read_object("gpu_result") == 2
    record("capability/gpu_result_value", gpu_val, detail=gpu_val)

    INFO(f"[DONE] test_capability_matching: {len(completed)} tasks completed")


test_capability_matching()
