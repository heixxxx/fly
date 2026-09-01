"""断连宽限 e2e：worker 进程闪断（kill -9）→ 宽限期内不判死 → 超时判死 → task 重排队恢复。

2026-08-16 补覆盖（审计建议②）：断连宽限语义（worker_reconnect_timeout>0）此前
QA 零覆盖（现有 case 全部 =0 只测"断连即死"）。网络级闪断模拟（ss -K 杀 TCP）在
WSL2 内核不可用（无 SSOCK_DESTROY），改用进程级闪断等价验证 master 侧语义：

  G2 master 侧：worker 连接断开 → 宽限窗口内不判死（task 保持）→ 宽限超时
  判死（AGENT::0006 提醒）→ task 重排队 → 同 id 重启的 worker 执行完成。

同 worker_id 重启（--worker-id 1）覆盖"重连注册"的 master 分支（旧连接已 EOF，
新注册正常接受）。宽限窗口缩至 10s 提速（默认 120s）。worker 侧重连 loop 的
存活重连路径由单测 ReconnectWithinGracePreservesTask / DisconnectReconnectsAndReports 覆盖。
"""
from _fly_log import INFO
import os
import shutil
import signal
import subprocess
import time

from fly import as_task, open_db, get_config
from fly.runtime import get_agent

LOG_DIR = get_config().get_str("log_dir")
DB_PATH = os.path.join(LOG_DIR, "db")

if os.path.isdir(DB_PATH):
    shutil.rmtree(DB_PATH, ignore_errors=True)

cfg = get_config()
cfg.set_int("fail_unscheduleable_tasks", 0)
cfg.set_int("worker_reconnect_timeout", 10)   # 宽限 10s（默认 120，缩窗提速）

master = get_agent()
master.launch_local_workers([
    {"attributes": ["slowhost"]},   # w1：执行慢 task 的进程（将被闪断）
    {"attributes": ["other"]},      # w2：对照
])
assert master.wait_for_workers(2), "both workers should connect"
mport = master.port


@as_task(requires=["slowhost"])
def slow_task(db, key):
    import time as _t
    _t.sleep(3.0)
    db.write_object(key, "recovered")


master_log = os.path.join(LOG_DIR, "master.log")

# 顺序（避免 kill 与 task 执行竞态）：先 kill w1（宽限开始，无 task 牵连），
# 再提交 task（pin slowhost；此时 w1 不可用，task 挂起 pending），宽限内同 id
# 重启 w1' → 重连注册解除宽限 → task 派发到 w1' 执行完成。
pids = master.get_worker_pids()
assert len(pids) == 2, f"expect 2 live worker pids, got {pids}"
w1_pid = pids[0]
os.kill(w1_pid, signal.SIGKILL)
INFO(f"[grace] killed w1 pid={w1_pid}")

# 等 master 确认知晓断连（EOF 处理完成）再提交 task——否则 assign 可能落在
# 「w1 已死但 master 未感知」窗口，task 派给死进程后随重连保留关联而悬挂。
t0 = time.time()
while time.time() - t0 < 10:
    if os.path.exists(master_log) and "worker 1 disconnected" in open(master_log, errors="replace").read():
        break
    time.sleep(0.1)
else:
    raise AssertionError("master should observe w1 disconnect promptly")

db = open_db(DB_PATH)
slow_task(db, "grace/obj")

# 宽限观察窗（B 类）：断言不变量是「宽限窗口（10s）内不判死」——等的
# 事件不应发生，无前置可 wait_until。sleep(7) 即暴露窗口（7s < 10s 宽限），
# 窗口结束后读 master.log 断言「未判死」仍成立。
time.sleep(7)


# ── 观察窗结束，断言不变量 ──
content = open(master_log, errors="replace").read()
assert "worker 1 declared dead" not in content, \
    "w1 must NOT be declared dead within grace window (7s < 10s)"

# 同 worker_id 重启 worker（重连注册路径：旧连接已 EOF，新注册正常接受，
# 宽限解除、worker 状态恢复可调度）。
fly_bin = shutil.which("fly") or get_agent()._find_fly_binary()
worker1_log = os.path.join(LOG_DIR, "worker1.log")
proc = subprocess.Popen(
    [fly_bin, "--worker", "--worker-id", "1",
     "--master-host", "127.0.0.1", "--master-port", str(mport),
     "--worker-attributes", "slowhost", "--host", "grace-host-1"],
    stdin=subprocess.DEVNULL, stdout=open(worker1_log, "a"), stderr=subprocess.STDOUT)
INFO(f"[grace] respawned worker_id=1 pid={proc.pid}")

# task 派发到重连的 w1' 并完成——宽限期保留 + 重连恢复的行为铁证。
completed = master.wait_for_all_tasks(expected=1, timeout=30)
assert len(completed) >= 1, \
    f"task should be scheduled to respawned w1 and complete, completed={len(completed)}"
assert db.read_object("grace/obj") == "recovered", "task output should be written by respawned w1"

# 重连路径正确语义：宽限被重连消费，全程不判死（超时判死路径见单测
# ReconnectTimeoutExhaustedCleanExit）。
content = open(master_log, errors="replace").read()
assert "worker 1 declared dead" not in content, \
    "reconnect within grace must clear the deadline, no death declaration"

INFO("[PASS] test_disconnect_reconnect_grace: kill -> grace hold -> same-id respawn -> task recovery")

proc.terminate()
master.stop()
