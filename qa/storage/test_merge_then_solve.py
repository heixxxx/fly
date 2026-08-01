"""E2E test: merge 后用源 db 句柄读数据（solver build→merge→solve 链不断）。

验证 db_path 废弃后的核心业务连续性场景：
  1. 建 db A，写 matrix 数据（模拟 solver build_matrix）
  2. merge db A（默认 base_path 不变，只 data_path 集中）
  3. 把【merge 前的 db A 句柄】传给后续 task（模拟 solve kickoff 读 matrix）
  4. task 在 worker 上用旧句柄 read_object("matrix") 成功

这是 solver build_matrix→merge→solve 链不断的核心保障：
  - db_path（源 path）作逻辑锚点不变 → DependencyGraph 依赖 key 稳定
  - merge 后 data_path 集中，旧句柄的 db_paths_ 指向新 data_path → 读字节命中产物
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
assert wait_for(lambda: master.worker_count >= 2), "need 2 workers"

# ── Phase 1: 建 db + 写 matrix（模拟 build_matrix）──
db = open_db(DB_PATH)

@as_task()
def _write_matrix_task(db, n):
    db.write_object("matrix", {"n": n, "N": n * n, "data": list(range(n))})

_write_matrix_task(db, 8)
assert wait_for(lambda: len(master.completed_tasks) >= 1)
time.sleep(0.5)
db.freeze()
assert db.is_frozen()

INFO(f"[MERGE-SOLVE] matrix written + frozen, db_path={db.get_db_path()}")

# ── Phase 2: merge（默认 base_path 不变，data_path 集中）──
merged_db = merge_db(DB_PATH, delete_source=True)
INFO("[MERGE-SOLVE] merge done")

# ── Phase 3: 用【merge 前的 db 句柄】在 worker 上读 matrix（模拟 solve kickoff）──
# 这是核心验证：旧句柄 db 的 db_path（源 path）作锚点不变，DependencyGraph 依赖 key 稳定。
# task 的 inputs 不显式依赖（简化测试），直接在 worker 读。
_read_matrix_task(db, 8)
wait_tasks(timeout=30)

# 校验 task 成功完成（读失败会 TaskFailed → wait_tasks 抛异常）
completed = master.completed_tasks
assert len(completed) >= 2, f"expected 2 completed tasks, got {len(completed)}"

# master 句柄直接读（走 remote_idx → merge worker）
m = db.read_object("matrix")
assert m["n"] == 8, f"master read matrix n mismatch: {m['n']}"
INFO("[MERGE-SOLVE] source db handle reads matrix correctly after merge")

INFO("[PASS] test_merge_then_solve")
