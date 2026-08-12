"""Complex scenario（.pyt 编排，替代旧 subprocess wrapper）。

run1 (complex_run1.py): 初始数据生产（多 DB）
run2 (complex_run2.py): load_db + migration + dynamic props + restart

db 在 case log 目录，两 sub case 经 env 共享 FLY_DB_DIR。
"""
import os, shutil

DB_DIR = FLY_CASE_LOG_DIR

def setup_clean():
    for p in ["db_raw", "db_feat", "db_raw_moved", "db_feat_moved", "db_model"]:
        dp = os.path.join(DB_DIR, p)
        if os.path.isdir(dp):
            shutil.rmtree(dp, ignore_errors=True)

shared_env = {"FLY_DB_DIR": DB_DIR}

run_subcase("complex_run1.py", timeout=120, setup=setup_clean, env=shared_env)
run_subcase("complex_run2.py", timeout=120, env=shared_env)
INFO("[PASS] test_complex_scenario — all features verified across 2 runs")
