"""E2E: attribute timeout<0 死等，集群无 worker 能获得属性时 fail。

Worker: ["alpha"]（无 nonexistent 属性）
Task: requires=(["nonexistent"], -1)，死等，集群永远无法获得该属性。
验证：task 在死锁检测下被 fail（而非永远等待或降级调度）。
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


@as_task(requires=(["nonexistent_attr"], -1))
def dead_wait_write(db, key, value):
    db.write_object(key, value)


cleanup()
# 启用 fail_unscheduleable_tasks，让死锁检测生效
get_config().set_int("fail_unscheduleable_tasks", 1)

from fly.runtime import get_agent
master = get_agent()
master.launch_local_workers([{"attributes": ["alpha"]}])
assert master.wait_for_workers(1, timeout=30), "Worker failed to connect"

db = open_db(DB_PATH)

dead_wait_write(db, "dead_result", 1)


def wait_failed(expected, timeout=15):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if len(master.failed_tasks) >= expected:
            break
        time.sleep(0.3)
    return master.failed_tasks


failed = wait_failed(1, timeout=15)
assert len(failed) >= 1, \
    "dead-wait task should fail (no worker can acquire property), " \
    f"failed={len(failed)}, completed={len(master.completed_tasks)}"

master.stop()
INFO("[PASS] attr timeout<0 dead-wait: task failed as expected")
