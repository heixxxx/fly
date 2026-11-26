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
from test import record
from fly import load_db, get_config, expect_workers
from fly.runtime import get_agent

DB_PATH = os.environ["FLY_DB_PATH"]

# 编排脚本挪入的检查（原先在两个 sub case 之间内联断言，重录模式会真的执行
# 内联语句而失败，见 qa/README.md「编排脚本的写法约定」）：时机不变——run1
# 的工作刚结束、本 sub case 开始工作之前。
# 保留：场景前提——load_db 的屏障场景依赖 run1 已冻结的 db，未冻结时后续等待无意义
assert os.path.isfile(os.path.join(DB_PATH, "_FROZEN")), "db should be frozen after run1"

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
log_dir = cfg.get_str("log_dir")
worker_log = os.path.join(log_dir, "worker1.log")
proc = subprocess.Popen(
    [fly_bin, "--worker", "--worker-id", "1",
     # 自己起 worker 要自己传日志目录（launch_local_workers 会替框架启动的 worker
     # 传）：worker 的日志目录只认命令行上的 --log-dir，不传时落到工作目录下的
     # fly_log/，写进源码树
     "--log-dir", log_dir,
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
# SIGSTOP 投递是异步的（kill 返回 ≠ 进程已停，毫秒窗口内 worker 仍可处理
# 完 IdxLoad 回 ack——100 轮压测 round 4 实锤：park 后 2ms ack 返回，
# load_db 秒完成，断言 pending==1 超时失败）。必须等 /proc 状态确定为 T
# （job-stopped）再继续，park 才是确定性闸门。
os.kill(proc.pid, signal.SIGSTOP)
t0 = time.time()
while True:
    with open(f"/proc/{proc.pid}/stat", "r") as f:
        stat = f.read()
    state = stat.rsplit(") ", 1)[1].split()[0]
    if state == "T":
        break
    if time.time() - t0 > 10:
        proc.kill()
        raise AssertionError(f"worker never reached stopped state (state={state})")
    time.sleep(0.01)
INFO(f"[linkage] parked worker pid={proc.pid} (SIGSTOP confirmed state=T) before load_db")

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
thread_finished = not t.is_alive()
# 原断言：assert not t.is_alive(), "load_db must fail fast on worker death, not hang forever"
record("load_db/thread_finished", thread_finished, detail=result.get("outcome"))
outcome_ready = result.get("outcome", "hang") != "hang"
# 原断言：assert result.get("outcome", "hang") != "hang"
record("load_db/outcome_present", outcome_ready, detail=result.get("outcome"))
# 保留：守卫——后面的 result["outcome"] 下标依赖 outcome 已就绪
assert result.get("outcome", "hang") != "hang"
msg_has_marker = "idx load failed" in result["outcome"]
# 原断言：assert "idx load failed" in result["outcome"], \
record("load_db/idx_load_failed_msg", msg_has_marker, detail=result.get("outcome"))

master.stop()
INFO("[DONE] wait_linkage_run2: load_db infinite wait settled by worker death "
     f"(explicit error: {result['outcome'][:80]})")
