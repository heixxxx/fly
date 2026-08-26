"""run2：fly.migrate_project 目录搬迁（meta db_path 改写 + chain 邻居边更新）。"""
import json
import os

from fly import migrate_project

OLD = os.environ["FLY_PROJ_PATH"]
NEW = os.environ["FLY_NEW_PROJ_PATH"]

proj = migrate_project(OLD, NEW)
assert proj.db_path == os.path.abspath(NEW)
assert not os.path.isdir(OLD), "old project dir should be moved away"

# meta 改写断言：db_path 指向新位置。
meta = json.load(open(os.path.join(NEW, "_PROJECT_META.json"), encoding="utf-8"))
for actual, info in meta["dbs"].items():
    assert info["db_path"].startswith(os.path.abspath(NEW)), \
        f"meta db_path not rewritten: {actual} -> {info['db_path']}"

# chain 邻居边断言：matrix.next / solve.prev 的 db_path 均为新路径。
# （chain 字段合并进 _DB_META JSON 后从这里读取。）
def read_chain(db_path):
    return json.load(open(os.path.join(db_path, "_DB_META"), encoding="utf-8"))

matrix_chain = read_chain(os.path.join(NEW, "matrix"))
solve_chain = read_chain(os.path.join(NEW, "solve"))
new_abs = os.path.abspath(NEW)
for edge in matrix_chain.get("next", []):
    assert edge["db_path"].startswith(new_abs), \
        f"matrix next edge not rewritten: {edge}"
for edge in solve_chain.get("prev", []):
    assert edge["db_path"].startswith(new_abs), \
        f"solve prev edge not rewritten: {edge}"
# uid 不变（逻辑身份保持）。
assert matrix_chain["uid"] == solve_chain["prev"][0]["uid"], \
    "uid must survive migration"

print(f"[PASS] migrate_run2: project moved {OLD} -> {NEW}, meta+chain rewritten")
