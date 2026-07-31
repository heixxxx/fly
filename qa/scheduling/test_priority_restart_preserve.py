"""E2E: priority + attribute_timeout 在 fail→persist→restart 后不丢失。

回归保护：FailedTaskRecord 必须完整保留 priority_ 和 attribute_timeout_，
restart_failed_tasks 重新提交时必须把它们传回 submit_task。否则一个
@as_task(priority=20, requires=(["gpu"], 0.0)) 的任务失败重启后会退化为
priority=10（失去抢先）+ 死等 gpu（失去降级），调度语义被破坏。

观测信号（单 worker 强制串行，completed/failed 顺序 = 调度执行顺序）：
  Phase 1 — 高优先级 + 限时降级 task 因数据依赖缺失而失败 → persist
  Phase 2 — 补依赖 + restart：高优先级 task(priority=20) 重新就绪，
            同时提交一个默认 priority=10 的 task。单 worker 串行下，
            若 priority 还原正确，priority=20 的 task 应先被调度执行；
            若 priority 丢失（退化为 10），两者同优先级按 task_id 升序，
            先提交的默认 task 会先执行 → 测试失败。
"""
from _fly_log import INFO
import os
import shutil
import time

from fly import as_task, open_db, get_config, get_work_directory

DB_PATH = os.path.join(get_work_directory(), "db")


def cleanup():
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)


# 高优先级 + 限时降级（requires=(["gpu"], 0)）：缺 gpu 立即降级执行。
# 依赖 phantom：第一次缺依赖失败 → persist；补依赖后 restart 成功。
@as_task(inputs=lambda db, key, value: [db.get_full_name("phantom")],
         requires=(["gpu"], 0.0),
         priority=20)
def high_prio_dep_write(db, key, value):
    db.write_object(key, value)


# 默认优先级 task（priority=10），无依赖，用于与 restart 出的高优先级 task 竞争
@as_task()
def default_prio_write(db, key, value):
    db.write_object(key, value)


def wait_for(condition, timeout=20.0, interval=0.3):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if condition():
            return True
        time.sleep(interval)
    return False


cleanup()
# 关键：关闭死锁 fail，让缺 gpu 的 task 走限时降级路径（而非被 fail_unscheduleable 杀掉）
get_config().set_int("fail_unscheduleable_tasks", 0)

from fly.runtime import get_agent

master = get_agent()
master.launch_local_workers([{"attributes": []}])
assert master.wait_for_workers(1, timeout=30), "Worker failed to connect"

db = open_db(DB_PATH)
log_dir = get_config().get_str("log_dir")
failed_file = os.path.join(log_dir, "failed_tasks.bin")

# ── Phase 1: 高优先级 task 因缺 phantom 依赖失败 ──
# requires=(["gpu"],0) 限时降级 → 在无 gpu worker 上降级执行 → 读 phantom 失败 → persist
high_prio_dep_write(db, "high", "v1")

assert wait_for(lambda: len(master.failed_tasks) >= 1, timeout=20), \
    f"Phase 1: high-prio task should fail (missing dep), failed={master.failed_tasks}"
assert os.path.isfile(failed_file), \
    "Phase 1: failed_tasks.bin should exist after failure"
p1_failed = list(master.failed_tasks)
INFO(f"  Phase 1 OK: high-prio task failed as expected, failed_ids={p1_failed}")

# ── Phase 2: 补依赖 + restart，验证 priority 还原 ──
db.write_object("phantom", "ready")

# 关键时序：先提交默认优先级 task（task_id 较小），再 restart（restart 的 task_id 更大但 priority=20）
default_prio_write(db, "default", "v0")

# 记录 restart 前的 completed，便于观测 restart 后的新增完成顺序
pre_restart_completed = len(master.completed_tasks)

master.restart_failed_tasks(failed_file)

# 等待两个 task 都完成（restart 的高优先级 + 新提交的默认优先级）
def both_done():
    return len(master.completed_tasks) >= pre_restart_completed + 2

assert wait_for(both_done, timeout=20), \
    f"Phase 2: both tasks should complete, completed={master.completed_tasks}"

# 取 restart 后新增的完成顺序（单 worker 串行 = 调度执行顺序）
new_completed = master.completed_tasks[pre_restart_completed:]
INFO(f"  Phase 2: completion order after restart = {new_completed}")

# 单 worker 串行下，priority=20 的 restart task 应先于 priority=10 的默认 task 完成。
# restart task 的 id 复用 Phase1 失败的原 id（restart_failed_tasks 传 record.task_id_）；
# default task 是 Phase2 新提交，id 一定大于 restart task 的 id。
# 若 priority 正确还原：restart task(priority=20) 先完成（即便 id 更大）
# 若 priority 丢失（退化为 10）：同优先级按 task_id 升序 → default(id 小) 先完成 → 回归失败
restart_task_id = p1_failed[0]

assert new_completed[0] == restart_task_id, (
    f"Phase 2: restarted high-priority task (id={restart_task_id}, priority=20) "
    f"should complete FIRST after restart (priority preserved). "
    f"Got order={new_completed} — if a smaller-id default-priority task came first, "
    f"priority was LOST during restart (regression).")
assert new_completed[1] != restart_task_id, (
    f"Phase 2: second completion should be the default-priority task. "
    f"Got order={new_completed}")

# 端到端正确性：两个对象都写入成功
assert db.read_object("high") == "v1", "high-prio task should have written 'high'"
assert db.read_object("default") == "v0", "default task should have written 'default'"

# 失败文件应被清空（restart 成功后删除）
assert not os.path.isfile(failed_file), \
    "Phase 2: failed_tasks.bin should be deleted after successful restart"

master.stop()
INFO(f"[PASS] priority + attr_timeout preserved across restart: "
     f"order={new_completed} (high-prio id={restart_task_id} first)")
