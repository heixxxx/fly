"""E2E test: merge 后用 db 句柄在 task 中读数据（solver build→merge→solve 链不断）。

验证 db_path 统一后的核心业务连续性场景（模拟 solver 真实流程）：
  1. 建 db A，写 matrix 数据（模拟 solver build_matrix）
  2. merge db A（默认 db_path 不变，只 data_path 集中）
  3. 把【merge 返回的新句柄】传给后续 task（模拟 solve kickoff 读 matrix）
  4. task 在 worker 上用新句柄 read_object("matrix") 成功
  5. 也验证用【merge 前的源句柄】传入 task 读（用户可能传任一句柄）

这是 solver build_matrix→merge→solve 链不断的核心保障：
  - merge 后 db_path_ 更新到 target，所有句柄（底层共享同一 C++ 对象）一致
  - task 在 worker 上经 task args 序列化拿到 db 句柄，read 走 remote_idx 命中 merge worker
"""
from _fly_log import INFO
import os
import time
import shutil

import fly
from fly import as_task, open_db, merge_db, get_config, wait_tasks
DB_PATH = os.path.join(get_config().get_str("log_dir"), "merge_solve_db")


def cleanup():
    for p in [DB_PATH, DB_PATH + ".merged_data"]:
        if os.path.isdir(p):
            shutil.rmtree(p, ignore_errors=True)


def wait_for(condition, timeout=30.0, interval=0.3):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if condition():
            return True
        time.sleep(interval)
    return False


# task：在 worker 上用 db 句柄读 matrix（模拟 solve kickoff 的 _solve_kickoff_task）
@as_task()
def _read_matrix_task(db, expected_n):
    """用 db 句柄读 matrix 对象，验证值正确。模拟 solver 的 matrix_db.read_object。"""
    m = db.read_object("matrix")
    assert m["n"] == expected_n, f"matrix n mismatch: {m['n']} != {expected_n}"
    return m["n"]


cleanup()
get_config().set_int("fail_unscheduleable_tasks", 0)

master = fly.get_agent()
master.launch_local_workers([{"host": "host_A"}, {"host": "host_B"}])
assert master.wait_workers_registered(timeout=60), "need 2 workers"

# ── Phase 1: 建 db + 写 matrix（模拟 build_matrix）──
db = open_db(DB_PATH)

@as_task()
def _write_matrix_task(db, n):
    db.write_object("matrix", {"n": n, "N": n * n, "data": list(range(n))})

_write_matrix_task(db, 8)
assert wait_for(lambda: len(master.completed_tasks) >= 1)
# freeze 前置同步点：写 task 完成落账（替代裸 sleep 缓冲）
from test import wait_until
assert wait_until(lambda: len(master.completed_tasks) >= 1, timeout=10), \
    "write task must complete before freeze"
db.freeze()
assert db.is_frozen()

INFO(f"[MERGE-SOLVE] matrix written + frozen, db_path={db.get_db_path()}")

# ── Phase 2: merge（默认 db_path 不变，data_path 集中）──
merged_db = merge_db(DB_PATH, delete_source=True)
INFO("[MERGE-SOLVE] merge done")

# ── Phase 3: 用【merge 返回的新句柄 merged_db】在 worker 上读 matrix ──
# 模拟 solver solve(name, matrix_db=merged_db) 的真实流程：
# 用户 merge 后把返回的 merged_db 传给后续 flow，task 在 worker 上用此句柄读。
_read_matrix_task(merged_db, 8)
wait_tasks(timeout=30)

completed = master.completed_tasks
assert len(completed) >= 2, f"expected 2 completed tasks, got {len(completed)}"
INFO("[MERGE-SOLVE] merged_db handle read in task: PASS")

# ── Phase 4: 用【merge 前的源句柄 db】在 worker 上读 matrix ──
# 验证源句柄（底层共享同一 C++ Database 对象，db_path_ 已更新到 target）也能读。
_read_matrix_task(db, 8)
wait_tasks(timeout=30)

INFO("[MERGE-SOLVE] source db handle read in task: PASS")

# master 句柄直接读（走 remote_idx → merge worker）
m = db.read_object("matrix")
assert m["n"] == 8, f"master read matrix n mismatch: {m['n']}"
INFO("[MERGE-SOLVE] master handle read: PASS")

INFO("[PASS] test_merge_then_solve")
