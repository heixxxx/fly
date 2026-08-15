"""E2E test: 同 host 无 storage_only 时判死保持现状（全灭 fail，不接管）。

对照 case（test_takeover_after_death 的阴性面）：storage_takeover_enabled=1
但 host-a 上没有 storage 节点 → try_storage_takeover 返回 false →
fail_orphan_data_objects 立即执行（单副本 holder 全灭 → mark_data_removed，
读失败）。验证 feature 不改变无 storage 拓扑的既有行为。
"""
from _fly_log import INFO
import os
import shutil
import signal
import time

from test import write_data
from fly import open_db, get_config
DB_PATH = os.path.join(get_config().get_str("log_dir"), "db")
from fly.runtime import get_agent


def wait_for(condition, timeout=30.0, interval=0.5):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if condition():
            return True
        time.sleep(interval)
    return False


if os.path.isdir(DB_PATH):
    shutil.rmtree(DB_PATH, ignore_errors=True)
os.makedirs(DB_PATH, exist_ok=True)

get_config().set_int("fail_unscheduleable_tasks", 0)
get_config().set_int("worker_reconnect_timeout", 0)
get_config().set_int("storage_takeover_enabled", 1)  # feature 开但 host 上无 storage

master = get_agent()
master.launch_local_workers([{"host": "host-a"}])  # 仅 hybrid，无 storage
assert master.wait_for_workers(1), "worker should connect"

db = open_db(DB_PATH)
full_name = db.get_full_name("plain/obj")

write_data(db, "plain/obj", 7)
assert wait_for(lambda: len(master.completed_tasks) >= 1), "write task should complete"

from _fly_storage import ex_stg_get_data_service
ds = ex_stg_get_data_service()
holders = ds.get_remote_workers(full_name)
assert holders == [1], f"single-replica holder must be worker 1, got {holders}"

worker_pids = master.get_worker_pids()
os.kill(worker_pids[0], signal.SIGKILL)
os.waitpid(worker_pids[0], 0)
INFO("[CTRL] killed holder worker 1 (SIGKILL), no storage on host-a")

# 无接管者：holder 列表不应出现新 worker（等待一个短窗口确认稳定）。
assert not wait_for(lambda: len(ds.get_remote_workers(full_name)) > 1, timeout=5), \
    "holder list should NOT grow without a storage node on the host"
INFO("[CTRL] holder unchanged after death")

# 读失败路径由 TIER2 网络期限保证（死 holder connect refused 退避满 30s 后
# 抛出，master 进程无 TIER3 兜底）——30s 阻塞不适合 30s 限的 QA，故以判死
# 侧日志断言代替直接 read：单副本全灭时 fail_orphan_data_objects 应立即把
# 对象 mark_data_removed 并打 AGENT::0003。

master.stop()

master_log = os.path.join(get_config().get_str("log_dir"), "master.log")
with open(master_log) as f:
    content = f.read()
assert "storage takeover initiated" not in content, "takeover must not initiate"
assert "lost all replicas" in content, \
    "fail_orphan should fire immediately without a storage node (AGENT::0003)"
INFO("[CTRL] no takeover, immediate orphan-fail in master.log")

INFO("[PASS] test_no_storage_no_takeover")
