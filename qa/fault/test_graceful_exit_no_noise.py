"""Test: worker 正常退出无噪声 + 异常退出对偶（用户裁定语义）.

master/worker 双侧显式区分退出性质：
- 正常退出（master stop 广播 Shutdown → worker graceful）：master 走
  handle_worker_exit——无 "declared dead" / "lost all replicas"（AGENT::0003）
  噪声，monitor.db 记 EXITED 事件；worker 进程退出码 0。
- 异常退出（kill -9，worker_reconnect_timeout=0 断连即死）：master 走
  handle_worker_death——"declared dead" 应出现（异常路径负向验证）。
"""
import os
import signal
import sqlite3

from _fly_log import INFO
from test import read_data, write_data, wait_until
from fly import (open_db, wait_tasks, launch_workers,
                 wait_workers_registered, get_agent, get_config)

LOG_DIR = get_config().get_str("log_dir")
DB_PATH = os.path.join(LOG_DIR, "db")
MASTER_LOG = os.path.join(LOG_DIR, "master.log")
MONITOR_DB = os.path.join(LOG_DIR, "monitor.db")

import shutil
for p in (DB_PATH,):
    if os.path.isdir(p):
        shutil.rmtree(p, ignore_errors=True)

master = get_agent()

# ── 阶段 A：正常退出（stop 广播 Shutdown）——零噪声 + EXITED 事件 ──
launch_workers([{}, {}])
assert wait_workers_registered(timeout=60)

db = open_db(DB_PATH)
write_data(db, "gk", "gv")
read_data(db, "gk", deps=[db.get_full_name("gk")])
wait_tasks(timeout=30)
assert db.read_object("gk") == "gv"
INFO("[A] data plane done; stopping master")

master.stop()

log_body = open(MASTER_LOG).read() if os.path.isfile(MASTER_LOG) else ""
assert "declared dead" not in log_body, \
    "graceful stop must not classify workers as dead:\n" + \
    "\n".join(l for l in log_body.splitlines() if "declared dead" in l)
assert "lost all replicas" not in log_body and "AGENT::0003" not in log_body, \
    "orphan-fail ERROR must not fire on graceful stop (data is drained to disk)"
assert "exited (master-initiated shutdown confirmed)" in log_body, \
    "normal-exit path must log the EXITED confirmation"
INFO("[A] master.log clean: no dead/orphan noise, exit confirmation present")

assert os.path.isfile(MONITOR_DB), "monitor.db must exist after run"
conn = sqlite3.connect(MONITOR_DB)
# worker_id=0 是 master 自注册（register_worker(0)），终态无 EXITED 语义。
rows = conn.execute(
    "SELECT worker_id, last_event FROM workers WHERE worker_id > 0 "
    "ORDER BY worker_id").fetchall()
conn.close()
assert rows, "workers must be registered in monitor.db"
assert all(ev == "EXITED" for _, ev in rows), \
    f"all workers must end EXITED, got {rows}"
INFO(f"[A] monitor.db final events: {rows}")

# ── 阶段 B：异常退出对偶（kill -9 + 断连即死）——declared dead 应出现 ──
get_config().set_int("worker_reconnect_timeout", 0)
ids = launch_workers([{"attributes": ["doomed"]}])
assert wait_workers_registered(timeout=60)

pids = master.get_worker_pids()
assert pids, "worker pid must be observable for the kill scenario"
os.kill(pids[0], signal.SIGKILL)

# 断连即死模式：on_disconnect 立即判死，等 master.log 落盘。
def _declared_dead():
    body = open(MASTER_LOG).read() if os.path.isfile(MASTER_LOG) else ""
    return "declared dead" in body


assert wait_until(_declared_dead, timeout=15), \
    "kill -9 must classify the worker as dead (abnormal path intact)"
INFO("[B] kill -9 correctly classified as death (negative control)")

master.stop()
INFO("[PASS] test_graceful_exit_no_noise")
