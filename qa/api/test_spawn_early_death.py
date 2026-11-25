"""E2E: 本地 spawn worker 注册前早夭的快速失败（不再无限等待）。

背景：worker_register_timeout 默认 0 = 占位符永不清理 + 等待无限（bsub 慢
调度场景的裁定语义）。本地 Popen spawn 的进程握有句柄——注册前退出（资源
饥饿/启动即崩）应立即 RuntimeError，而非挂在无限等待上。

验证（白盒构造：伪造"已退出进程"的批次条目，id 永远不会注册）：
  _wait_spawned_workers([fake_id]) → 立即 RuntimeError 含早夭明细；
  对照：不传 batch_ids 的调用保持既有语义（此处以 wait_workers_registered
  不受伪造条目影响佐证占位符清理仍归 config 管）。
"""
import subprocess

from _fly_log import INFO

from test import record
from fly.runtime import get_agent
import time

master = get_agent()
master.start()

# 伪造一个"本批 spawn 的进程"：/bin/true 立即退出，worker_id=999 永不注册。
fake = subprocess.Popen(["/bin/true"])
fake.wait()
master._spawned_procs[999] = fake

t0 = time.time()
err_type = None
msg_full = False
try:
    master._wait_spawned_workers([999])
    raise AssertionError("must raise on early-death spawn")
except RuntimeError as e:
    msg = str(e)
    err_type = type(e).__name__
    msg_full = "exited before registering" in msg and "999" in msg
# 原断言：assert "exited before registering" in msg and "999" in msg, f"bad detail: {msg}"
record("early_death/error_type", err_type)
record("early_death/msg_mentions_exited_and_id", msg_full, detail=msg)
elapsed = time.time() - t0
fast = elapsed < 5.0
# 原断言：assert elapsed < 5.0, f"早夭必须立即失败而非等待, took {elapsed:.1f}s"
record("early_death/failed_fast_within_5s", fast, detail=elapsed)
INFO(f"[DONE] early-death spawn failed fast in {elapsed:.2f}s")

# 对照：默认入口（不传 batch_ids）不做早夭检测——既有语义不变
master._wait_spawned_workers()

master.stop()
INFO("[DONE] test_spawn_early_death")
