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
from test import read_data, write_data, wait_until, record
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
# 保留：等待原语
assert wait_workers_registered(timeout=60)

db = open_db(DB_PATH)
write_data(db, "gk", "gv")
read_data(db, "gk", deps=[db.get_full_name("gk")])
wait_tasks(timeout=30)
gk_value = db.read_object("gk")
# 原断言：assert db.read_object("gk") == "gv"
record("data_plane/gk_value", gk_value, detail=gk_value)
INFO("[A] data plane done; stopping master")

master.stop()

log_body = open(MASTER_LOG).read() if os.path.isfile(MASTER_LOG) else ""
no_dead_noise = "declared dead" not in log_body
# 原断言：assert "declared dead" not in log_body, \
record("graceful_stop/no_declared_dead", no_dead_noise,
       detail=[l for l in log_body.splitlines() if "declared dead" in l])
orphan_quiet = "lost all replicas" not in log_body and "AGENT::0003" not in log_body
# 原断言：assert "lost all replicas" not in log_body and "AGENT::0003" not in log_body, \
record("graceful_stop/no_orphan_fail_error", orphan_quiet,
       detail=[l for l in log_body.splitlines() if "lost all replicas" in l or "AGENT::0003" in l])
exit_confirmed = "exited (master-initiated shutdown confirmed)" in log_body
# 原断言：assert "exited (master-initiated shutdown confirmed)" in log_body, \
record("graceful_stop/exit_confirmation_logged", exit_confirmed, detail=log_body[-2000:])
INFO("[A] master.log clean: no dead/orphan noise, exit confirmation present")

monitor_db_exists = os.path.isfile(MONITOR_DB)
# 原断言：assert os.path.isfile(MONITOR_DB), "monitor.db must exist after run"
record("monitor/db_exists", monitor_db_exists, detail=MONITOR_DB)
# 保留：守卫——后面 sqlite3.connect 与查询都依赖这个文件存在
assert os.path.isfile(MONITOR_DB), "monitor.db must exist after run"
conn = sqlite3.connect(MONITOR_DB)
# worker_id=0 是 master 自注册（register_worker(0)），终态无 EXITED 语义。
rows = conn.execute(
    "SELECT worker_id, last_event FROM workers WHERE worker_id > 0 "
    "ORDER BY worker_id").fetchall()
conn.close()
rows_registered = bool(rows)
all_exited = all(ev == "EXITED" for _, ev in rows)
# 原断言：assert rows, "workers must be registered in monitor.db"
record("monitor/workers_registered", rows_registered, detail=rows)
# 原断言：assert all(ev == "EXITED" for _, ev in rows), \
record("monitor/all_workers_exited", all_exited, detail=rows)
INFO(f"[A] monitor.db final events: {rows}")

# ── 阶段 B：异常退出对偶（kill -9 + 断连即死）——declared dead 应出现 ──
get_config().set_int("worker_reconnect_timeout", 0)
ids = launch_workers([{"attributes": ["doomed"]}])
# 保留：等待原语
assert wait_workers_registered(timeout=60)

pids = master.get_worker_pids()
pids_observable = bool(pids)
# 原断言：assert pids, "worker pid must be observable for the kill scenario"
record("kill/pids_observable", pids_observable, detail=pids)
# 保留：守卫——后面的 pids[0] 依赖至少能观察到一个 worker 进程
assert pids, "worker pid must be observable for the kill scenario"
os.kill(pids[0], signal.SIGKILL)

# 断连即死模式：on_disconnect 立即判死，等 master.log 落盘。
def _declared_dead():
    body = open(MASTER_LOG).read() if os.path.isfile(MASTER_LOG) else ""
    return "declared dead" in body


# 保留：等待原语
assert wait_until(_declared_dead, timeout=15), \
    "kill -9 must classify the worker as dead (abnormal path intact)"
INFO("[B] kill -9 correctly classified as death (negative control)")

master.stop()
INFO("[DONE] test_graceful_exit_no_noise")
