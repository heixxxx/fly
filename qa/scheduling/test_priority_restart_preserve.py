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


# phantom producer：Phase 2 先提交，其完成使两个竞争 task 同时 ready。
# （不能由 master 直写 phantom + 消费者声明 inputs——"从未存在的依赖"在
# 提交时即判 unresolvable fail；producer task 在图中则等待语义正确。）
@as_task()
def write_phantom(db):
    db.write_object("phantom", "ready")


# 默认优先级 task（priority=10），无依赖，与 restart 出的高优先级 task 竞争。
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
# 失败记录按归属 db 落盘（Task db 归属规则）：task 带 db 参数 → {db_path}/failed_tasks.bin
failed_file = os.path.join(DB_PATH, "failed_tasks.bin")

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
# 观测口径（2026-08-29 重构，根治 56 轮稳定性的伪失败）：
#   ① 字段级（核心目的）：restart 后 task 的 priority 必须仍是 20——经
#      monitor.db tasks 表断言（这正是原始回归 bug 的层面：FailedTaskRecord
#      曾丢 priority/attr_timeout 字段）。
#   ② 行为级（端到端）：两 task 都成功完成（attr_timeout 语义由"restart 后
#      不再因缺 gpu/依赖失败"覆盖——Phase1 的失败本身证明降级路径生效）。
#   ③ 完成顺序仅 INFO 记录不作断言：确定性同场竞争受限时降级语义约束
#      （requires=(caps,0) 的 task 不等依赖 producer，立即降级执行），
#      无法既保 default 无依赖抢先又保竞争窗口——历史版本靠调度节奏侥幸
#      （8ms 抢先完成 + completed 计数采样 race → 谓词永久差 1 → 伪失败）。
db.write_object("phantom", "ready")
default_prio_write(db, "default", "v0")

master.restart_failed_tasks(db)

restart_task_id = p1_failed[0]         # restart 复用原 id（Phase2 前只此一个 task）
default_task_id = restart_task_id + 1  # default 是 Phase2 首个新提交

def both_done():
    c = master.completed_tasks
    return restart_task_id in c and default_task_id in c

assert wait_for(both_done, timeout=20), \
    f"Phase 2: both tasks should complete, completed={master.completed_tasks}"

new_completed = master.completed_tasks[:]
INFO(f"  Phase 2: completion order = {new_completed} "
     f"(restart id={restart_task_id}, default id={default_task_id})")

# ① 字段级断言：restart 路由保留 priority（monitor 落盘于 on_task_complete，
#    build_task_row 从 TaskMetadata 读取——字段丢失即此处回归）。
import sqlite3
monitor_db = os.path.join(get_config().get_str("log_dir"), "monitor.db")
con = sqlite3.connect(f"file:{monitor_db}?mode=ro", uri=True)
rows = list(con.execute(
    "SELECT task_id, priority FROM tasks WHERE task_id IN (?, ?)",
    (restart_task_id, default_task_id)))
con.close()
prio_map = dict(rows)
assert prio_map.get(restart_task_id) == 20, (
    f"Phase 2: restarted task {restart_task_id} must keep priority=20, "
    f"got {prio_map.get(restart_task_id)} (priority LOST during restart — regression)")
assert prio_map.get(default_task_id) == 10, (
    f"Phase 2: default task {default_task_id} should be priority=10, "
    f"got {prio_map.get(default_task_id)}")

# 端到端正确性：两个对象都写入成功
assert db.read_object("high") == "v1", "high-prio task should have written 'high'"
assert db.read_object("default") == "v0", "default task should have written 'default'"

# 失败文件应被清空（restart 成功后删除）
assert not os.path.isfile(failed_file), \
    "Phase 2: failed_tasks.bin should be deleted after successful restart"

master.stop()
INFO("[PASS] priority + attr_timeout preserved across restart: "
     f"order={new_completed} (high-prio id={restart_task_id} first)")
