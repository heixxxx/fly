"""E2E test: cross-path merge 后用 db 句柄在 task 中读数据。

验证 cross-path merge（merge_db_path != 源 path）后，把 merge 返回的句柄
传入后续 task，task 在 worker 上 read_object 成功。

比 test_merge_then_solve 更严格：db_path 从源变到 target（_MIGRATED_TO 重定向），
验证 worker 端经 task args 序列化拿到 db 句柄后，read 走 target 命名空间的
remote_idx/local_idx 命中 merge worker 的数据。
"""
from _fly_log import INFO
import os
import time
import shutil

import fly
from fly import as_task, open_db, merge_db, get_config, wait_tasks
DB_PATH = os.path.join(get_config().get_str("log_dir"), "cross_solve_db")
MERGE_DB_PATH = os.path.join(get_config().get_str("log_dir"), "cross_solve_merged")


def cleanup():
    for p in [DB_PATH, DB_PATH + ".merged_data", MERGE_DB_PATH, MERGE_DB_PATH + ".merged_data"]:
        if os.path.isdir(p):
            shutil.rmtree(p, ignore_errors=True)


def wait_for(condition, timeout=30.0, interval=0.3):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if condition():
            return True
        time.sleep(interval)
    return False


@as_task()
def _read_matrix_task(db, expected_n):
    """用 db 句柄读 matrix，验证值正确。模拟 solver matrix_db.read_object。"""
    m = db.read_object("matrix")
    assert m["n"] == expected_n, f"matrix n mismatch: {m['n']} != {expected_n}"
    return m["n"]


cleanup()
get_config().set_int("fail_unscaleable_tasks", 0)

master = fly.get_agent()
master.launch_local_workers([{"host": "host_A"}, {"host": "host_B"}])
assert master.wait_workers_registered(timeout=60), "need 2 workers"

# ── Phase 1: 建 db + 写 matrix + freeze ──
db = open_db(DB_PATH)

@as_task()
def _write_matrix_task(db, n):
    db.write_object("matrix", {"n": n, "N": n * n, "data": list(range(n))})

_write_matrix_task(db, 8)
assert wait_for(lambda: len(master.completed_tasks) >= 1)
time.sleep(0.5)
db.freeze()
assert db.is_frozen()
INFO("[CROSS-SOLVE] matrix written + frozen")

# ── Phase 2: cross-path merge（merge_db_path != 源 path）──
merged_db = merge_db(DB_PATH, merge_db_path=MERGE_DB_PATH, delete_source=True)
INFO(f"[CROSS-SOLVE] merge done, merged_db path={merged_db.get_db_path()}")

# ── Phase 3: 用 merge 返回的句柄在 worker 上读 matrix ──
# cross-path 场景：db_path 从源变到 target，task args 序列化传 merged_db，
# worker 用 target db_path 查 remote_idx（cleanup 重建到 target 命名空间）命中 merge worker。
_read_matrix_task(merged_db, 8)
wait_tasks(timeout=30)

completed = master.completed_tasks
assert len(completed) >= 2, f"expected 2 completed tasks, got {len(completed)}"
INFO("[CROSS-SOLVE] merged_db handle read in task: PASS")

# master 句柄直接读
m = merged_db.read_object("matrix")
assert m["n"] == 8, f"master read matrix n mismatch: {m['n']}"
INFO("[CROSS-SOLVE] master handle read: PASS")

INFO("[PASS] test_merge_then_solve_cross_path")
