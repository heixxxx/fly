"""E2E test: db chain 基础机制 — 建链、find_db、role 子类、DAG 多跳。

验证流程：
  1. 定义 db 子类（MatrixDb role=matrix, SolveDb role=solve, AnalysisDb role=analysis）
  2. 建链：matrix → solve → analysis（三步流程 DAG）
  3. find_db: analysis 沿链向前找 matrix（两跳）
  4. find_db: solve 沿链向前找 matrix（一跳）
  5. role 子类: find_db 返回正确子类实例
  6. nexts/prevs: 双向链遍历
"""
import os
import sys
import time

from test import record
from fly import open_db, get_config

DB_BASE = os.path.join(get_config().get_str("log_dir"), "db_chain_basic")


# ── 定义 db 子类 ──────────────────────────────────────────────────

from storage import Database


class MatrixDb(Database):
    """存储输入矩阵的 db。role=matrix。"""
    role = "matrix"

    def load_matrix(self):
        return self.read_object("matrix")


class SolveDb(Database):
    """存储求解过程与结果的 db。role=solve。"""
    role = "solve"

    def load_solution(self):
        return self.read_object("__rasg__sol")


class AnalysisDb(Database):
    """存储误差分析的 db。role=analysis。"""
    role = "analysis"


# ── 测试 ──────────────────────────────────────────────────────────

import shutil

def cleanup():
    if os.path.isdir(DB_BASE):
        shutil.rmtree(DB_BASE, ignore_errors=True)


cleanup()

# ── Step 1: 建 matrix db（无前驱）──
matrix_path = os.path.join(DB_BASE, "matrix")
matrix_db = open_db(matrix_path, db_cls=MatrixDb, logical_name="matrix")
matrix_uid = matrix_db.get_uid()
# 原断言：assert matrix_db.get_uid() is not None, "matrix_db should have uid"
record("step1/matrix_db_has_uid", matrix_uid is not None, detail=matrix_uid)
matrix_role = matrix_db.get_role()
# 原断言：assert matrix_db.get_role() == "matrix", f"role should be matrix, got {matrix_db.get_role()}"
record("step1/matrix_db_role", matrix_role)
# 原断言：assert isinstance(matrix_db, MatrixDb), "should be MatrixDb instance"
record("step1/matrix_db_type", type(matrix_db).__name__)
print(f"  matrix_db: uid={matrix_db.get_uid()}, path={matrix_db.get_db_path()}")

# ── Step 2: 建 solve db（前驱=matrix）──
solve_path = os.path.join(DB_BASE, "solve")
solve_db = open_db(solve_path, db_cls=SolveDb, prev=[matrix_db], logical_name="solve")
solve_uid = solve_db.get_uid()
# 原断言：assert solve_db.get_uid() is not None
record("step2/solve_db_has_uid", solve_uid is not None, detail=solve_uid)
solve_role = solve_db.get_role()
# 原断言：assert solve_db.get_role() == "solve"
record("step2/solve_db_role", solve_role)
# 原断言：assert isinstance(solve_db, SolveDb)
record("step2/solve_db_type", type(solve_db).__name__)
print(f"  solve_db: uid={solve_db.get_uid()}, path={solve_db.get_db_path()}")

# ── Step 3: 建 analysis db（前驱=solve）──
analysis_path = os.path.join(DB_BASE, "analysis")
analysis_db = open_db(analysis_path, db_cls=AnalysisDb, prev=[solve_db],
                      logical_name="analysis")
analysis_uid = analysis_db.get_uid()
# 原断言：assert analysis_db.get_uid() is not None
record("step3/analysis_db_has_uid", analysis_uid is not None, detail=analysis_uid)
analysis_role = analysis_db.get_role()
# 原断言：assert analysis_db.get_role() == "analysis"
record("step3/analysis_db_role", analysis_role)
print(f"  analysis_db: uid={analysis_db.get_uid()}, path={analysis_db.get_db_path()}")

# ── Step 4: find_db — analysis 找 matrix（两跳）──
found_matrix = analysis_db.find_db(role="matrix")
# 保留：判空守卫——后续 print 还要用 found_matrix
assert found_matrix is not None, "should find matrix from analysis (2 hops)"
found_matrix_uid = found_matrix.get_uid()
# 原断言：assert found_matrix.get_uid() == matrix_db.get_uid(), "uid should match"
record("step4/find_matrix_uid_matches", found_matrix_uid == matrix_uid,
       detail={"found": found_matrix_uid, "expected": matrix_uid})
# 原断言：assert isinstance(found_matrix, MatrixDb), "should be MatrixDb instance"
record("step4/find_matrix_type", type(found_matrix).__name__)
print(f"  find_db(matrix) from analysis: uid={found_matrix.get_uid()} ✓")

# ── Step 5: find_db — solve 找 matrix（一跳）──
found_from_solve = solve_db.find_db(role="matrix")
# 保留：判空守卫——后续 print 还要用 found_from_solve
assert found_from_solve is not None
found_from_solve_uid = found_from_solve.get_uid()
# 原断言：assert found_from_solve.get_uid() == matrix_db.get_uid()
record("step5/find_from_solve_uid_matches", found_from_solve_uid == matrix_uid,
       detail={"found": found_from_solve_uid, "expected": matrix_uid})
# 原断言：assert isinstance(found_from_solve, MatrixDb)
record("step5/find_from_solve_type", type(found_from_solve).__name__)
print(f"  find_db(matrix) from solve: uid={found_from_solve.get_uid()} ✓")

# ── Step 6: find_db — analysis 找 solve（一跳）──
found_solve = analysis_db.find_db(role="solve")
# 保留：判空守卫——后续 print 还要用 found_solve
assert found_solve is not None
found_solve_uid = found_solve.get_uid()
# 原断言：assert found_solve.get_uid() == solve_db.get_uid()
record("step6/find_solve_uid_matches", found_solve_uid == solve_uid,
       detail={"found": found_solve_uid, "expected": solve_uid})
# 原断言：assert isinstance(found_solve, SolveDb)
record("step6/find_solve_type", type(found_solve).__name__)
print(f"  find_db(solve) from analysis: uid={found_solve.get_uid()} ✓")

# ── Step 7: find_db 找不到的情况 ──
not_found = analysis_db.find_db(role="nonexistent")
# 原断言：assert not_found is None, "should not find nonexistent role"
record("step7/nonexistent_not_found", not_found is None, detail=not_found)
print("  find_db(nonexistent) from analysis: None ✓")

# ── Step 8: prevs / nexts 双向链 ──
analysis_prevs = analysis_db.prevs()
# 原断言：assert len(analysis_prevs) == 1, f"analysis should have 1 prev, got {len(analysis_prevs)}"
record("step8/analysis_prev_count", len(analysis_prevs))
analysis_prev_uids = [p.get_uid() for p in analysis_prevs]
# 原断言：assert analysis_prevs[0].get_uid() == solve_db.get_uid()
record("step8/analysis_prev_uid_matches_solve",
       analysis_prev_uids[0] == solve_uid if analysis_prev_uids else None,
       detail=analysis_prev_uids)
print(f"  analysis.prevs(): {[p.get_uid() for p in analysis_prevs]} ✓")

solve_nexts = solve_db.nexts()
# 原断言：assert len(solve_nexts) == 1, f"solve should have 1 next, got {len(solve_nexts)}"
record("step8/solve_next_count", len(solve_nexts))
solve_next_uids = [n.get_uid() for n in solve_nexts]
# 原断言：assert solve_nexts[0].get_uid() == analysis_db.get_uid()
record("step8/solve_next_uid_matches_analysis",
       solve_next_uids[0] == analysis_uid if solve_next_uids else None,
       detail=solve_next_uids)
print(f"  solve.nexts(): {[n.get_uid() for n in solve_nexts]} ✓")

solve_prevs = solve_db.prevs()
# 原断言：assert len(solve_prevs) == 1
record("step8/solve_prev_count", len(solve_prevs))
solve_prev_uids = [p.get_uid() for p in solve_prevs]
# 原断言：assert solve_prevs[0].get_uid() == matrix_db.get_uid()
record("step8/solve_prev_uid_matches_matrix",
       solve_prev_uids[0] == matrix_uid if solve_prev_uids else None,
       detail=solve_prev_uids)
print(f"  solve.prevs(): {[p.get_uid() for p in solve_prevs]} ✓")

matrix_nexts = matrix_db.nexts()
# 原断言：assert len(matrix_nexts) == 1
record("step8/matrix_next_count", len(matrix_nexts))
matrix_next_uids = [n.get_uid() for n in matrix_nexts]
# 原断言：assert matrix_nexts[0].get_uid() == solve_db.get_uid()
record("step8/matrix_next_uid_matches_solve",
       matrix_next_uids[0] == solve_uid if matrix_next_uids else None,
       detail=matrix_next_uids)
print(f"  matrix.nexts(): {[n.get_uid() for n in matrix_nexts]} ✓")

# ── Step 9: find_db by uid ──
found_by_uid = analysis_db.find_db(uid=matrix_uid)
# 保留：判空守卫——后续 print 还要用 found_by_uid
assert found_by_uid is not None
found_by_uid_matched = found_by_uid.get_uid() == matrix_uid
# 原断言：assert found_by_uid.get_uid() == matrix_db.get_uid()
record("step9/find_by_uid_matches", found_by_uid_matched,
       detail={"found": found_by_uid.get_uid(), "expected": matrix_uid})
print(f"  find_db(uid={matrix_uid[:8]}): ✓")

# ── Step 10: find_db by logical_name ──
found_by_name = analysis_db.find_db(logical_name="matrix")
# 保留：判空守卫——后续 print 还要用 found_by_name
assert found_by_name is not None
found_by_name_matched = found_by_name.get_uid() == matrix_uid
# 原断言：assert found_by_name.get_uid() == matrix_db.get_uid()
record("step10/find_by_name_matches", found_by_name_matched,
       detail={"found": found_by_name.get_uid(), "expected": matrix_uid})
print("  find_db(logical_name=matrix): ✓")

print("\nALL DONE")
