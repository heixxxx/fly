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



from e2e_tasks import cpu_write, gpu_write
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
    for i in range(40):
        if master.worker_count >= 2:
            break
        time.sleep(0.5)
    assert master.worker_count >= 2, \
        f"Only {master.worker_count}/2 workers connected"
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
    assert len(completed) >= 2, f"Expected 2 completed, got {len(completed)}"

    assert db.read_object("cpu_result") == 1
    assert db.read_object("gpu_result") == 2

    INFO(f"[PASS] test_capability_matching: {len(completed)} tasks completed")


test_capability_matching()
