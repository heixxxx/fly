"""大对象+多对象跨文件失败 → load_db + restart（.pyt 编排，替代旧 subprocess wrapper）。

run1 (mixed_fail_run1.py): mixed write + fail（FLY_MIXED_FAIL=1），大对象 rollover + abort 清理
run2 (mixed_fail_run2.py): load_db + restart（FLY_MIXED_FAIL=0，读 run1 的 failed_tasks.bin）

run2 经 env FLY_RUN1_LOG_DIR 定位 run1 的 failed_tasks.bin。db 在 case log 目录。
"""
import os, shutil

DB_PATH = os.path.join(FLY_CASE_LOG_DIR, "db_mixed")

def setup_clean():
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)

r1 = run_subcase("mixed_fail_run1.py", timeout=120, setup=setup_clean,
                 env={"FLY_DB_PATH": DB_PATH, "FLY_MIXED_FAIL": "1"})
run1_log = os.path.dirname(r1.log_path)
run_subcase("mixed_fail_run2.py", timeout=120,
            env={"FLY_DB_PATH": DB_PATH, "FLY_MIXED_FAIL": "0", "FLY_RUN1_LOG_DIR": run1_log})
INFO("[PASS] test_mixed_write_fail: cross-file abort + load_db + restart verified")
