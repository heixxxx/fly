"""E2E: attribute timeout 到期后降级调度。

Worker: ["alpha"]（无 gpu）
Task: requires=(["gpu"], 2.0)，集群无 gpu worker，2 秒后降级调度到 alpha worker。
验证：task 最终被降级调度并执行（非死等 fail）。
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


@as_task(requires=(["gpu"], 2.0))
def soft_gpu_write(db, key, value):
    db.write_object(key, value)


cleanup()
get_config().set_int("fail_unscheduleable_tasks", 0)

from fly.runtime import get_agent
master = get_agent()
master.launch_local_workers([{"attributes": ["alpha"]}])
assert master.wait_for_workers(1, timeout=30), "Worker failed to connect"

db = open_db(DB_PATH)

soft_gpu_write(db, "result", 42)


def wait_completed(expected, timeout=20):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if len(master.completed_tasks) >= expected:
            break
        time.sleep(0.3)
    return master.completed_tasks


completed = wait_completed(1, timeout=20)
assert len(completed) >= 1, \
    "soft_gpu_write should degrade-schedule after timeout, " \
    f"completed={len(completed)}, failed={master.failed_tasks}"

assert db.read_object("result") == 42, \
    f"task should write 42, got {db.read_object('result')}"

master.stop()
INFO("[PASS] attr timeout expire: task degraded-scheduled after 2s timeout")
