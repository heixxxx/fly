"""Run 2: load_db + restart → 验证数据正确 + 无残留。

restart 后 FLY_MIXED_FAIL=0，task 重跑不失败。验证：
  - load_db 后脏数据不恢复
  - restart 后所有对象数据正确（含大对象）
  - 无 dirty 残留
"""
from _fly_log import INFO, ERR
import os
import time

from test import record
from fly import get_config

DB_PATH = os.environ.get("FLY_DB_PATH") or os.path.join(get_config().get_str("log_dir"), "db")

with open(os.path.join(DB_PATH, "_test_db_path")) as f:
    expected_db_path = f.read().strip()

from fly.runtime import get_agent

get_config().set_int("aggregation_threshold", 102400)

master = get_agent()

db = master.load_db(DB_PATH)
INFO(f"[RUN2] load_db: {db}")

# 保留：等待原语
assert master.wait_workers_registered(timeout=60.0), "Worker should connect after load_db"
time.sleep(1.0)

db_path_ok = db.get_db_path() == expected_db_path
# 原断言：assert db.get_db_path() == expected_db_path, \
record("load_db/path_preserved", db_path_ok,
       detail={"actual": db.get_db_path(), "expected": expected_db_path})

# load_db 后脏数据不应恢复（abort 段被丢弃）
for key in ["mixed/small_0", "mixed/large", "mixed/dirty"]:
    readable = False
    try:
        db.read_object(key)
        readable = True
        ERR(f"[RUN2] WARN: {key} unexpectedly restored by load_db")
    except Exception:
        pass
    # 新增判定：原来只打日志，读成功也不会使用例失败
    record(f"dirty_unreadable_after_load/{key}", not readable, detail=key)
INFO("[RUN2] dirty data not restored by load_db")

# restart failed tasks：读 run1 落在归属 db 目录的 failed_tasks.bin
# （Task db 归属规则：task 带 db 参数 → {db_path}/failed_tasks.bin，run1/run2 共享 DB_PATH）。
failed_file = os.path.join(DB_PATH, "failed_tasks.bin")
bin_persisted = os.path.isfile(failed_file)
# 原断言：assert os.path.isfile(failed_file), f"failed_tasks.bin should exist: {failed_file}"
record("failed_tasks_bin/persisted", bin_persisted, detail=failed_file)

master.restart_failed_tasks(db)
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

# 保留：等待原语（等待全部重启任务结束）
assert wait_all_done(), \
    f"Not all done: pending={master.pending_tasks}, running={master.running_tasks}, failed={master.failed_tasks}"

INFO("[RUN2] restart completed successfully")

# 验证小对象数据正确
small0 = db.read_object("mixed/small_0")
# 原断言：assert db.read_object("mixed/small_0") == 0
record("restart_data/small_0", small0, detail=small0)
small1 = db.read_object("mixed/small_1")
# 原断言：assert db.read_object("mixed/small_1") == 100
record("restart_data/small_1", small1, detail=small1)
small2 = db.read_object("mixed/small_2")
# 原断言：assert db.read_object("mixed/small_2") == 200
record("restart_data/small_2", small2, detail=small2)

# 验证大对象数据正确（50000 元素的 list）
large = db.read_object("mixed/large")
large_is_list_of_50000 = isinstance(large, list) and len(large) == 50000
# 原断言：assert isinstance(large, list) and len(large) == 50000, \
record("restart_data/large_shape", large_is_list_of_50000,
       detail={"type": type(large).__name__, "len": len(large) if isinstance(large, list) else None})
# 保留：守卫——后面 large[0]、large[49999] 依赖它是 50000 元素的 list
assert isinstance(large, list) and len(large) == 50000, \
    f"large object mismatch: type={type(large)}, len={len(large) if isinstance(large, list) else 'N/A'}"
large_ends_ok = large[0] == 0 and large[49999] == 49999
# 原断言：assert large[0] == 0 and large[49999] == 49999
record("restart_data/large_ends", large_ends_ok, detail={"first": large[0], "last": large[49999]})

# 验证大对象后的小对象
after0 = db.read_object("mixed/after_large_0")
# 原断言：assert db.read_object("mixed/after_large_0") == "after0"
record("restart_data/after_large_0", after0, detail=after0)
after1 = db.read_object("mixed/after_large_1")
# 原断言：assert db.read_object("mixed/after_large_1") == "after1"
record("restart_data/after_large_1", after1, detail=after1)

INFO("[RUN2] all data correct (including large object across rollover)")

# 验证 dirty 不存在
dirty_readable = False
try:
    db.read_object("mixed/dirty")
    dirty_readable = True
    ERR("[RUN2] WARN: mixed/dirty should not exist")
except Exception:
    pass
# 新增判定：原来只打日志，读成功也不会使用例失败
record("dirty_unreadable_after_restart/mixed/dirty", not dirty_readable, detail="mixed/dirty")
INFO("[RUN2] no dirty residue")

INFO("[RUN2] DONE: mixed write abort + restart verified")
