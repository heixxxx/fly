"""run2：load_db 可见性屏障进行中 worker 死亡 → 断连即死（reconnect=0）→
判死联动 settle 置 -1 → load_db 显式 RuntimeError（无限等待不 hang 的铁证）。

确定性构造（park 闸门模式）：预先手动起 worker → 等 online → SIGSTOP 冻结
（TCP 连接保持、IdxLoad 命令堆积不处理 → pending==1 无限稳定）→ 启动 load_db
线程 → 观察 pending==1 → SIGKILL（连接断开）→ master 断连即死 → settle。
消除"ack 先于杀返回"的时序竞争。
"""
import os
import shutil
import signal
import subprocess
import threading
import time

from _fly_log import INFO
from fly import load_db, get_config, expect_workers
from fly.runtime import get_agent

DB_PATH = os.environ["FLY_DB_PATH"]

cfg = get_config()
# 断连即死：杀 worker 后 master 立即判死 → settle_pending_for_dead_worker
# 置 -1（默认 120s 宽限会让 load_db 等满宽限才失败，测试等不起也不必要——
# 判死路径最终同一条）。
cfg.set_int("worker_reconnect_timeout", 0)

master = get_agent()
master.start()
mport = master.port

# 手动起 link-host-1 worker（与 run1 写数据的 host 一致——load_db Phase 2
# 按 _DB_META hostname 匹配到它，不再自行 spawn）。
expect_workers([1])
fly_bin = shutil.which("fly") or master._find_fly_binary()
worker_log = os.path.join(get_config().get_str("log_dir"), "worker1.log")
proc = subprocess.Popen(
    [fly_bin, "--worker", "--worker-id", "1",
     "--master-host", "127.0.0.1", "--master-port", str(mport),
     "--host", "link-host-1"],
    stdin=subprocess.DEVNULL, stdout=open(worker_log, "a"), stderr=subprocess.STDOUT)

t0 = time.time()
while time.time() - t0 < 30:
    hosts = dict(master._agent.get_worker_hostnames())
    if 1 in hosts:
        break
    time.sleep(0.1)
else:
    proc.kill()
    raise AssertionError("manual worker should register")

# park：冻结 worker 进程。连接保持（master 不判死）、IdxLoad 命令堆积——
# pending 屏障到达 1 后无限期稳定，杀的时机完全由测试控制。
os.kill(proc.pid, signal.SIGSTOP)
INFO(f"[linkage] parked worker pid={proc.pid} (SIGSTOP) before load_db")

result = {}


def do_load_db():
    try:
        load_db(DB_PATH)
        result["outcome"] = "ok"
    except Exception as e:  # RuntimeError: idx load failed
        result["outcome"] = str(e)


t = threading.Thread(target=do_load_db)
t.start()

# IdxLoad 已发给冻结 worker：pending 稳定 == 1（无竞争——ack 不会回来）。
t0 = time.time()
while time.time() - t0 < 30:
    if master._agent.idx_load_pending(DB_PATH) == 1:
        break
    time.sleep(0.05)
else:
    proc.kill()
    t.join(30)
    raise AssertionError("idx load should reach pending==1 against parked worker")

# unpark 直接送死：STOP → KILL，连接断开 → master 断连即死 → settle 置 -1 →
# load_db 在无限等待中收到显式失败信号 raise。
os.kill(proc.pid, signal.SIGKILL)
INFO("[linkage] killed parked worker during idx load barrier")

t.join(30)
assert not t.is_alive(), "load_db must fail fast on worker death, not hang forever"
assert result.get("outcome", "hang") != "hang"
assert "idx load failed" in result["outcome"], \
    f"expect explicit RuntimeError about idx load failure, got: {result.get('outcome')}"

master.stop()
INFO("[PASS] wait_linkage_run2: load_db infinite wait settled by worker death "
     f"(explicit error: {result['outcome'][:80]})")
