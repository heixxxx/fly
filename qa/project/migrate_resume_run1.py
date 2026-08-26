"""run1：project db（分离 data_path）+ 失败 task（unresolvable dep）。

失败构造确定性：fail_unscheduleable_tasks=1 → task 依赖的 phantom 对象无
产出者 → FAILED 实时 persist → bin 落 {old}/db/。
"""
import os
import time

from _fly_log import INFO
from fly import as_task, get_config, open_project
from fly.runtime import get_agent

PROJ_PATH = os.environ["FLY_PROJ_PATH"]


def wait_for(condition, timeout=30.0, interval=0.3):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if condition():
            return True
        time.sleep(interval)
    return False


@as_task()
def ok_task(db, key, val):
    db.write_object(key, val)


@as_task(inputs=lambda db, key: [db.get_full_name("phantom_dep")])
def dep_task(db, key):
    v = db.read_object("phantom_dep")
    db.write_object(key, v)


get_config().set_int("fail_unscheduleable_tasks", 1)

master = get_agent()
master.launch_local_workers([{}])
assert master.wait_for_workers(1)

proj = open_project(PROJ_PATH)
# 分离 data_path：正式数据落 proj 内独立目录（覆盖 _DB_META data_path 写读链）。
db = proj._create_db("workdb", data_path=os.path.join(PROJ_PATH, "workdb_data"))

ok_task(db, "ok_obj", 42)
dep_task(db, "dep_obj")

assert wait_for(lambda: len(master.completed_tasks) >= 1), \
    f"expected 1 completed, got {len(master.completed_tasks)}"
assert wait_for(lambda: len(master.failed_tasks) >= 1), \
    f"expected 1 failed, got {len(master.failed_tasks)}"

bin_path = os.path.join(PROJ_PATH, "workdb", "failed_tasks.bin")
assert os.path.isfile(bin_path), f"bin should land in owner db dir: {bin_path}"

INFO(f"[PASS] migrate_resume_run1: failure persisted at {bin_path}")
master.stop()
