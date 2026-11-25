"""E2E test: db chain path 复用 — merge 后在原址新建同名 db 不错配。

这是 uid 机制的核心价值场景（docs/db-chain-design.md §8）：
  t0: open_db("/proj/matrix", role=matrix) → uid=A（含数据 D_A）
  t1: merge_db("/proj/matrix" → "/shared/matrix") → 源彻底删除
  t2: open_db("/proj/matrix", role=matrix) → uid=B（新数据 D_B，自动递增避让）
  t3: 下游 solve 的 prev 仍指向 uid=A → find_db 正确定位到 /shared/matrix

uid 阻断了 path 复用的别名冲突。
"""
import os
import shutil

from test import record
from fly import open_db, merge_db, launch_workers, get_config
from fly.runtime import get_agent

DB_BASE = os.path.join(get_config().get_str("log_dir"), "db_chain_reuse")
MATRIX_PATH = os.path.join(DB_BASE, "matrix")
SOLVE_PATH = os.path.join(DB_BASE, "solve")
MERGE_TARGET = os.path.join(DB_BASE, "shared_matrix")

from storage import Database


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
assert wait_until(lambda: master.worker_count >= 1, timeout=10), \
    "worker should connect"

# ── t0: 建 matrix_db_A（uid=A）+ 写数据 D_A ──
from test import write_data
matrix_db_A = open_db(MATRIX_PATH, db_cls=MatrixDb, logical_name="matrix")
uid_A = matrix_db_A.get_uid()
write_data(matrix_db_A, "data/original", 100)
# 保留：等待原语（freeze 前同步点）
assert master.wait_for_all_tasks(timeout=10) or len(master.completed_tasks) >= 1
# freeze 前置同步点：写 task 完成落账（替代裸 sleep 缓冲）
# 保留：等待原语
assert wait_until(lambda: len(master.completed_tasks) >= 1, timeout=10), \
    "write task must complete before freeze"
matrix_db_A.freeze()
matrix_frozen = matrix_db_A.is_frozen()
# 原断言：assert matrix_db_A.is_frozen()
record("t0/matrix_frozen", matrix_frozen, detail=matrix_frozen)
print(f"  t0: matrix_db_A uid={uid_A}, path={MATRIX_PATH}")

# ── 建链 solve → matrix（prev 记录 uid=A）──
solve_db = open_db(SOLVE_PATH, db_cls=SolveDb, prev=[matrix_db_A], logical_name="solve")
print(f"  solve_db: uid={solve_db.get_uid()}, prev={solve_db.prevs()[0].get_uid()}")

# ── t1: merge matrix → shared_matrix（彻底删源）──
merged_db = merge_db(MATRIX_PATH, merge_db_path=MERGE_TARGET, delete_source=True)
source_deleted = not os.path.isdir(MATRIX_PATH)
# 原断言：assert not os.path.isdir(MATRIX_PATH), "source should be deleted"
record("t1/source_deleted", source_deleted, detail=source_deleted)
print(f"  t1: merged to {MERGE_TARGET}, source deleted")

# ── t2: 在原址新建 matrix_db_B（uid=B，新数据 D_B）──
# open_db 自动递增：原址无 _DB_META（已删），直接用 MATRIX_PATH
matrix_db_B = open_db(MATRIX_PATH, db_cls=MatrixDb, logical_name="matrix")
uid_B = matrix_db_B.get_uid()
write_data(matrix_db_B, "data/new", 200)
uid_B_differs = uid_B != uid_A
# 原断言：assert uid_B != uid_A, f"new db should have different uid: {uid_B} == {uid_A}"
record("t2/uid_B_differs", uid_B_differs, detail={"uid_B": uid_B, "uid_A": uid_A})
print(f"  t2: matrix_db_B uid={uid_B} (!= {uid_A}), path={MATRIX_PATH}")

# ── t3: solve 的 find_db(role=matrix) 应找到 uid=A（在 /shared/matrix），不是 uid=B ──
found = solve_db.find_db(role="matrix")
# 保留：判空守卫——后续 found.read_object 与 print 还要用 found
assert found is not None, "should find matrix in chain"
found_uid = found.get_uid()
# 原断言：assert found.get_uid() == uid_A, \
record("t3/find_uid_is_original", found_uid == uid_A,
       detail={"found": found_uid, "expected": uid_A})
found_path = found.get_db_path()
# 原断言：assert found.get_db_path() == MERGE_TARGET, \
record("t3/find_db_path", found_path)
print(f"  t3: find_db(matrix) from solve: uid={found.get_uid()} (correct! not {uid_B})")
print(f"       path={found.get_db_path()} ✓")

# ── 验证数据正确：找到的是 D_A（original=100），不是 D_B（new=200）──
original_value = found.read_object("data/original")
# 原断言：assert found.read_object("data/original") == 100, "should read original data D_A"
record("t3/original_data", original_value)
print("  data verification: found.read('data/original')=100 ✓ (D_A, not D_B)")

print("\nALL DONE")
