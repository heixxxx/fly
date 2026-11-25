"""run2：migrate_project 目录搬迁——meta/chain/_DB_META 的 data_path 改写断言。"""
import json
import os
import shutil

from _fly_log import INFO
from fly import migrate_project
from test import record

PROJ_PATH = os.environ["FLY_PROJ_PATH"]
NEW_PATH = os.environ["FLY_NEW_PROJ_PATH"]

proj = migrate_project(PROJ_PATH, NEW_PATH)

# project meta：db_path / data_path 均改写到新根。
with open(os.path.join(NEW_PATH, "_PROJECT_META.json"), encoding="utf-8") as f:
    meta = json.load(f)
info = meta["dbs"]["workdb"]
new_abs = os.path.abspath(NEW_PATH)
db_path_ok = info["db_path"].startswith(new_abs)
# 原断言：assert info["db_path"].startswith(new_abs), f"db_path not rewritten: {info['db_path']}"
record("resume_run2/meta_db_path_rewritten", db_path_ok, detail=info["db_path"])
data_path_ok = info["data_path"].startswith(new_abs)
# 原断言：assert info["data_path"].startswith(new_abs), f"data_path not rewritten: {info['data_path']}"
record("resume_run2/meta_data_path_rewritten", data_path_ok, detail=info["data_path"])

# _DB_META JSON：顶层 data_path 同批改写。
with open(os.path.join(NEW_PATH, "workdb", "_DB_META"), encoding="utf-8") as f:
    db_meta = json.load(f)
db_meta_data_path_ok = db_meta.get("data_path", "").startswith(new_abs)
# 原断言：assert db_meta.get("data_path", "").startswith(new_abs), \
record("resume_run2/db_meta_data_path_rewritten", db_meta_data_path_ok,
       detail=db_meta.get("data_path"))

# 分离 data 目录随迁。
data_dir_moved = os.path.isdir(os.path.join(NEW_PATH, "workdb_data"))
# 原断言：assert os.path.isdir(os.path.join(NEW_PATH, "workdb_data")), \
record("resume_run2/separated_data_dir_moved", data_dir_moved,
       detail=os.path.join(NEW_PATH, "workdb_data"))

# 旧根整体消失（无幽灵残留）。
old_root_gone = not os.path.exists(PROJ_PATH)
# 原断言：assert not os.path.exists(PROJ_PATH), f"old project root must be gone: {PROJ_PATH}"
record("resume_run2/old_root_gone", old_root_gone, detail=PROJ_PATH)

INFO(f"[DONE] migrate_resume_run2: migrated to {NEW_PATH}")
