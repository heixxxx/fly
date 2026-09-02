"""worker 异常退出码 e2e：重复 worker id 被拒 → 进程 rc=3；graceful 对照 rc=0。

覆盖（2026-09 覆盖率批次 14 项之 13）：
  worker 进程退出码语义（worker_agent.h：graceful=0 / abnormal=3，
  fly/main.py sys.exit 透传）。本 case 的 fly master 脚本进程亲自 Popen
  worker 子进程（即父进程），从而可以直接 reap 观察退出码：
    - worker_id=7 首次注册 → 正常
    - 再次以同 id 启动第二个 worker → master 拒绝重复 id
      （REGISTRATION_REJECTED，abnormal）→ 进程退出码 3
    - master.stop() 广播 ShutdownMessage → 第一个 worker graceful 退出码 0
"""
import os
import subprocess

from _fly_log import INFO

from fly import get_fly_binary, get_config
from fly.runtime import get_agent
from test import wait_until

master = get_agent()
master.start()

LOG_DIR = get_config().get_str("log_dir")
CONFIG_PATH = os.path.join(LOG_DIR, ".fly_config")
assert os.path.isfile(CONFIG_PATH), \
    f".fly_config must exist after master start: {CONFIG_PATH}"
FLY_BIN = get_fly_binary()


def _spawn_worker(worker_id):
    # stderr 保留到文件：C++ terminate_handler 的 FATAL backtrace 输出在
    # stderr（_exit(77)）——吞掉会丢失崩溃现场。
    err = open(os.path.join(LOG_DIR, f"worker{worker_id}.stderr"), "ab")
    return subprocess.Popen(
        [FLY_BIN, "--worker", "--worker-id", str(worker_id),
         "--log-dir", LOG_DIR, "--config-file", CONFIG_PATH],
        stdout=subprocess.DEVNULL, stderr=err)


# ── 1. 首次注册：worker_id=7 正常上线 ───────────────────────────────
p1 = _spawn_worker(7)
assert wait_until(lambda: 7 in master._agent.get_idle_workers(), timeout=30), \
    "worker 7 must register and go idle"
INFO("[PASS] worker 7 registered (duplicate-registration setup ready)")

# ── 2. 重复 id → REGISTRATION_REJECTED → 进程退出码 3 ───────────────
p2 = _spawn_worker(7)
try:
    rc2 = p2.wait(timeout=30)
except subprocess.TimeoutExpired:
    p2.kill()
    raise AssertionError("duplicate worker must exit promptly")
assert rc2 == 3, f"duplicate-id worker must exit with 3 (abnormal), got {rc2}"
INFO("[PASS] duplicate worker id rejected -> process exit code 3")

# ── 3. graceful 对照：ShutdownMessage 后 worker 退出码 0 ────────────
master.stop()
try:
    rc1 = p1.wait(timeout=30)
except subprocess.TimeoutExpired:
    p1.kill()
    raise AssertionError("worker must exit after master stop broadcast")
assert rc1 == 0, f"graceful worker must exit with 0, got {rc1}"
INFO("[PASS] graceful shutdown -> process exit code 0")

INFO("[PASS] test_worker_exit_code")
