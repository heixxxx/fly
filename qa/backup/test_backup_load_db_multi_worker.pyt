"""双虚拟 host 写+backup → load_db 分布式读（.pyt 两段式编排）。

run1 (backup_load_db_multi_worker_run1.py): host-alpha/host-beta 两 worker 各自写数据
  （backup_threshold=1，跨 host 副本），写 _test_db_path marker。
run2 (backup_load_db_multi_worker_run2.py): 新进程 load_db，按 hostname 分配 idx，
  验证跨 host backup 副本与 host-alpha 本地数据都可读。

2026-08-16 冗余清理重建：恢复此前丢失的两段式场景（旧 .pyt 误指向 pending_persist 副本）。
"""
import os, shutil

DB_PATH = os.path.join(FLY_CASE_LOG_DIR, "db_backup_multi")

def setup_clean():
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)

run_subcase("backup_load_db_multi_worker_run1.py", timeout=120, setup=setup_clean,
            env={"FLY_DB_PATH": DB_PATH})
assert os.path.isfile(os.path.join(DB_PATH, "_test_db_path")), "marker should exist after run1"

run_subcase("backup_load_db_multi_worker_run2.py", timeout=120,
            env={"FLY_DB_PATH": DB_PATH})
INFO("[PASS] test_backup_load_db_multi_worker: cross-host backup + load_db verified")
