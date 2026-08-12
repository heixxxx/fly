"""异常清理后的 load_db 验证（.pyt 编排，替代旧 subprocess wrapper）。

run1 (load_db_abort_run1.py): baseline + abort dirty task（idx ABORT + data truncate）
run2 (load_db_abort_run2.py): load_db + verify baseline 可读、dirty* 不可读

db 在 case log 目录，两 sub case 经 env 共享 FLY_DB_PATH。
"""
import os, shutil

DB_PATH = os.path.join(FLY_CASE_LOG_DIR, "db_abort")
shared_env = {"FLY_DB_PATH": DB_PATH}

def setup_clean():
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)

run_subcase("load_db_abort_run1.py", timeout=120, setup=setup_clean, env=shared_env)
assert os.path.isfile(os.path.join(DB_PATH, "_test_db_path")), "marker should exist after run1"

run_subcase("load_db_abort_run2.py", timeout=120, env=shared_env)
INFO("[PASS] test_load_db_abort: abort cleanup + load_db verified")
