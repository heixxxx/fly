"""E2E test: backup 目标选择偏好 storage_only worker（H1）。

三虚拟 host 布局：
  host-a: worker 1 (hybrid)
  host-b: worker 2 (hybrid)
  host-c: worker 3 (storage_only)

写入 task 只能落在两个 hybrid 之一（storage_only 不进调度候选），无论落谁，
backup 候选的 host-disjoint 层内都有 worker 3（唯一 storage 节点，且 host 全新）
——role tie-break 必须选中它。断言副本 holder 含 worker 3，且 master 日志出现
storage 命中标记。
"""
from _fly_log import INFO
import os
import shutil
import time

from test import write_data_backup
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
get_config().set_int("backup_threshold", 1)  # backup every object

master = get_agent()
master.launch_local_workers([
    {"host": "host-a"},
    {"host": "host-b"},
    {"host": "host-c", "role": "storage_only"},
])
assert master.wait_for_workers(3), \
    f"All workers should connect, got {master.worker_count}"

# storage worker（id=3）在线但不进调度候选。
wait_for(lambda: master._agent.get_idle_workers() == [1, 2], timeout=30)
idle = master._agent.get_idle_workers()
assert idle == [1, 2], f"idle candidates must be hybrid-only, got {idle}"

db = open_db(DB_PATH)
write_data_backup(db, "shared/alpha_data", 42)
write_data_backup(db, "shared/beta_data", 99)

assert wait_for(lambda: len(master.completed_tasks) >= 2), "2 write tasks should complete"
# 排空 internal __backup_object task（等 backup 完成的标准手法）。
master.wait_for_all_tasks(expected=None, timeout=10)

from _fly_storage import ex_stg_get_data_service
ds = ex_stg_get_data_service()
for name in ("shared/alpha_data", "shared/beta_data"):
    holders = ds.get_remote_workers(db.get_full_name(name))
    INFO(f"[H1] {name} holders={holders}")
    assert 3 in holders, \
        f"backup replica of {name} must land on storage worker 3, got {holders}"

# 数据可读（回归）。
assert db.read_object("shared/alpha_data") == 42
assert db.read_object("shared/beta_data") == 99

# 先停止再读 master 日志（INFO 缓冲退出才 flush，P3-19 同机制）。
master.stop()

master_log = os.path.join(get_config().get_str("log_dir"), "master.log")
with open(master_log) as f:
    content = f.read()
assert "host-disjoint + storage-only" in content, \
    "master log should record storage-only backup target selection"
INFO("[H1] backup target selection logged storage-only preference")

INFO("[PASS] test_backup_prefers_storage")
