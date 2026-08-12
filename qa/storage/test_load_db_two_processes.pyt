"""load_db 两进程生命周期（.pyt 编排，替代旧 wrapper 场景2）。

run1 建库写数据，run2 load_db + 读 + 新 task + freeze。db 在 case log 目录（自动随 case 清）。
"""
import os, shutil

db_path = os.path.join(FLY_CASE_LOG_DIR, "load_db_p2")
shared_env = {"FLY_DB_PATH": db_path}

def setup_clean():
    if os.path.isdir(db_path):
        shutil.rmtree(db_path, ignore_errors=True)

run_subcase("load_db_run1.py", timeout=60, setup=setup_clean, env=shared_env)
# 验证 run1 产物（_DB_META 存在，未 freeze）
assert os.path.isdir(db_path), "DB should exist after run1"
assert os.path.isfile(os.path.join(db_path, "_DB_META")), "_DB_META should exist"
assert not os.path.isfile(os.path.join(db_path, "_FROZEN")), "should NOT be frozen after run1"

run_subcase("load_db_run2.py", timeout=60, env=shared_env)
assert os.path.isfile(os.path.join(db_path, "_FROZEN")), "should be frozen after run2"
INFO("[PASS] test_load_db_two_processes: two-process lifecycle verified")
