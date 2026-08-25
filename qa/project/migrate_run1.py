"""run1：建 project（matrix → solve 两个 db，chain 边 matrix.next=[solve]），写数据 freeze。"""
import os
import shutil
import time

from storage import Database
from fly import as_task, open_project
from fly.runtime import get_agent

PROJ_PATH = os.environ["FLY_PROJ_PATH"]

if os.path.isdir(PROJ_PATH):
    shutil.rmtree(PROJ_PATH, ignore_errors=True)


class QMatrixDb(Database):
    role = "matrix"


class QSolveDb(Database):
    role = "solve"


master = get_agent()
master.launch_local_workers([{}])
assert master.wait_for_workers(1)

proj = open_project(PROJ_PATH)
matrix_db = proj._create_db("matrix", db_cls=QMatrixDb)
solve_db = proj._create_db("solve", db_cls=QSolveDb, prev=[matrix_db])


@as_task()
def write_obj(db, key, val):
    db.write_object(key, val)


write_obj(matrix_db, "matrix", {"n": 4})
write_obj(solve_db, "sol", {"converged": True})
assert master.wait_for_all_tasks(timeout=60)

matrix_db.freeze()
solve_db.freeze()
for name in ("matrix", "solve"):
    t0 = time.time()
    while not proj.is_db_frozen(name) and time.time() - t0 < 30:
        time.sleep(0.2)
    assert proj.is_db_frozen(name), f"{name} should be frozen"

master.stop()
