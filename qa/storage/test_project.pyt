"""fly.Project 基类机制（.pyt 编排，5 sub case 真拆，替代旧单 sub case 包装）。

basic (project_basic_mechanism.py): 注册 / _create_db / get_db / freeze / 持久化 / 重名 / pickle
load1+load2 (project_load_run1.py / run2.py): load_project 两进程全量恢复 + 子类还原
sync (project_sync_freeze.py): 同步 freeze 管理（freeze_db/freeze_all）+ 边界
errors (project_errors.py): load 错误分支（无 meta → RuntimeError）

db 在 case log 目录；load1/load2 经 env 共享 FLY_PROJ_PATH。
"""
import os

PROJ_LOAD = os.path.join(FLY_CASE_LOG_DIR, "project_load")
load_env = {"FLY_PROJ_PATH": PROJ_LOAD}

run_subcase("project_basic_mechanism.py", timeout=120)
run_subcase("project_load_run1.py", timeout=120, env=load_env)
assert os.path.isfile(os.path.join(PROJ_LOAD, "_PROJECT_META.json")), \
    "_PROJECT_META.json should exist after load_run1"
run_subcase("project_load_run2.py", timeout=120, env=load_env)
run_subcase("project_sync_freeze.py", timeout=120)
run_subcase("project_errors.py", timeout=120)
INFO("[PASS] test_project: 5 sub cases (basic + load + sync + errors) verified")
