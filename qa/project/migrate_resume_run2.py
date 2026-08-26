"""run2：migrate_project 目录搬迁——meta/chain/_DB_META 的 data_path 改写断言。"""
import json
import os
import shutil

from _fly_log import INFO
from fly import migrate_project

PROJ_PATH = os.environ["FLY_PROJ_PATH"]
NEW_PATH = os.environ["FLY_NEW_PROJ_PATH"]

proj = migrate_project(PROJ_PATH, NEW_PATH)

# project meta：db_path / data_path 均改写到新根。
with open(os.path.join(NEW_PATH, "_PROJECT_META.json"), encoding="utf-8") as f:
    meta = json.load(f)
info = meta["dbs"]["workdb"]
new_abs = os.path.abspath(NEW_PATH)
assert info["db_path"].startswith(new_abs), f"db_path not rewritten: {info['db_path']}"
assert info["data_path"].startswith(new_abs), f"data_path not rewritten: {info['data_path']}"

# _DB_META JSON：顶层 data_path 同批改写。
with open(os.path.join(NEW_PATH, "workdb", "_DB_META"), encoding="utf-8") as f:
    db_meta = json.load(f)
assert db_meta.get("data_path", "").startswith(new_abs), \
    f"_DB_META data_path not rewritten: {db_meta.get('data_path')}"

# 分离 data 目录随迁。
assert os.path.isdir(os.path.join(NEW_PATH, "workdb_data")), \
    "separated data dir must move with project"

# 旧根整体消失（无幽灵残留）。
assert not os.path.exists(PROJ_PATH), f"old project root must be gone: {PROJ_PATH}"

INFO(f"[PASS] migrate_resume_run2: migrated to {NEW_PATH}")
