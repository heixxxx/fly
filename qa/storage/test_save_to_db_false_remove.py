from _fly_log import INFO
import time
import os
import shutil
import glob



from test import write_temp
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

assert wait_for(lambda: len(master.completed_tasks) >= 3, timeout=30.0)
assert len(master.failed_tasks) == 0

assert db.read_object("d1") == "A" * 500
assert db.read_object("d2") == "B" * 500
assert db.read_object("d3") == "C" * 500

disk_before = get_temp_disk_bytes()

db.remove_object("d1")
db.remove_object("d2")
db.remove_object("d3")

disk_after = get_temp_disk_bytes()

assert disk_after <= disk_before, \
    f"Disk should not grow after remove: before={disk_before}, after={disk_after}"

try:
    db.read_object("d1")
    assert False, "Should fail after remove"
except Exception:
    pass

# ── 幂等删除原语（2026-09-17 §19 批次）：remove_object(missing_ok) ──
# missing_ok=False（默认，严格模式）：对象不存在 → KeyError——清理清单
# 中「必然存在」的键用它，缺失即暴露流程 bug；
# missing_ok=True（清理语义）：不存在时 no-op 不抛（写前清理/框架通用
# 清理，对象可能合法不存在）。返回 None（os.remove 同惯例）。
try:
    db.remove_object("d1")  # 上面已删除——严格模式必须报缺失
    raise AssertionError("strict remove of missing object must raise KeyError")
except KeyError as e:
    assert "d1" in str(e), str(e)
INFO("[PASS] remove_object strict mode: missing object raises KeyError")

# 清理语义：不存在的对象 no-op 不抛；存在的对象照常删除
assert db.remove_object("d1", missing_ok=True) is None
assert db.remove_object("never_existed", missing_ok=True) is None
db.write_object("d_cleanup", "Z" * 100, save_to_db=False)
assert db.remove_object("d_cleanup", missing_ok=True) is None
try:
    db.read_object("d_cleanup")
    raise AssertionError("removed object should not be readable")
except KeyError:
    pass
INFO("[PASS] remove_object missing_ok=True: no-op on missing, removes existing")

INFO(f"[PASS] test_save_to_db_false_remove: disk before={disk_before}, after={disk_after}")
