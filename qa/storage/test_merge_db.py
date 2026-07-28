"""E2E test: fly.merge_db 主动 API。

验证流程（设计见 docs/db-merge-design.md）：
  1. master launch workers，task 写对象 → freeze
  2. fly.merge_db(path) 派发 __merge_object tasks，跨 worker 拉数据集中到 master host
  3. 校验：产物 data_path 下有 .dat；产物可读全部对象；源 .dat 已删（delete_source=True）

单机测试用 --host 模拟多 host：launch 一个带 --host="host_A" 的 worker（数据源）+
master host 的 local worker（merge target）。host_A worker 写对象后 freeze，
merge_db 把数据拉到 master host 本地 data_path。
"""
from _fly_log import INFO, WARN
import os
import time
import shutil

from e2e_tasks import write_data
from fly import open_db, merge_db, get_config
DB_PATH = os.path.join(get_config().get_str("log_dir"), "merge_db")
from fly.runtime import get_agent


def cleanup():
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)
    merged_data = DB_PATH + ".merged_data"
    if os.path.isdir(merged_data):
        shutil.rmtree(merged_data, ignore_errors=True)


def wait_for(condition, timeout=30.0, interval=0.5):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if condition():
            return True
        time.sleep(interval)
    return False


cleanup()
get_config().set_int("fail_unscheduleable_tasks", 0)

master = get_agent()
# launch 一个带 --host="host_A" 的 worker（模拟另一台机器的数据源）。
master.launch_local_workers([{"host": "host_A"}])
assert wait_for(lambda: master.worker_count >= 1), "host_A worker should connect"

db = open_db(DB_PATH)

# 在 host_A worker 上写若干对象（task 调度到唯一在线 worker host_A）。
write_data(db, "merge/x", 100)
write_data(db, "merge/y", 200)
write_data(db, "merge/z", "hello_merge")
assert wait_for(lambda: len(master.completed_tasks) >= 3), "3 writes should complete"
time.sleep(0.5)  # 等落盘

# freeze（前置条件）。
db.freeze()
assert db.is_frozen(), "db should be frozen"

# 记录源 .dat 文件数（freeze 后稳定）。
def count_dat(path):
    if not os.path.isdir(path):
        return 0
    return sum(1 for f in os.listdir(path) if f.startswith("data_") and f.endswith(".dat"))

src_dat_before = count_dat(DB_PATH)
INFO(f"[MERGE] source .dat count before merge: {src_dat_before}")
assert src_dat_before > 0, "source should have .dat files after writes"

# ── 执行 merge_db ──
merged_db = merge_db(DB_PATH, delete_source=True)
INFO(f"[MERGE] merge_db returned: {merged_db}")

# 校验：产物 data_path 下应有 .dat。
merged_data_path = DB_PATH + ".merged_data"
merged_dat = count_dat(merged_data_path)
INFO(f"[MERGE] merged data_path .dat count: {merged_dat}")
assert merged_dat > 0, "merged data_path should have .dat files"

# 校验：产物应能读到全部对象（merged_db 句柄读，走 master remote_idx/local）。
# 注意：merged_db 的 idx 在共享 base_path（复用源），data 在 merged_data_path。
# master remote_idx 已被 merge task 的 register 更新，指向 master host worker。
assert merged_db.read_object("merge/x") == 100, "merged db should read merge/x"
assert merged_db.read_object("merge/y") == 200, "merged db should read merge/y"
assert merged_db.read_object("merge/z") == "hello_merge", "merged db should read merge/z"
INFO("[MERGE] all objects readable from merged db")

# 校验：源 .dat 应已删除（delete_source=True）。
src_dat_after = count_dat(DB_PATH)
INFO(f"[MERGE] source .dat count after merge: {src_dat_after}")
assert src_dat_after == 0, f"source .dat should be deleted, got {src_dat_after}"

INFO("[PASS] test_merge_db")
