"""run2：migrate_project(consolidate=True, new_path=...)——跨 host 数据集中 +
目录搬迁组合链路（在线 master + worker；全程无超时）。"""
import os

from _fly_log import INFO
from fly import migrate_project
from fly.runtime import get_agent

PROJ_PATH = os.environ["FLY_PROJ_PATH"]
NEW_PATH = os.environ["FLY_NEW_PROJ_PATH"]

# consolidate 需要在线 worker（merge task 执行者）：源 host + master host 各一。
master = get_agent()
master.launch_local_workers([{"host": "consol-host-a"}, {}])
assert master.wait_for_workers(2)

proj = migrate_project(PROJ_PATH, NEW_PATH, consolidate=True)

# 集中产物自包含：merged data 目录随 project 搬到新位置。
merged_dirs = [d for d in os.listdir(NEW_PATH) if d.endswith(".merged_data")]
assert merged_dirs, f".merged_data should move with project, got: {os.listdir(NEW_PATH)}"
INFO(f"[PASS] migrate_consol_run2: consolidated + moved, merged_data={merged_dirs}")
master.stop()
