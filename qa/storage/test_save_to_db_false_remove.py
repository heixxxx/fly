from _fly_log import INFO
import time
import os
import shutil
import glob



from test import write_temp, record, wait_tasks
from fly import open_db, get_config
DB_PATH = os.path.join(get_config().get_str("log_dir"), "db")


def cleanup():
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)


def wait_for(condition, timeout=60.0, interval=0.5):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if condition():
            return True
        time.sleep(interval)
    return False


def get_temp_disk_bytes():
    total = 0
    for d in glob.glob("/tmp/fly_temp_*"):
        if os.path.isdir(d):
            for root, dirs, files in os.walk(d):
                for f in files:
                    total += os.path.getsize(os.path.join(root, f))
    return total


cleanup()
get_config().set_int("fail_unscheduleable_tasks", 0)

from fly.runtime import get_agent
master = get_agent()

master.launch_local_workers([{}])
assert master.wait_for_workers(1)

db = open_db(DB_PATH)

write_temp(db, "d1", "A" * 500)
write_temp(db, "d2", "B" * 500)
write_temp(db, "d3", "C" * 500)

# 原断言：assert wait_for(lambda: len(master.completed_tasks) >= 3, timeout=30.0)
# 原断言：assert len(master.failed_tasks) == 0
assert wait_tasks(master, 3, "writes/no_failed_tasks", timeout=30.0)

# 原断言：assert db.read_object("d1") == "A" * 500
record("read_d1", db.read_object("d1"))
# 原断言：assert db.read_object("d2") == "B" * 500
record("read_d2", db.read_object("d2"))
# 原断言：assert db.read_object("d3") == "C" * 500
record("read_d3", db.read_object("d3"))

disk_before = get_temp_disk_bytes()

db.remove_object("d1")
db.remove_object("d2")
db.remove_object("d3")

disk_after = get_temp_disk_bytes()

# 原断言：assert disk_after <= disk_before, \
record("disk_not_grown_after_remove", disk_after <= disk_before,
       detail={"before": disk_before, "after": disk_after})

# 旧写法失效：assert False 抛出的 AssertionError 是 Exception 的子类，会被下方
# 同一个 except Exception 一并吞掉，读操作没按预期失败时用例照样通过。
# 2026-11-25 修复的 raised 标志写法，转成记录保持等价。
removed_raised = None
try:
    db.read_object("d1")
except Exception as e:
    removed_raised = {"type": type(e).__name__, "msg": str(e)}
# 原断言：assert raised, "Should fail after remove"
record("removed_d1_unreadable", removed_raised is not None, detail=removed_raised)
record("removed_d1_error_type", removed_raised["type"] if removed_raised else None)

# ── 幂等删除原语（2026-09-17 §19 批次）：remove_object(missing_ok) ──
# missing_ok=False（默认，严格模式）：对象不存在 → KeyError——清理清单
# 中「必然存在」的键用它，缺失即暴露流程 bug；
# missing_ok=True（清理语义）：不存在时 no-op 不抛（写前清理/框架通用
# 清理，对象可能合法不存在）。返回 None（os.remove 同惯例）。
strict_missing_raised = None
try:
    db.remove_object("d1")  # 上面已删除——严格模式必须报缺失
except KeyError as e:
    strict_missing_raised = {"type": type(e).__name__, "msg": str(e)}
# 原断言：assert "d1" in str(e), str(e)
record("strict_remove_missing_raises", strict_missing_raised is not None,
       detail=strict_missing_raised)
record("strict_remove_missing_key_in_message",
       "d1" in strict_missing_raised["msg"] if strict_missing_raised else False,
       detail=strict_missing_raised["msg"] if strict_missing_raised else None)
INFO("[DONE] remove_object strict mode: missing object raises KeyError")

# 清理语义：不存在的对象 no-op 不抛；存在的对象照常删除
# 原断言：assert db.remove_object("d1", missing_ok=True) is None
rm_d1 = db.remove_object("d1", missing_ok=True)
# 原断言：assert db.remove_object("never_existed", missing_ok=True) is None
rm_never = db.remove_object("never_existed", missing_ok=True)
record("missing_ok_returns_none", {"d1": rm_d1, "never_existed": rm_never})
db.write_object("d_cleanup", "Z" * 100, save_to_db=False)
# 原断言：assert db.remove_object("d_cleanup", missing_ok=True) is None
rm_cleanup = db.remove_object("d_cleanup", missing_ok=True)
record("missing_ok_cleanup_returns_none", rm_cleanup)
cleanup_raised = None
try:
    db.read_object("d_cleanup")
except Exception as e:
    cleanup_raised = {"type": type(e).__name__, "msg": str(e)}
# 原断言（try 块内 raise AssertionError——机械核对不追踪 raise 语句，
# 转成记录保持等价语义）：
#   raise AssertionError("removed object should not be readable")
record("removed_d_cleanup_unreadable", cleanup_raised is not None, detail=cleanup_raised)
record("removed_d_cleanup_error_type", cleanup_raised["type"] if cleanup_raised else None)
INFO("[DONE] remove_object missing_ok=True: no-op on missing, removes existing")

INFO(f"[DONE] test_save_to_db_false_remove: disk before={disk_before}, after={disk_after}")
