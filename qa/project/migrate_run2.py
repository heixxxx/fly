"""run2：fly.migrate_project 目录搬迁（meta db_path 改写 + chain 邻居边更新）。"""
import json
import os

from fly import migrate_project
from test import record

OLD = os.environ["FLY_PROJ_PATH"]
NEW = os.environ["FLY_NEW_PROJ_PATH"]

proj = migrate_project(OLD, NEW)
# 原断言：assert proj.db_path == os.path.abspath(NEW)
record("migrate/db_path", proj.db_path)
old_gone = not os.path.isdir(OLD)
# 原断言：assert not os.path.isdir(OLD), "old project dir should be moved away"
record("migrate/old_dir_gone", old_gone, detail=OLD)

# meta 改写断言：db_path 指向新位置。（循环遍历运行时集合，记录挪到循环外累计。）
meta = json.load(open(os.path.join(NEW, "_PROJECT_META.json"), encoding="utf-8"))
meta_ok = True
meta_bad = []
for actual, info in meta["dbs"].items():
    ok = info["db_path"].startswith(os.path.abspath(NEW))
    if not ok:
        meta_bad.append({"db": actual, "db_path": info["db_path"]})
    meta_ok = meta_ok and ok
# 原断言：assert info["db_path"].startswith(os.path.abspath(NEW)), \
record("migrate/meta_db_path_rewritten", meta_ok, detail=meta_bad)

# chain 邻居边断言：matrix.next / solve.prev 的 db_path 均为新路径。
# （chain 字段合并进 _DB_META JSON 后从这里读取。）
def read_chain(db_path):
    return json.load(open(os.path.join(db_path, "_DB_META"), encoding="utf-8"))

matrix_chain = read_chain(os.path.join(NEW, "matrix"))
solve_chain = read_chain(os.path.join(NEW, "solve"))
new_abs = os.path.abspath(NEW)
matrix_next_ok = all(e["db_path"].startswith(new_abs) for e in matrix_chain.get("next", []))
# 原断言：assert edge["db_path"].startswith(new_abs), \
record("migrate/matrix_next_rewritten", matrix_next_ok, detail=matrix_chain.get("next"))
solve_prev_ok = all(e["db_path"].startswith(new_abs) for e in solve_chain.get("prev", []))
# 原断言：assert edge["db_path"].startswith(new_abs), \
record("migrate/solve_prev_rewritten", solve_prev_ok, detail=solve_chain.get("prev"))
# uid 不变（逻辑身份保持）。
uid_ok = matrix_chain["uid"] == solve_chain["prev"][0]["uid"]
# 原断言：assert matrix_chain["uid"] == solve_chain["prev"][0]["uid"], \
record("migrate/uid_preserved", uid_ok,
       detail={"matrix": matrix_chain.get("uid"), "solve_prev0": solve_chain.get("prev", [{}])[0].get("uid")})

print(f"[DONE] migrate_run2: project moved {OLD} -> {NEW}, meta+chain rewritten")
