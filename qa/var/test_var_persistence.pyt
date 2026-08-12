"""var 跨进程持久化（.pyt 编排，替代旧 subprocess wrapper）。

run1 (var_persistence_run1.py): set vars + freeze（持久化 _VARS）
run2 (var_persistence_run2.py): load_db + verify vars 恢复

db 在 case log 目录（自动随 case 清），两 sub case 经 env 共享 FLY_VAR_PERSISTENCE_DB。
"""
import os, shutil

DB_PATH = os.path.join(FLY_CASE_LOG_DIR, "persistence_db")
shared_env = {"FLY_VAR_PERSISTENCE_DB": DB_PATH}

def setup_clean():
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)

run_subcase("var_persistence_run1.py", timeout=120, setup=setup_clean, env=shared_env)
run_subcase("var_persistence_run2.py", timeout=120, env=shared_env)
INFO("[PASS] test_var_persistence")
