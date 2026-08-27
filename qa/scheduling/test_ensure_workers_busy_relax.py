"""E2E: ensure_workers 两阶段收集——时限内只收空闲候选，到点放宽忙碌候选。

Worker: 2 个空属性 hybrid。
流程：
  1. 两个长 task（sleep 6s）占满全部 worker → 集群无空闲候选；
  2. ensure_workers(timeout=2.0)：阶段一窗口内收不齐（无人空闲），到点放宽
     → 给 BUSY worker 追加属性立即返回。断言耗时 >= timeout（确经阶段一）
     且远小于长 task 剩余时长；
  3. 提交 requires=[新属性] 的 task：提交时全部 worker 忙 → 排队；占位的
     长 task 结束后调度系统自动把它派给被追加属性的 worker 完成。

验证「打上属性的 BUSY worker 不需要等它空闲，后续 task 由调度接管」。
"""
import os
import shutil
import time

from _fly_log import INFO

from fly import as_task, ensure_workers, open_db, get_work_directory
from fly.runtime import get_agent

DB_PATH = os.path.join(get_work_directory(), "db")


def cleanup():
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)


@as_task()
def long_occupy(db, seconds):
    time.sleep(seconds)


@as_task(requires=["qaew:r:x"])
def needs_x(db):
    db.write_object("mark_x", 42)


master = get_agent()
master.launch_local_workers([{}, {}])
assert master.wait_for_workers(2, timeout=30), "workers failed to connect"

cleanup()
db = open_db(DB_PATH)

# 占满集群：两个长 task 同时运行 → 全部 BUSY
long_occupy(db, 6)
long_occupy(db, 6)
deadline = time.time() + 15
while len(master.running_tasks) < 2:
    assert time.time() < deadline, f"long tasks must both start: {master.completed_tasks}"
    time.sleep(0.1)

# 两阶段：timeout=2s 的阶段一必然落空（无人空闲），到点放宽 BUSY 收齐。
# 6s 长 task 尚剩 ~4s——成功返回即证明"不等空闲"。
t0 = time.time()
ok = ensure_workers(["qaew:r:x"], timeout=2.0)
elapsed = time.time() - t0
assert ok is True
assert elapsed >= 1.9, \
    f"阶段一应真实等待 timeout 窗口（而非秒过）: {elapsed:.2f}s"
assert elapsed < 4.5, \
    f"放宽后必须立即收集完成（不得等 worker 空闲）: {elapsed:.2f}s"
INFO(f"[PASS] busy relax in {elapsed:.2f}s")

agent = master._agent
busy = agent.get_busy_workers()
assert any("qaew:r:x" in set(agent.get_worker_capabilities(w)) for w in busy), \
    "属性应已落在 BUSY worker 上（不等空闲）"

# 调度接管：requires task 在全部忙时排队，长 task 一结束即落到带属性者
needs_x(db)


def wait_done():
    """两个长 task + needs_x 全部完成（completed 单调计数，事件驱动）。"""
    deadline = time.time() + 30
    while time.time() < deadline:
        if len(master.completed_tasks) >= 3:
            return True
        assert not master.failed_tasks, \
            f"unexpected failures: {master.get_task_error(master.failed_tasks[0])}"
        time.sleep(0.2)
    return False


assert wait_done(), "relaxed BUSY worker 应在空闲后自动接到 requires task"
assert db.read_object("mark_x") == 42
INFO("[PASS] scheduler picked up the required task after worker freed")

master.stop()
INFO("[PASS] ensure_workers busy relax two-phase")
