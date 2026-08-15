"""E2E test: 接管与旧 backup 副本共存时的版本选优（存储面 H3 重复数据语义）。

场景：w2(storage) 持有 v1 的 backup 副本，源 w1 随后无 backup 重写为 v2。
kill w1 后接管加载 w1 的 idx（含 v2）。断言读返回 **v2**：
  - entries.back() 选优：接管的 v2 entry 压过旧 backup 的 v1（禁止按
    「对象已存在」跳过加载——否则数据回退到 v1）；
  - restore 的 ObjectCache invalidate：w2 在 backup 时缓存的 v1 字节必须
    失效，否则 cache hit 绕过 back() 选优返回旧值。
"""
from _fly_log import INFO
import os
import shutil
import signal
import time

from test import write_two_versions
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
get_config().set_int("backup_threshold", 1)
get_config().set_int("worker_reconnect_timeout", 0)
get_config().set_int("storage_takeover_enabled", 1)

master = get_agent()
master.launch_local_workers([
    {"host": "host-a"},
    {"host": "host-a", "role": "storage_only"},
])
assert master.wait_for_workers(2), "workers should connect"

db = open_db(DB_PATH)
full_name = db.get_full_name("dup/obj")

# 同 task 双版本：v1 带 backup（副本落 storage w2），v2 同上下文重写（仅更新
# w1 源，w2 的 backup 副本停留在 v1）。同名重写需同 task 上下文（provenance）。
write_two_versions(db, "dup/obj", 1, 2)
assert wait_for(lambda: len(master.completed_tasks) >= 1)
master.wait_for_all_tasks(expected=None, timeout=10)  # 排空 internal backup

from _fly_storage import ex_stg_get_data_service
ds = ex_stg_get_data_service()
holders = ds.get_remote_workers(full_name)
INFO(f"[DUP] after v1+backup holders={holders}")
assert 2 in holders, f"backup replica must be on storage w2, got {holders}"

worker_pids = master.get_worker_pids()
os.kill(worker_pids[0], signal.SIGKILL)
os.waitpid(worker_pids[0], 0)
INFO("[DUP] killed source worker 1 (SIGKILL)")

# 等接管完成：holder 本来就含 w2，需以 master 日志的 IdxLoadAck 为准
# （INFO 自动 flush 64KB/1s——P3-19，轮询日志可行）。
master_log = os.path.join(get_config().get_str("log_dir"), "master.log")


def takeover_done():
    try:
        with open(master_log) as f:
            return "IdxLoadAck: worker_id=2" in f.read()
    except FileNotFoundError:
        return False


assert wait_for(takeover_done, timeout=30), "takeover IdxLoadAck should arrive"
INFO("[DUP] takeover loaded source idx onto storage w2")

# 核心断言：读到 v2（接管的新版本），不是 v1（backup 旧副本/缓存）。
value = db.read_object("dup/obj")
assert value == 2, f"must read latest v2 after takeover, got {value}"
INFO("[DUP] latest version served (back() selection + cache invalidation)")

master.stop()

with open(master_log) as f:
    content = f.read()
assert "storage takeover initiated" in content

INFO("[PASS] test_takeover_duplicate_latest_wins")
