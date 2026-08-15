"""E2E test: 判死后同 host storage_only 接管读服务（存储面 H3 主链路）。

验证（用户确认语义：master 显式驱动、类似重启恢复流程、只读加载）：
  1. 单副本数据 holder（hybrid w1@host-a）被 SIGKILL → 判死
     （worker_reconnect_timeout=0 断连即死）；
  2. master 自动发起接管：同 host storage_only w2 只读加载死 worker 的 idx；
  3. 接管完成前读请求靠 TIER2 重试窗口自愈，完成后：
     - master 直接 read_object 成功（TIER2 打 storage 副本，M2 排序联动）；
     - 新上线 hybrid w3 的依赖 task 正常完成（延迟 fail + mark_data_ready 恢复）。
"""
from _fly_log import INFO
import os
import shutil
import signal
import time

from test import write_data, read_data
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
get_config().set_int("worker_reconnect_timeout", 0)  # 断连即死（判死即时触发接管）
get_config().set_int("storage_takeover_enabled", 1)

master = get_agent()
# 第一批：host-a 上一个 hybrid（数据写入者）+ 一个 storage_only（接管者）。
master.launch_local_workers([
    {"host": "host-a"},
    {"host": "host-a", "role": "storage_only"},
])
assert master.wait_for_workers(2), "first batch should connect"

db = open_db(DB_PATH)
full_name = db.get_full_name("takeover/obj")

# 单副本数据（无 backup）：唯一 hybrid w1 执行写入 → holder == [1]。
write_data(db, "takeover/obj", 42)
assert wait_for(lambda: len(master.completed_tasks) >= 1), "write task should complete"

from _fly_storage import ex_stg_get_data_service
ds = ex_stg_get_data_service()
holders = ds.get_remote_workers(full_name)
INFO(f"[H3] pre-kill holders={holders}")
assert holders == [1], f"single-replica holder must be worker 1, got {holders}"

# SIGKILL holder → 断连即死 → handle_worker_death → try_storage_takeover。
worker_pids = master.get_worker_pids()
os.kill(worker_pids[0], signal.SIGKILL)
os.waitpid(worker_pids[0], 0)
INFO("[H3] killed holder worker 1 (SIGKILL)")

# 接管完成标志：holder 列表出现 storage worker 2（IdxLoad ack 后 update_remote_idx）。
assert wait_for(lambda: 2 in ds.get_remote_workers(full_name), timeout=30), \
    "storage worker 2 should take over as holder after death"
INFO("[H3] storage takeover completed (holder now includes worker 2)")

# 第二批：host-b hybrid 上线，执行依赖该对象的读 task。
# 注意用注册语义等待（wait_workers_registered）而非连接数（wait_for_workers）：
# w1 已死，连接数含它永远到不了 3。
master.launch_local_workers([{"host": "host-b"}])
assert master.wait_workers_registered(timeout=60), "second batch should register"
INFO(f"[H3-DBG] worker_count={master.worker_count}, "
     f"idle={list(master._agent.get_idle_workers())}")
wait_for(lambda: master._agent.get_idle_workers() == [3], timeout=30)

# 读 task：key 与 deps 同为接管对象（test_stress_readwrite 同款用法）。
read_data(db, "takeover/obj", [full_name])
assert wait_for(lambda: len(master.completed_tasks) >= 2, timeout=60), \
    "dependent read task should complete after takeover (not failed)"
assert not master.failed_tasks, f"no task should fail, failed={master.failed_tasks}"
INFO("[H3] dependent task completed via taken-over replica")

# master 直接读（TIER2 打 storage 副本，M2 死 holder 排尾联动）。
assert db.read_object("takeover/obj") == 42
INFO("[H3] direct read served by storage worker")

# 先停止再读 master 日志（INFO 缓冲退出才 flush，P3-19 同机制）。
master.stop()

master_log = os.path.join(get_config().get_str("log_dir"), "master.log")
with open(master_log) as f:
    content = f.read()
assert "storage takeover initiated" in content, "master log should record takeover"
INFO("[H3] takeover logged in master.log")

INFO("[PASS] test_takeover_after_death")
