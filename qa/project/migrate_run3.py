"""run3：从新路径 load_project → 数据可读 + chain 前驱查找（find_db 沿新边定位）。"""
import os

from fly import load_project
from test import record
from fly.runtime import get_agent

NEW = os.environ["FLY_NEW_PROJ_PATH"]

master = get_agent()
master.launch_local_workers([{}])
assert master.wait_for_workers(1)

proj = load_project(NEW)
# 原断言：assert proj.list_dbs() == ["matrix", "solve"], proj.list_dbs()
record("migrate/list_dbs", proj.list_dbs())

solve_db = proj.get_db("solve")
sol_val = solve_db.read_object("sol")
# 原断言：assert solve_db.read_object("sol") == {"converged": True}
record("migrate/sol", sol_val)

# chain 前驱查找：solve 沿 prev 边（已改写的新路径）找到 matrix db。
matrix_db = solve_db.find_db(role="matrix")
# 原断言：assert matrix_db is not None, "find_db(role=matrix) should traverse rewritten prev edge"
record("migrate/find_db_matrix_found", matrix_db is not None)
# 保留：守卫——后面的 read_object 依赖 matrix_db 非空
assert matrix_db is not None, "find_db(role=matrix) should traverse rewritten prev edge"
matrix_val = matrix_db.read_object("matrix")
# 原断言：assert matrix_db.read_object("matrix") == {"n": 4}
record("migrate/matrix", matrix_val)

master.stop()
print("[DONE] migrate_run3: load_project at new path + chain traversal verified")
