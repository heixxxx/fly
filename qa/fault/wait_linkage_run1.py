"""run1：建库 → worker 写数据 → freeze（供 run2 做 load_db 判死联动测试）。"""
import os
import shutil

from fly import as_task, open_db, get_config
from fly.runtime import get_agent

DB_PATH = os.environ["FLY_DB_PATH"]

if os.path.isdir(DB_PATH):
    shutil.rmtree(DB_PATH, ignore_errors=True)

master = get_agent()
# 独立 host 写数据：_DB_META 记录该 hostname，run2 load_db 时按 host 分组
# 派发 IdxLoad（与本 master host 区分开）。
master.launch_local_workers([{"host": "link-host-1"}])
assert master.wait_for_workers(1), "worker should connect"


@as_task()
def write_objs(db):
    for i in range(20):
        db.write_object(f"obj_{i}", {"payload": "x" * 4096, "idx": i})


db = open_db(DB_PATH)
write_objs(db)
completed = master.wait_for_all_tasks(timeout=60)
assert len(completed) >= 1, f"write task should complete, completed={completed}"

db.freeze()
from test import wait_until
assert wait_until(lambda: db.is_frozen(), timeout=30), \
    "db should be frozen before run1 exits"

master.stop()
