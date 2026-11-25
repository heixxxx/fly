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
from test import record

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
    # 保留：等待循环体内
    assert time.time() < deadline, f"long tasks must both start: {master.completed_tasks}"
    time.sleep(0.1)

# 两阶段：timeout=2s 的阶段一必然落空（无人空闲），到点放宽 BUSY 收齐。
# 6s 长 task 尚剩 ~4s——成功返回即证明"不等空闲"。
t0 = time.time()
ok = ensure_workers(["qaew:r:x"], timeout=2.0)
elapsed = time.time() - t0
# 原断言：assert ok is True
record("busy_relax/ok", ok is True, detail=ok)
# 原断言：assert elapsed >= 1.9, \
record("busy_relax/phase_one_waited", elapsed >= 1.9, detail=elapsed)
# 原断言：assert elapsed < 4.5, \
record("busy_relax/relaxed_immediately", elapsed < 4.5, detail=elapsed)
INFO(f"[DONE] busy relax in {elapsed:.2f}s")

agent = master._agent
busy = agent.get_busy_workers()
attr_on_busy = any("qaew:r:x" in set(agent.get_worker_capabilities(w)) for w in busy)
# 原断言：assert any("qaew:r:x" in set(agent.get_worker_capabilities(w)) for w in busy), \
record("busy_relax/attr_on_busy_worker", attr_on_busy, detail=sorted(busy))

# 调度接管：requires task 在全部忙时排队，长 task 一结束即落到带属性者
needs_x(db)


def wait_done():
    """两个长 task + needs_x 全部完成（completed 单调计数，事件驱动）。"""
    deadline = time.time() + 30
    while time.time() < deadline:
        if len(master.completed_tasks) >= 3:
            return True
        # 保留：等待循环体内
        assert not master.failed_tasks, \
            f"unexpected failures: {master.get_task_error(master.failed_tasks[0])}"
        time.sleep(0.2)
    return False


assert wait_done(), "relaxed BUSY worker 应在空闲后自动接到 requires task"
mark_x_val = db.read_object("mark_x")
# 原断言：assert db.read_object("mark_x") == 42
record("busy_relax/mark_x_value", mark_x_val, detail=mark_x_val)
INFO("[DONE] scheduler picked up the required task after worker freed")

master.stop()
INFO("[DONE] ensure_workers busy relax two-phase")
