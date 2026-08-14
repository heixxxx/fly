"""E2E: capability 完整匹配优先于 data locality 偏好（scheduler 阶段 A 优先于阶段 B）。

验证：即使 locality 偏好指向 worker 0（持有 big_obj），若 task 要求的 capability
只有 worker 1 满足，task 仍被调度到 worker 1。

强制冲突设计：
  - worker 0: attributes=["holder"]（持有 big_obj，但无 gpu）
  - worker 1: attributes=["gpu"]（不持有 big_obj，但有 gpu）
  - write task requires=["holder"] → 必落 worker 0（写 big_obj + holder_marker）
  - consume task requires=["gpu"] 且依赖 big_obj
    → locality 指向 worker 0（持有者），但 capability 要求 gpu（只在 worker 1）
    → 阶段 A capability 完整匹配优先 → 调度到 worker 1
  - 断言 consume 跑在 worker 1（capability 胜出），证明 locality 不破坏强约束。
"""
from _fly_log import INFO
import os
import shutil
import time

from fly import as_task, open_db, get_config

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


# 写大对象的 task，强制 requires=["holder"]（必落 worker 0）。
@as_task(requires=["holder"])
def write_on_holder(db, payload_key, worker_key, size):
    from test import get_wid
    db.write_object(payload_key, list(range(size)))
    db.write_object(worker_key, get_wid())


# 依赖 big_obj 且 requires=["gpu"]（必落 worker 1，与 locality 偏好冲突）。
@as_task(inputs=lambda db, source_key, result_key: [db.get_full_name(source_key)],
         requires=["gpu"])
def consume_on_gpu(db, source_key, result_key):
    from test import get_wid
    db.read_object(source_key)
    db.write_object(result_key, get_wid())


cleanup()
get_config().set_int("locality_scheduling_enabled", 1)

from fly.runtime import get_agent
master = get_agent()
master.launch_local_workers([
    {"attributes": ["holder"]},  # worker 1: 持有 holder 属性（写 task 落这里）
    {"attributes": ["gpu"]},     # worker 2: 持有 gpu 属性（consume 必落这里）
])
assert master.wait_workers_registered(timeout=60)
assert master.worker_count >= 2, f"Only {master.worker_count}/2 workers connected"

db = open_db(DB_PATH)

# 写 big_obj，强制落 holder worker（worker 1）。
write_on_holder(db, "big_obj", "holder_marker", 1_000_000)
completed = wait_completed(master, 1, timeout=60)
assert len(completed) >= 1, "write_on_holder did not complete"

holder_worker = db.read_object("holder_marker")
INFO(f"[LOCALITY-CAP] big_obj held by worker {holder_worker}")

# consume 要求 gpu（只在 worker 2），依赖 big_obj（locality 指向 worker 1）。
# capability 完整匹配应优先 → 调度到 worker 2。
consume_on_gpu(db, "big_obj", "consume_result")
completed2 = wait_completed(master, 2, timeout=60)
assert len(completed2) >= 2, "consume_on_gpu did not complete"

consume_worker = db.read_object("consume_result")
INFO(f"[LOCALITY-CAP] consume ran on worker {consume_worker}, holder was {holder_worker}")

# consume 必须跑在 gpu worker（worker 2），即使 locality 偏好指向 holder（worker 1）。
# worker_id 从 1 开始，holder=worker 1，gpu=worker 2。
assert int(consume_worker) != int(holder_worker), \
    f"capability priority FAILED: consume ran on holder worker {consume_worker}, " \
    f"should be the OTHER (gpu) worker. holder={holder_worker}."
assert int(consume_worker) == 2, \
    f"consume should run on worker 2 (gpu), got {consume_worker}"

master.stop()
INFO("[PASS] locality yields to capability: consume scheduled to gpu worker despite locality hint")
