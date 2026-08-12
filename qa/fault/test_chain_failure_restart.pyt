"""依赖链中途失败 → 持久化 → 跨进程 restart 恢复（.pyt 编排，替代旧 subprocess wrapper）。

run1 (chain_fail_run1.py): 提交 DAG，node7 失败（FLY_FAIL_NODES=7），下游连锁失败，持久化 failed_tasks.bin
run2 (chain_fail_run2.py): load_db 恢复 stage0/1 + restart_failed_tasks（读 run1 的 failed_tasks.bin）

run2 经 env FLY_RUN1_LOG_DIR 拿 run1 的 log_dir，定位 failed_tasks.bin（.pyt 的 sub case 各有独立 log_dir）。
db 在 case log 目录。
"""
import os, shutil

DB_PATH = os.path.join(FLY_CASE_LOG_DIR, "db_chain")

def setup_clean():
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)

r1 = run_subcase("chain_fail_run1.py", timeout=120, setup=setup_clean,
                 env={"FLY_DB_PATH": DB_PATH, "FLY_FAIL_NODES": "7", "FLY_DOWNSTREAM_SLEEP": "2.0"})
assert os.path.isfile(os.path.join(DB_PATH, "_test_db_path")), "marker should exist after run1"

run1_log = os.path.dirname(r1.log_path)
run_subcase("chain_fail_run2.py", timeout=120,
            env={"FLY_DB_PATH": DB_PATH, "FLY_RUN1_LOG_DIR": run1_log})
INFO("[PASS] test_chain_failure_restart: chain failure + cross-process restart verified")
