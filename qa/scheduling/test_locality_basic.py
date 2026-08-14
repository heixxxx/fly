"""E2E: data locality 调度基本功能验证。

验证：当 locality_scheduling_enabled=1 时，依赖某对象的 task 被调度到持有该对象的 worker。

设计：
  - 启动 2 个无 capability 的 worker（确保阶段 A 不抢断，纯测 locality 偏好）。
  - 提交 write_payload_with_worker 写大对象 big_obj + worker 标记 holder_marker。
  - 等 ready 后，提交 locality_consume 依赖 big_obj。
  - 断言：consume_result（执行 worker id）== holder_marker（持有 big_obj 的 worker id）。

两个 worker 都无 capability、task 无 requires，调度决策完全由 locality_order_ 决定（阶段 B）。
"""
from _fly_log import INFO
import os
import shutil
import time

from test import write_payload_with_worker, locality_consume
from fly import open_db, get_config

DB_PATH = os.path.join(get_config().get_str("log_dir"), "db")


def cleanup():
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)


def wait_completed(master, expected, timeout=60):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if len(master.completed_tasks) >= expected:
            return master.completed_tasks
        time.sleep(0.3)
    return master.completed_tasks


cleanup()
# 启用 data locality 调度
get_config().set_int("locality_scheduling_enabled", 1)

from fly.runtime import get_agent
master = get_agent()
master.launch_local_workers([
    {"attributes": []},
    {"attributes": []},
])
assert master.wait_workers_registered(timeout=60)
assert master.worker_count >= 2, f"Only {master.worker_count}/2 workers connected"

db = open_db(DB_PATH)

# 写大对象（~8MB）+ 自报 worker 标记。
write_payload_with_worker(db, "big_obj", "holder_marker", 1_000_000)

completed = wait_completed(master, 1, timeout=60)
assert len(completed) >= 1, "write_payload_with_worker did not complete"

# 持有 big_obj 的 worker id
holder_worker = db.read_object("holder_marker")
INFO(f"[LOCALITY] big_obj held by worker {holder_worker}")

# 提交依赖 big_obj 的 consume task（应被调度到 holder_worker）
locality_consume(db, "big_obj", "consume_result")

completed2 = wait_completed(master, 2, timeout=60)
assert len(completed2) >= 2, "locality_consume did not complete"

# consume_result 记录执行 consume 的 worker id
consume_worker = db.read_object("consume_result")
INFO(f"[LOCALITY] locality_consume ran on worker {consume_worker}, big_obj holder is worker {holder_worker}")

assert int(consume_worker) == int(holder_worker), \
    f"locality FAILED: consume ran on worker {consume_worker}, expected {holder_worker} (data holder)"

master.stop()
INFO("[PASS] locality basic: consume task scheduled to data-holding worker")
