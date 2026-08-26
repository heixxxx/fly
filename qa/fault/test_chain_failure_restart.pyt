"""依赖链中途失败 → 持久化 → 跨进程 restart 恢复（.pyt 编排，替代旧 subprocess wrapper）。

run1 (chain_fail_run1.py): 提交 DAG，node7 失败（FLY_FAIL_NODES=7），下游连锁失败，
     failed_tasks.bin 按归属落 db 目录
run2 (chain_fail_run2.py): load_db 恢复 stage0/1 + restart_failed_tasks（读归属 db 目录的 bin）

run1/run2 共享 DB_PATH；失败记录按归属 db 落盘（Task db 归属规则），跨进程不依赖 log_dir。
"""
import os, shutil

DB_PATH = os.path.join(FLY_CASE_LOG_DIR, "db_chain")

def setup_clean():
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)

r1 = run_subcase("chain_fail_run1.py", timeout=120, setup=setup_clean,
                 env={"FLY_DB_PATH": DB_PATH, "FLY_FAIL_NODES": "7", "FLY_DOWNSTREAM_SLEEP": "2.0"})
assert os.path.isfile(os.path.join(DB_PATH, "_test_db_path")), "marker should exist after run1"

run_subcase("chain_fail_run2.py", timeout=120,
            env={"FLY_DB_PATH": DB_PATH})
INFO("[PASS] test_chain_failure_restart: chain failure + cross-process restart verified")
