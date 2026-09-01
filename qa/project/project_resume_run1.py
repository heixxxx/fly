"""run1：project 场景提交 task 批，部分完成后 SIGTERM 优雅中断——
FAILED/PENDING/RUNNING task 持久化进归属 db 目录（{db_path}/failed_tasks.bin）。
"""
import os
import signal
import time

from fly import as_task, wait_tasks
from fly import open_project
from fly.runtime import get_agent

PROJ_PATH = os.environ["FLY_PROJ_PATH"]

master = get_agent()
master.launch_local_workers([{}, {}])
assert master.wait_for_workers(2)

proj = open_project(PROJ_PATH)
db = proj._create_db("workdb")


@as_task()
def work(db, i):
    import time as _t
    _t.sleep(1.5)
    db.write_object(f"obj_{i}", i * 10)


for i in range(6):
    work(db, i)

# 等 2 个完成（形成"部分已完成 + 部分 RUNNING/PENDING"的断点现场），
# 其余 task 在途时 SIGTERM → 优雅退出：drain 等 RUNNING 完成 + 尾部
# persist PENDING 落盘 → failed_tasks.bin 进 project 目录。
from test import wait_until
assert wait_until(lambda: len(master._agent.get_completed_tasks()) >= 2, timeout=30), \
    "expected at least 2 completed tasks before SIGTERM"

# SIGTERM → handler raise SystemExit(0) → _run_master 收尾 agent.stop()（优雅
# drain + persist_pending_tasks）。剩余在途 task 的 spec 落 project bin。
os.kill(os.getpid(), signal.SIGTERM)
time.sleep(60)  # 不可达：SystemExit 从 handler 冒泡终止脚本
raise AssertionError("unreachable: SIGTERM should terminate via SystemExit")
