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

run_subcase("load_db_run2.py", timeout=60, env=shared_env)
INFO("[PASS] test_load_db_two_processes: two-process lifecycle verified")
