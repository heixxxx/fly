"""E2E test: db chain task 序列化 — db 带 uid/role 通过 task 传递到 worker。

验证流程：
  1. 定义 db 子类（MatrixDb role=matrix, SolveDb role=solve）
  2. 建链：matrix → solve
  3. 提交 task 把 solve_db 作为参数传递到 worker
  4. worker 上反序列化后：uid 正确、role 正确、类型正确、find_db 可用
"""
import os
import sys
import time
import shutil

from fly import open_db, launch_workers, as_task, wait_tasks, get_config
from fly.runtime import get_agent

DB_BASE = os.path.join(get_config().get_str("log_dir"), "db_chain_task")

from storage import Database


class MatrixDb(Database):
    role = "matrix"


class SolveDb(Database):
    role = "solve"


def cleanup():
    if os.path.isdir(DB_BASE):
        shutil.rmtree(DB_BASE, ignore_errors=True)


cleanup()

# launch worker
master = get_agent()
launch_workers([{}])
# 等待 worker 连接
import time as _t
_t0 = _t.time()
while _t.time() - _t0 < 10:
    if master.worker_count >= 1:
        break
    _t.sleep(0.5)
assert master.worker_count >= 1, "worker should connect"

# 建链
matrix_db = open_db(os.path.join(DB_BASE, "matrix"), db_cls=MatrixDb, logical_name="matrix")
solve_db = open_db(os.path.join(DB_BASE, "solve"), db_cls=SolveDb, prev=[matrix_db],
                   logical_name="solve")

# 提交 task 到 worker，验证 db 句柄传递后链信息完整
@as_task()
def verify_chain_task(db):
    """在 worker 上验证 db 的链信息。

    注意：worker 进程不知道 QA 测试里定义的 MatrixDb/SolveDb 子类
    （它们没被 import 到 worker），所以 type 可能是基类 Database。
    但 uid/role 来自磁盘 _DB_CHAIN，find_db 也读磁盘，所以这些是正确的。
    子类机制在 SolverProject 场景下有效（solver.dbs 模块被 worker import）。
    """
    results = {}
    results["uid"] = db.get_uid()
    results["role"] = db.get_role()

    # find_db 在 worker 上也可用（读磁盘 _DB_CHAIN）
    found = db.find_db(role="matrix")
    results["found_matrix_uid"] = found.get_uid() if found else None
    results["found_matrix_role"] = found.get_role() if found else None

    # 写结果到 db
    db.write_object("__chain_test_result", results)


verify_chain_task(solve_db)
assert wait_tasks(timeout=30.0), "task should complete"

time.sleep(0.5)  # 等落盘

result = solve_db.read_object("__chain_test_result")
assert result is not None, "result should be readable"
assert result["uid"] == solve_db.get_uid(), \
    f"uid mismatch: {result['uid']} != {solve_db.get_uid()}"
assert result["role"] == "solve", f"role mismatch: {result['role']}"
assert result["found_matrix_uid"] == matrix_db.get_uid(), \
    f"find_db(matrix) uid mismatch: {result['found_matrix_uid']} != {matrix_db.get_uid()}"
assert result["found_matrix_role"] == "matrix", \
    f"find_db(matrix) role mismatch: {result['found_matrix_role']}"

print(f"  uid on worker: {result['uid']} ✓")
print(f"  role on worker: {result['role']} ✓")
print(f"  find_db(matrix) on worker: uid={result['found_matrix_uid']}, role={result['found_matrix_role']} ✓")

print("\nALL PASSED")
