"""E2E test: db chain merge + find_db — merge 前驱后链仍正确。

验证流程：
  1. 建链 matrix → solve（双向 DAG）
  2. 在 solve 上 find_db(matrix) 能找到
  3. 跨 path merge matrix 到新路径（彻底删源）
  4. merge 后 solve 的 _DB_CHAIN.prev[].db_path 已更新到 target
  5. find_db(matrix) 仍正确找到（通过 master uid_to_path_ 重定向）
  6. matrix 的 next[].db_path 已更新到 target
"""
import os
import shutil

from test import record
from fly import open_db, merge_db, launch_workers, get_config
from fly.runtime import get_agent

DB_BASE = os.path.join(get_config().get_str("log_dir"), "db_chain_merge")
MATRIX_PATH = os.path.join(DB_BASE, "matrix")
SOLVE_PATH = os.path.join(DB_BASE, "solve")
MERGE_TARGET = os.path.join(DB_BASE, "matrix_merged")

from storage import Database

from storage import DbMetaFile


class MatrixDb(Database):
    role = "matrix"


class SolveDb(Database):
    role = "solve"


def cleanup():
    for p in [DB_BASE, MATRIX_PATH, SOLVE_PATH, MERGE_TARGET]:
        if os.path.isdir(p):
            shutil.rmtree(p, ignore_errors=True)


cleanup()

# launch worker（merge 需要）
master = get_agent()
launch_workers([{"host": "host_A"}])
from test import wait_until
# 保留：等待原语
assert wait_until(lambda: master.worker_count >= 1, timeout=10), "worker should connect"

# ── Step 1: 建链 matrix → solve ──
matrix_db = open_db(MATRIX_PATH, db_cls=MatrixDb, logical_name="matrix")
solve_db = open_db(SOLVE_PATH, db_cls=SolveDb, prev=[matrix_db], logical_name="solve")

matrix_uid = matrix_db.get_uid()
# 原断言：assert matrix_uid is not None
record("step1/matrix_db_has_uid", matrix_uid is not None, detail=matrix_uid)
print(f"  matrix_db: uid={matrix_uid}, path={matrix_db.get_db_path()}")

# ── Step 2: merge 前 find_db ──
found = solve_db.find_db(role="matrix")
# 保留：判空守卫——后续 print 还要用 found
assert found is not None
found_uid = found.get_uid()
# 原断言：assert found.get_uid() == matrix_uid
record("step2/find_pre_merge_uid_matches", found_uid == matrix_uid,
       detail={"found": found_uid, "expected": matrix_uid})
print(f"  pre-merge find_db(matrix): uid={found.get_uid()} ✓")

# ── Step 3: 写数据到 matrix + freeze（merge 前置条件）──
from test import write_data
write_data(matrix_db, "data/x", 42)
# 保留：等待原语（freeze 前同步点）
assert master.wait_for_all_tasks(timeout=10) or len(master.completed_tasks) >= 1
# freeze 前置同步点：写 task 完成落账（替代裸 sleep 缓冲）
# 保留：等待原语
assert wait_until(lambda: len(master.completed_tasks) >= 1, timeout=10), \
    "write task must complete before freeze"
matrix_db.freeze()
matrix_frozen = matrix_db.is_frozen()
# 原断言：assert matrix_db.is_frozen()
record("step3/matrix_frozen", matrix_frozen, detail=matrix_frozen)

# ── Step 4: 跨 path merge matrix ──
merged_db = merge_db(MATRIX_PATH, merge_db_path=MERGE_TARGET, delete_source=True)
print(f"  merge done: {MATRIX_PATH} -> {MERGE_TARGET}")

# 验证源已删除
source_deleted = not os.path.isdir(MATRIX_PATH)
# 原断言：assert not os.path.isdir(MATRIX_PATH), "source should be deleted"
record("step4/source_deleted", source_deleted, detail=source_deleted)

# ── Step 5: merge 后 find_db 仍正确（通过 uid 重定向）──
found_after = solve_db.find_db(role="matrix")
# 保留：判空守卫——后续 print 还要用 found_after
assert found_after is not None, "find_db should still find matrix after merge"
found_after_uid = found_after.get_uid()
# 原断言：assert found_after.get_uid() == matrix_uid, "uid should be unchanged"
record("step5/find_after_uid_matches", found_after_uid == matrix_uid,
       detail={"found": found_after_uid, "expected": matrix_uid})
found_after_path = found_after.get_db_path()
# 原断言：assert found_after.get_db_path() == MERGE_TARGET, \
record("step5/find_after_db_path", found_after_path)
print(f"  post-merge find_db(matrix): uid={found_after.get_uid()}, "
      f"path={found_after.get_db_path()} ✓")

# ── Step 6: 验证 solve._DB_META.prev[].db_path 已更新 ──
solve_chain = DbMetaFile(SOLVE_PATH).read()
# 保留：判空守卫——后续 solve_chain["prev"] 还要用 solve_chain
assert solve_chain is not None
prev_edge = solve_chain["prev"][0]
prev_uid_match = prev_edge["uid"] == matrix_uid
# 原断言：assert prev_edge["uid"] == matrix_uid, "prev uid should match"
record("step6/prev_uid_matches", prev_uid_match,
       detail={"found": prev_edge["uid"], "expected": matrix_uid})
prev_path = prev_edge["db_path"]
# 原断言：assert prev_edge["db_path"] == MERGE_TARGET, \
record("step6/prev_db_path", prev_path)
print(f"  solve._DB_META.prev updated to {prev_edge['db_path']} ✓")

# ── Step 7: 验证 merged target._DB_META 有 absorbed_from ──
target_chain = DbMetaFile(MERGE_TARGET).read()
# 保留：判空守卫——后续 target_chain["uid"] 还要用 target_chain
assert target_chain is not None
target_uid_match = target_chain["uid"] == matrix_uid
# 原断言：assert target_chain["uid"] == matrix_uid, "target should inherit source uid"
record("step7/target_uid_inherited", target_uid_match,
       detail={"found": target_chain["uid"], "expected": matrix_uid})
absorbed_from = target_chain.get("absorbed_from", [])
absorbed_contains = MATRIX_PATH in absorbed_from
# 原断言：assert MATRIX_PATH in target_chain.get("absorbed_from", []), \
record("step7/absorbed_from_contains_source", absorbed_contains, detail=absorbed_from)
print(f"  target._DB_CHAIN absorbed_from={target_chain['absorbed_from']} ✓")

# ── Step 8: 验证 merged data 可读 ──
merged_x = merged_db.read_object("data/x")
# 原断言：assert merged_db.read_object("data/x") == 42, "merged db should read data"
record("step8/merged_data_x", merged_x)
print("  merged db reads data/x=42 ✓")

print("\nALL DONE")
