"""E2E: attribute timeout=0 立即降级调度。

Worker: ["alpha"]（无 gpu）
Task: requires=(["gpu"], 0)，timeout=0 表示数据依赖满足后仅检查一次，无完整匹配立即降级。
验证：task 被立即降级调度（无需等待），且数据正确写入。
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


@as_task(requires=(["gpu"], 0))
def immediate_degrade_write(db, key, value):
    db.write_object(key, value)


cleanup()
get_config().set_int("fail_unscheduleable_tasks", 0)

from fly.runtime import get_agent
master = get_agent()
master.launch_local_workers([{"attributes": ["alpha"]}])
assert master.wait_for_workers(1, timeout=30), "Worker failed to connect"

db = open_db(DB_PATH)

t0 = time.time()
immediate_degrade_write(db, "result", 99)


def wait_completed(expected, timeout=10):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if len(master.completed_tasks) >= expected:
            break
        time.sleep(0.3)
    return master.completed_tasks


completed = wait_completed(1, timeout=10)
elapsed = time.time() - t0

assert len(completed) >= 1, \
    f"immediate_degrade_write should schedule, completed={len(completed)}"
assert db.read_object("result") == 99
# timeout=0 立即降级，应该在很短时间内完成（< 3秒，考虑调度延迟）
assert elapsed < 3.0, \
    f"timeout=0 should degrade immediately, but took {elapsed:.1f}s"

master.stop()
INFO(f"[PASS] attr timeout=0: task degraded immediately in {elapsed:.2f}s")
