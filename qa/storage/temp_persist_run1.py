"""run1：worker 写 temp 对象（save_to_db=False，落盘新语义）+ 正式对象，不 freeze。"""
import os
import shutil

from fly import as_task, open_db
from fly.runtime import get_agent

DB_PATH = os.environ["FLY_DB_PATH"]

if os.path.isdir(DB_PATH):
    shutil.rmtree(DB_PATH, ignore_errors=True)

master = get_agent()
master.launch_local_workers([{"host": "temp-host-1"}])
assert master.wait_for_workers(1)


@as_task()
def write_temp_and_final(db):
    # temp（save_to_db=False → temp_data_*.dat + .temp.idx 落盘）
    db.write_object("iters/state_0", {"step": 0, "arr": list(range(100))},
                    save_to_db=False)
    db.write_object("iters/state_1", {"step": 1, "arr": list(range(100, 200))},
                    save_to_db=False)
    # 正式对象对照
    db.write_object("final/result", {"ok": True})


db = open_db(DB_PATH)
write_temp_and_final(db)
completed = master.wait_for_all_tasks(timeout=60)
assert len(completed) >= 1, f"task should complete, got {completed}"

# 不 freeze（freeze 会清理 temp）——run2 验证跨进程恢复。
master.stop()
