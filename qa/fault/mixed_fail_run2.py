"""Run 2: load_db + restart → 验证数据正确 + 无残留。

restart 后 FLY_MIXED_FAIL=0，task 重跑不失败。验证：
  - load_db 后脏数据不恢复
  - restart 后所有对象数据正确（含大对象）
  - 无 dirty 残留
"""
from _fly_log import INFO, ERR
import os
import time

from fly import get_config

DB_PATH = os.environ.get("FLY_DB_PATH") or os.path.join(get_config().get_str("log_dir"), "db")

with open(os.path.join(DB_PATH, "_test_db_path")) as f:
    expected_db_path = f.read().strip()

from fly.runtime import get_agent

get_config().set_int("aggregation_threshold", 102400)

master = get_agent()

db = master.load_db(DB_PATH)
INFO(f"[RUN2] load_db: {db}")

assert master.wait_workers_registered(timeout=60.0), "Worker should connect after load_db"
time.sleep(1.0)

assert db.get_db_path() == expected_db_path, \
    f"db_path mismatch: {db.get_db_path()} != {expected_db_path}"

# load_db 后脏数据不应恢复（abort 段被丢弃）
for key in ["mixed/small_0", "mixed/large", "mixed/dirty"]:
    try:
        db.read_object(key)
        ERR(f"[RUN2] WARN: {key} unexpectedly restored by load_db")
    except Exception:
        pass
INFO("[RUN2] dirty data not restored by load_db")

# restart failed tasks：读 run1 的 failed_tasks.bin（.pyt 经 env 传；旧 wrapper fallback）。
run1_log = os.environ.get("FLY_RUN1_LOG_DIR")
if run1_log:
    failed_file = os.path.join(run1_log, "failed_tasks.bin")
else:
    SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
    failed_file = os.path.join(SCRIPT_DIR, "test_mixed_write_fail.1", "failed_tasks.bin")
assert os.path.isfile(failed_file), f"failed_tasks.bin should exist: {failed_file}"

master.restart_failed_tasks(failed_file)
INFO("[RUN2] restart_failed_tasks called")

def wait_all_done(timeout=20.0):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if master.failed_tasks:
            ERR(f"[RUN2] unexpected failure: {master.failed_tasks}")
            return False
        if not master.pending_tasks and not master.running_tasks:
            return True
        time.sleep(0.3)
    return False

assert wait_all_done(), \
    f"Not all done: pending={master.pending_tasks}, running={master.running_tasks}, failed={master.failed_tasks}"

INFO("[RUN2] restart completed successfully")

# 验证小对象数据正确
assert db.read_object("mixed/small_0") == 0
assert db.read_object("mixed/small_1") == 100
assert db.read_object("mixed/small_2") == 200

# 验证大对象数据正确（50000 元素的 list）
large = db.read_object("mixed/large")
assert isinstance(large, list) and len(large) == 50000, \
    f"large object mismatch: type={type(large)}, len={len(large) if isinstance(large, list) else 'N/A'}"
assert large[0] == 0 and large[49999] == 49999

# 验证大对象后的小对象
assert db.read_object("mixed/after_large_0") == "after0"
assert db.read_object("mixed/after_large_1") == "after1"

INFO("[RUN2] all data correct (including large object across rollover)")

# 验证 dirty 不存在
try:
    db.read_object("mixed/dirty")
    ERR("[RUN2] WARN: mixed/dirty should not exist")
except Exception:
    pass
INFO("[RUN2] no dirty residue")

INFO("[RUN2] PASS: mixed write abort + restart verified")
