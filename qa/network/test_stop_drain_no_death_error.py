"""stop() drain 期 worker 断连零误报（issue 010 回归）.

显式 master.stop() 的 drain 阶段，worker 断连必须归类为正常退出
（handle_worker_exit），不得误报「worker dead + 副本全灭」——
AGENT::0003 / "Task failed (data lost)" 落 master 日志即判失败
（stop 时序噪声污染 QA/运维错误信号）。

场景对齐 issue 010 复现源（test_launch_ssh_workers 同构，launch_local_workers 版）：
启动 2 worker → 数据面写读（对象落 worker，成为 holder）→ 显式 stop()
→ 断言 master 日志零判死/数据全灭标记。
"""
import os
import shutil

from _fly_log import INFO
from test import read_data, write_data
from fly import open_db, wait_tasks, wait_workers_registered, get_agent, get_config

LOG_DIR = get_config().get_str("log_dir")
DB_PATH = os.path.join(LOG_DIR, "db")

if os.path.isdir(DB_PATH):
    shutil.rmtree(DB_PATH, ignore_errors=True)

master = get_agent()
master.start()
master.launch_local_workers([{}, {}])
assert wait_workers_registered(timeout=60) is True, "workers should register"
INFO("[1] 2 workers registered")

db = open_db(DB_PATH)
write_data(db, "stop_key", "stop_value")
read_data(db, "stop_key", deps=[db.get_full_name("stop_key")])
wait_tasks(timeout=30)
assert db.read_object("stop_key") == "stop_value"
INFO("[2] data plane OK (object held by worker)")

# 显式 stop：drain 期若断连被误分类 → handle_worker_death →
# fail_orphan_data_objects → AGENT::0003（副本全灭）+ 逐 task ERR 落 master 日志
master.stop()

# fly.log 是 runqa 进程结束后才合并生成的，脚本内扫描 logger 主日志与
# message 透出日志两处
log_body = ""
for name in ("master.log", "message.log"):
    p = os.path.join(LOG_DIR, name)
    if os.path.isfile(p):
        log_body += open(p, errors="replace").read()
for marker in ("AGENT::0003", "lost all replicas", "Task failed (data lost)"):
    assert marker not in log_body, \
        f"stop() drain 误报判死：master 日志含 {marker!r}（issue 010 回归失败）"
INFO("[3] stop() clean: no death misreport markers in master log")
INFO("[PASS] test_stop_drain_no_death_error")
