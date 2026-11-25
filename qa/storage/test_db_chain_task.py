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

from test import record
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
# 保留：等待原语——手写轮询循环的收尾判定，worker 没连上后续 task 必然挂起
assert master.worker_count >= 1, "worker should connect"

# 建链
matrix_db = open_db(os.path.join(DB_BASE, "matrix"), db_cls=MatrixDb, logical_name="matrix")
solve_db = open_db(os.path.join(DB_BASE, "solve"), db_cls=SolveDb, prev=[matrix_db],
                   logical_name="solve")
matrix_uid = matrix_db.get_uid()
solve_uid = solve_db.get_uid()

# 提交 task 到 worker，验证 db 句柄传递后链信息完整
@as_task()
def verify_chain_task(db):
    """在 worker 上验证 db 的链信息。

    注意：worker 进程不知道 QA 测试里定义的 MatrixDb/SolveDb 子类
    （它们没被 import 到 worker），所以 type 可能是基类 Database。
    但 uid/role 来自磁盘 _DB_META，find_db 也读磁盘，所以这些是正确的。
    子类机制在 SolverProject 场景下有效（solver.dbs 模块被 worker import）。
    """
    results = {}
    results["uid"] = db.get_uid()
    results["role"] = db.get_role()

    # find_db 在 worker 上也可用（读磁盘 _DB_META）
    found = db.find_db(role="matrix")
    results["found_matrix_uid"] = found.get_uid() if found else None
    results["found_matrix_role"] = found.get_role() if found else None

    # 写结果到 db
    db.write_object("__chain_test_result", results)


verify_chain_task(solve_db)
# 保留：等待原语（fly 的 wait_tasks 等全部任务完成）
assert wait_tasks(timeout=30.0), "task should complete"

time.sleep(0.5)  # 等落盘

result = solve_db.read_object("__chain_test_result")
# 保留：判空守卫——后续 result["uid"] 等取值还要用 result
assert result is not None, "result should be readable"
uid_matches = result["uid"] == solve_uid
# 原断言：assert result["uid"] == solve_db.get_uid(), \
record("worker/uid_matches", uid_matches,
       detail={"found": result["uid"], "expected": solve_uid})
# 原断言：assert result["role"] == "solve", f"role mismatch: {result['role']}"
record("worker/role", result["role"])
found_uid_matches = result["found_matrix_uid"] == matrix_uid
# 原断言：assert result["found_matrix_uid"] == matrix_db.get_uid(), \
record("worker/found_matrix_uid_matches", found_uid_matches,
       detail={"found": result["found_matrix_uid"], "expected": matrix_uid})
# 原断言：assert result["found_matrix_role"] == "matrix", \
record("worker/found_matrix_role", result["found_matrix_role"])

print(f"  uid on worker: {result['uid']} ✓")
print(f"  role on worker: {result['role']} ✓")
print(f"  find_db(matrix) on worker: uid={result['found_matrix_uid']}, role={result['found_matrix_role']} ✓")

print("\nALL DONE")
