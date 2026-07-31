"""E2E: 任务优先级 — priority 高的 task 先被调度执行。

单 worker 强制串行：先提交低优先级 task，再提交高优先级 task，
验证高优先级插队先执行（单 worker 下 completed_tasks 顺序 = 调度执行顺序）。
"""
from _fly_log import INFO
import os
import shutil
import time

from fly import as_task, open_db, get_work_directory

DB_PATH = os.path.join(get_work_directory(), "db")


def cleanup():
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)


# 低优先级 task：priority=5（低于默认 10，让路）
@as_task(priority=5)
def low_prio_write(db, key, value):
    db.write_object(key, value)


# 高优先级 task：priority=20（高于默认 10，抢先）
@as_task(priority=20)
def high_prio_write(db, key, value):
    db.write_object(key, value)


cleanup()

from fly.runtime import get_agent

master = get_agent()
# 单 worker 强制串行：两 task 同时 ready 时，priority 决定谁先被调度
master.launch_local_workers([{"attributes": []}])
assert master.wait_for_workers(1, timeout=30), "Worker failed to connect"

db = open_db(DB_PATH)

# 关键时序：先提交低优先级，再提交高优先级
# 若 priority 生效，高优先级应插队先完成（即便后提交）
low_prio_write(db, "low", 1)
high_prio_write(db, "high", 2)

completed = []
t0 = time.time()
while time.time() - t0 < 30:
    completed = master.completed_tasks
    if len(completed) >= 2:
        break
    time.sleep(0.3)

assert len(completed) >= 2, f"Expected 2 completed tasks, got {len(completed)}"

# 单 worker 串行下，completed_tasks 顺序 = 调度执行顺序
# high_prio_write 后提交但 priority=20，应先于 priority=5 的 low_prio_write 完成
# task_id 是递增分配的：low=1, high=2
assert completed[0] == 2, (
    f"High-priority task (id=2, priority=20) should complete first, "
    f"got order={completed}")
assert completed[1] == 1, (
    f"Low-priority task (id=1, priority=5) should complete second, "
    f"got order={completed}")

# 校验对象值确实写入（正确性）
assert db.read_object("low") == 1
assert db.read_object("high") == 2

master.stop()
INFO(f"[PASS] priority scheduling: completion order={completed} "
     f"(high-priority id=2 first, low-priority id=1 second)")
