"""E2E test: merge_db 等待 pending/running task 的限制。

验证：调用 merge_db 时若有未完成的 task，merge_db 会先等待它们完成，
不会在 task 运行期间开始 merge（保证数据分布稳定）。
"""
from _fly_log import INFO
import os
import time
import shutil

from test import write_data, slow_write
from fly import open_db, merge_db, get_config
DB_PATH = os.path.join(get_config().get_str("log_dir"), "merge_wait_db")
OTHER_DB_PATH = os.path.join(get_config().get_str("log_dir"), "merge_wait_other")
from fly.runtime import get_agent


def cleanup():
    for p in [DB_PATH, OTHER_DB_PATH, DB_PATH + ".merged_data"]:
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
master.launch_local_workers([{"host": "host_A"}])
assert master.wait_workers_registered(timeout=60)

# 在 db1 写对象并 freeze（merge 的目标）。
db = open_db(DB_PATH)
write_data(db, "obj/a", 1)
assert wait_for(lambda: len(master.completed_tasks) >= 1)
# freeze 前置同步点：写 task 完成落账（替代裸 sleep 缓冲）
from test import wait_until
assert wait_until(lambda: len(master.completed_tasks) >= 1, timeout=10), \
    "write task must complete before freeze"
db.freeze()
assert db.is_frozen()

# 提交一个慢 task（写另一个 db，2 秒后完成），不等待。
other_db = open_db(OTHER_DB_PATH)
slow_write(other_db, "slow/x", 99, 2.0)
INFO("[WAIT-TEST] submitted slow_write task (2s delay), now calling merge_db")

# 记录调用 merge_db 前的时间。
t_before = time.time()

# 立即调 merge_db —— 应等待 slow_write 完成后再 merge。
merged_db = merge_db(DB_PATH, delete_source=False)

elapsed = time.time() - t_before
INFO(f"[WAIT-TEST] merge_db returned after {elapsed:.1f}s")

# 验证：merge_db 至少等了 ~2s（slow_write 的延迟）。
assert elapsed >= 1.5, (
    f"merge_db 应等待 pending task 完成，但仅耗时 {elapsed:.1f}s（预期 >= 1.5s）")

# 验证 slow_write 的对象已写入（task 在 merge 前完成）。
assert other_db.read_object("slow/x") == 99, "slow_write 对象应已写入"

# 验证 merge 产物正常。
assert merged_db.read_object("obj/a") == 1

INFO("[PASS] test_merge_db_waits_for_tasks")
