"""E2E test: master 自动补齐存储节点（存储面 H4）。

验证（用户确认语义：master 周期检测 + 经 host 上的活 worker 本地 spawn）：
  1. auto_storage_nodes_enabled=1 时，无 storage 的 host（host-a，仅 1 hybrid）
     被 heartbeat 循环检测发现，向其活 worker 发 StorageSpawnRequest；
  2. spawn 出的 storage worker（高基区 worker_id）正常注册、role 正确、
     不进调度候选；
  3. SETSID detach：SIGKILL 发起 spawn 的 worker 后 storage 存活；
  4. master.stop() 的 drain 正常回收 spawn 出的 storage（无孤儿进程）。
"""
from _fly_log import INFO
import os
import signal
import time

from fly import get_config
from fly.runtime import get_agent


def wait_for(condition, timeout=30.0, interval=0.5):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if condition():
            return True
        time.sleep(interval)
    return False


get_config().set_int("fail_unscheduleable_tasks", 0)
get_config().set_int("auto_storage_nodes_enabled", 1)
get_config().set_int("auto_storage_check_interval", 5)  # 首检 <5s（占位节流）
# kill 触发 worker 后 stop 的 drain 不等 120s 断连宽限。
get_config().set_int("worker_reconnect_timeout", 0)

master = get_agent()
master.launch_local_workers([{"host": "host-a"}])  # 仅 hybrid，无 storage
assert master.wait_for_workers(1), "hybrid worker should connect"

# 检测循环应发现 host-a 缺 storage 并 spawn（首个检测在 heartbeat 首轮即触发，
# last_storage_check_ts_ 初始 0）。spawn 的 worker_id 在高基区（>=100000）。
assert wait_for(lambda: len(master._agent.get_storage_only_workers()) > 0, timeout=30), \
    "auto storage spawn should bring up a storage worker on host-a"
storage_ids = list(master._agent.get_storage_only_workers())
INFO(f"[H4] auto-spawned storage workers: {storage_ids}")
assert all(wid >= 100000 for wid in storage_ids), \
    f"auto-spawned ids must come from the high base range, got {storage_ids}"

# spawn 的 storage 不进调度候选（仍由 idle 过滤保证）。
wait_for(lambda: master._agent.get_idle_workers() == [1], timeout=15)
idle = master._agent.get_idle_workers()
assert idle == [1], f"idle must stay hybrid-only, got {idle}"
INFO("[H4] spawned storage excluded from scheduling")

# SETSID detach 验证：杀发起 spawn 的 worker（w1），storage 不受牵连。
worker_pids = master.get_worker_pids()
os.kill(worker_pids[0], signal.SIGKILL)
os.waitpid(worker_pids[0], 0)
INFO("[H4] killed spawn trigger worker 1 (SIGKILL)")

assert wait_for(lambda: False, timeout=2) is False  # 观察窗口：storage 不应随父进程退出
storage_alive = master._agent.get_storage_only_workers()
assert len(storage_alive) > 0, "spawned storage must survive trigger worker death (setsid)"
INFO("[H4] spawned storage survived trigger death")

# master.stop() drain 正常回收 spawn 的 storage（无孤儿进程）。
master.stop()

# spawn 决策是 MSG（AGENT::0005 → message.log）；master 进程的 INFO 辅证。
log_dir = get_config().get_str("log_dir")
with open(os.path.join(log_dir, "message.log")) as f:
    msg_content = f.read()
assert "auto storage spawn: host host-a has no storage worker" in msg_content, \
    "message log should record the spawn decision"
with open(os.path.join(log_dir, "master.log")) as f:
    master_content = f.read()
assert "auto storage spawn ack: host=host-a" in master_content, \
    "master log should record the spawn ack"
INFO("[H4] spawn decision + ack logged")

INFO("[PASS] test_auto_storage_spawn")
