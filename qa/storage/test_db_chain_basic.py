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

from fly import open_db, get_config

DB_BASE = os.path.join(get_config().get_str("log_dir"), "db_chain_basic")


# ── 定义 db 子类 ──────────────────────────────────────────────────

try:
    from storage.py.database import _Database
except ImportError:
    from storage.database import _Database


class MatrixDb(_Database):
    """存储输入矩阵的 db。role=matrix。"""
    role = "matrix"

    def load_matrix(self):
        return self.read_object("matrix")


class SolveDb(_Database):
    """存储求解过程与结果的 db。role=solve。"""
    role = "solve"

    def load_solution(self):
        return self.read_object("__rasg__sol")


class AnalysisDb(_Database):
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
assert matrix_db.get_uid() is not None, "matrix_db should have uid"
assert matrix_db.get_role() == "matrix", f"role should be matrix, got {matrix_db.get_role()}"
assert isinstance(matrix_db, MatrixDb), "should be MatrixDb instance"
print(f"  matrix_db: uid={matrix_db.get_uid()}, path={matrix_db.get_db_path()}")

# ── Step 2: 建 solve db（前驱=matrix）──
solve_path = os.path.join(DB_BASE, "solve")
solve_db = open_db(solve_path, db_cls=SolveDb, prev=[matrix_db], logical_name="solve")
assert solve_db.get_uid() is not None
assert solve_db.get_role() == "solve"
assert isinstance(solve_db, SolveDb)
print(f"  solve_db: uid={solve_db.get_uid()}, path={solve_db.get_db_path()}")

# ── Step 3: 建 analysis db（前驱=solve）──
analysis_path = os.path.join(DB_BASE, "analysis")
analysis_db = open_db(analysis_path, db_cls=AnalysisDb, prev=[solve_db],
                      logical_name="analysis")
assert analysis_db.get_uid() is not None
assert analysis_db.get_role() == "analysis"
print(f"  analysis_db: uid={analysis_db.get_uid()}, path={analysis_db.get_db_path()}")

# ── Step 4: find_db — analysis 找 matrix（两跳）──
found_matrix = analysis_db.find_db(role="matrix")
assert found_matrix is not None, "should find matrix from analysis (2 hops)"
assert found_matrix.get_uid() == matrix_db.get_uid(), "uid should match"
assert isinstance(found_matrix, MatrixDb), "should be MatrixDb instance"
print(f"  find_db(matrix) from analysis: uid={found_matrix.get_uid()} ✓")

# ── Step 5: find_db — solve 找 matrix（一跳）──
found_from_solve = solve_db.find_db(role="matrix")
assert found_from_solve is not None
assert found_from_solve.get_uid() == matrix_db.get_uid()
assert isinstance(found_from_solve, MatrixDb)
print(f"  find_db(matrix) from solve: uid={found_from_solve.get_uid()} ✓")

# ── Step 6: find_db — analysis 找 solve（一跳）──
found_solve = analysis_db.find_db(role="solve")
assert found_solve is not None
assert found_solve.get_uid() == solve_db.get_uid()
assert isinstance(found_solve, SolveDb)
print(f"  find_db(solve) from analysis: uid={found_solve.get_uid()} ✓")

# ── Step 7: find_db 找不到的情况 ──
not_found = analysis_db.find_db(role="nonexistent")
assert not_found is None, "should not find nonexistent role"
print(f"  find_db(nonexistent) from analysis: None ✓")

# ── Step 8: prevs / nexts 双向链 ──
analysis_prevs = analysis_db.prevs()
assert len(analysis_prevs) == 1, f"analysis should have 1 prev, got {len(analysis_prevs)}"
assert analysis_prevs[0].get_uid() == solve_db.get_uid()
print(f"  analysis.prevs(): {[p.get_uid() for p in analysis_prevs]} ✓")

solve_nexts = solve_db.nexts()
assert len(solve_nexts) == 1, f"solve should have 1 next, got {len(solve_nexts)}"
assert solve_nexts[0].get_uid() == analysis_db.get_uid()
print(f"  solve.nexts(): {[n.get_uid() for n in solve_nexts]} ✓")

solve_prevs = solve_db.prevs()
assert len(solve_prevs) == 1
assert solve_prevs[0].get_uid() == matrix_db.get_uid()
print(f"  solve.prevs(): {[p.get_uid() for p in solve_prevs]} ✓")

matrix_nexts = matrix_db.nexts()
assert len(matrix_nexts) == 1
assert matrix_nexts[0].get_uid() == solve_db.get_uid()
print(f"  matrix.nexts(): {[n.get_uid() for n in matrix_nexts]} ✓")

# ── Step 9: find_db by uid ──
found_by_uid = analysis_db.find_db(uid=matrix_db.get_uid())
assert found_by_uid is not None
assert found_by_uid.get_uid() == matrix_db.get_uid()
print(f"  find_db(uid={matrix_db.get_uid()[:8]}): ✓")

# ── Step 10: find_db by logical_name ──
found_by_name = analysis_db.find_db(logical_name="matrix")
assert found_by_name is not None
assert found_by_name.get_uid() == matrix_db.get_uid()
print(f"  find_db(logical_name=matrix): ✓")

print("\nALL PASSED")
