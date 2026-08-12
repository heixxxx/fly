"""worker remove 后，master 和其他 worker 都能正确写入新数据。

验证 worker remove 路径（task 内 db.remove_object → request_object_remove →
master on_remove_request → on_master_remove）正确清理 provenance：
worker task 写+删后，master 自写和别的 worker task 重写同名对象都应成功（不 mismatch）。

场景1: worker task write_and_remove(obj_m) → master 自写 obj_m（应成功）
场景2: worker task write_and_remove(obj_w) → 别的 worker task write_data(obj_w)（应成功）

关键：wait write_and_remove 的 task completed 后再重写 —— master 处理 on_task_complete
之前已先处理 on_remove_request（同 worker 发送顺序），故 completed 时 provenance 已清。
"""
from _fly_log import INFO
import time
import os
import shutil

from test import write_data, write_and_remove
from fly import open_db, get_config

DB_PATH = os.path.join(get_config().get_str("log_dir"), "db")


def wait_for(condition, timeout=30.0, interval=0.5):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if condition():
            return True
        time.sleep(interval)
    return False


def cleanup():
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)


cleanup()
get_config().set_int("fail_unscheduleable_tasks", 0)

from fly.runtime import get_agent
master = get_agent()
master.launch_local_workers([{}, {}])  # 2 worker，确保"别的 worker"存在
for i in range(40):
    if master.worker_count >= 2:
        break
    time.sleep(0.5)
assert master.worker_count >= 2, f"需要 2 个 worker，got {master.worker_count}"

db = open_db(DB_PATH)

# ── 场景1: worker task 写+删，然后 master 自写同名 ──
write_and_remove(db, "obj_m", "worker_data")
assert wait_for(lambda: len(master.completed_tasks) >= 1, timeout=30.0), \
    f"write_and_remove 应完成，got completed={len(master.completed_tasks)}"
# completed 时 on_remove_request 已处理（早于 on_task_complete）→ provenance 已清。
db.write_object("obj_m", "master_new")
val = db.read_object("obj_m")
assert val == "master_new", f"master 重写后应读 master_new，got {val}"
INFO("[PASS] 场景1: worker remove 后 master 自写成功")

# ── 场景2: worker task 写+删，然后别的 worker task 重写同名 ──
write_and_remove(db, "obj_w", "worker_data2")
assert wait_for(lambda: len(master.completed_tasks) >= 2, timeout=30.0), \
    f"第二次 write_and_remove 应完成，got completed={len(master.completed_tasks)}"
write_data(db, "obj_w", 999)  # worker task 重写（调度到任意 worker 都应成功）
assert wait_for(lambda: len(master.completed_tasks) >= 3, timeout=30.0), \
    f"worker 重写应完成，got completed={len(master.completed_tasks)} failed={len(master.failed_tasks)}"
assert len(master.failed_tasks) == 0, \
    f"worker 重写不应 failed（provenance 应已清）: {[master.get_task_error(t) for t in master.failed_tasks]}"
val2 = db.read_object("obj_w")
assert val2 == 999, f"worker 重写后应读 999，got {val2}"
INFO("[PASS] 场景2: worker remove 后别的 worker 重写成功")

INFO("[PASS] test_remove_then_rewrite: worker remove 后 master + worker 重写都正确")
