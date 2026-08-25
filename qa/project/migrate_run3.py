"""run3：从新路径 load_project → 数据可读 + chain 前驱查找（find_db 沿新边定位）。"""
import os

from fly import load_project
from fly.runtime import get_agent

NEW = os.environ["FLY_NEW_PROJ_PATH"]

master = get_agent()
master.launch_local_workers([{}])
assert master.wait_for_workers(1)

proj = load_project(NEW)
assert proj.list_dbs() == ["matrix", "solve"], proj.list_dbs()

solve_db = proj.get_db("solve")
assert solve_db.read_object("sol") == {"converged": True}

# chain 前驱查找：solve 沿 prev 边（已改写的新路径）找到 matrix db。
matrix_db = solve_db.find_db(role="matrix")
assert matrix_db is not None, "find_db(role=matrix) should traverse rewritten prev edge"
assert matrix_db.read_object("matrix") == {"n": 4}

master.stop()
print("[PASS] migrate_run3: load_project at new path + chain traversal verified")
