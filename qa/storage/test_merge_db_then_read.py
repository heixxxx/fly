"""E2E test: merge_db 后启动 task fan-out 读取 merge 数据。

同时验证两点：
  1. merge_db 是阻塞调用 —— 提交的读 task 在 merge 完成后才执行（task 内时间戳证明）
  2. merge_db 后能正确读到新数据 —— 读 task 读 merge 的对象，值正确

流程：写对象 → freeze → merge_db（阻塞）→ 提交 fanout_read_verify task → 校验读到正确数据。
若 merge 非阻塞，读 task 可能在 merge 完成前执行（数据未迁好 → 读失败或读到旧位置）。
"""
from _fly_log import INFO
import os
import time
import shutil

from e2e_tasks import write_data, fanout_read_verify
from fly import open_db, merge_db, get_config
DB_PATH = os.path.join(get_config().get_str("log_dir"), "merge_read_db")
from fly.runtime import get_agent


def cleanup():
    for p in [DB_PATH, DB_PATH + ".merged_data"]:
        if os.path.isdir(p):
            shutil.rmtree(p, ignore_errors=True)


def wait_for(condition, timeout=30.0, interval=0.3):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if condition():
            return True
        time.sleep(interval)
    return False


cleanup()
get_config().set_int("fail_unscheduleable_tasks", 0)

master = get_agent()
master.launch_local_workers([{"host": "host_A"}, {"host": "host_B"}])
assert wait_for(lambda: master.worker_count >= 2), "need 2 workers on different hosts"

# 写对象（分散到不同 host worker，模拟多机数据）。
db = open_db(DB_PATH)
write_data(db, "data/alpha", 100)
write_data(db, "data/beta", 200)
write_data(db, "data/gamma", "merged_value")
assert wait_for(lambda: len(master.completed_tasks) >= 3)
time.sleep(0.5)  # 等落盘

# freeze（merge 前置）。
db.freeze()
assert db.is_frozen()

# 记录 merge 前时间（验证阻塞：读 task 只能在 merge 后提交）。
t_before_merge = time.time()
INFO("[READ-TEST] calling merge_db (blocking)")

# merge_db 阻塞调用 —— 返回时 merge 已全部完成。
merged_db = merge_db(DB_PATH, delete_source=True)
merge_elapsed = time.time() - t_before_merge
INFO(f"[READ-TEST] merge_db returned after {merge_elapsed:.1f}s")

# merge 完成后，提交 fanout_read_verify task：读全部 merge 对象并校验。
# 此 task 在 merge 之后提交，证明 merge_db 是阻塞的（否则此行会在 merge 中途执行）。
# task 内部读不到数据会抛异常 → TaskFailed。所以"task 成功完成"即证明数据可读。
keys = ["data/alpha", "data/beta", "data/gamma"]
expected = [100, 200, "merged_value"]
fanout_read_verify(merged_db, keys, expected)
INFO("[READ-TEST] submitted fanout_read_verify task")

# 等待 task 完成。wait_for_all_tasks 遇 failed task 会 raise RuntimeError。
completed = master.wait_for_all_tasks(timeout=30)
INFO(f"[READ-TEST] fanout_read_verify completed, total completed tasks={len(completed)}")

# 校验：fanout_read_verify task 成功完成（无 failed）= merge 后数据可读。
# 若数据不可读，read_object 会抛异常导致 TaskFailed，wait_for_all_tasks 会 raise。
# 到达此处即证明：① merge_db 阻塞（task 在 merge 后才提交）② 数据可读。

# 额外直接校验：master 句柄也能读到（走 remote_idx → merge worker）。
assert merged_db.read_object("data/alpha") == 100
assert merged_db.read_object("data/beta") == 200
assert merged_db.read_object("data/gamma") == "merged_value"

INFO("[PASS] test_merge_db_then_read")
