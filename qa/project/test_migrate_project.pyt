"""project 整体迁移 e2e（目录搬迁模式）。

migrate_project(path, new_path)：meta db_path 改写 + _DB_CHAIN 邻居边更新
（uid 不变）+ failed_tasks.bin 随迁。不要求 frozen（半成品 db 靠 idx 事务段
保证搬后可恢复）。无任何超时参数（数据量与集群 IO 不可预估）。

run1 (migrate_run1.py): matrix → solve 两 db（chain 边），写数据 freeze。
run2 (migrate_run2.py): migrate_project 搬到新路径 + meta/chain 改写断言。
run3 (migrate_run3.py): load_project 新路径 → 数据可读 + find_db 沿新边定位。
"""
import os, shutil

PROJ_PATH = os.path.join(FLY_CASE_LOG_DIR, "proj_migrate")
NEW_PATH = os.path.join(FLY_CASE_LOG_DIR, "proj_migrated")

def setup_clean():
    for p in (PROJ_PATH, NEW_PATH):
        if os.path.isdir(p):
            shutil.rmtree(p, ignore_errors=True)

run_subcase("migrate_run1.py", timeout=120, setup=setup_clean,
            env={"FLY_PROJ_PATH": PROJ_PATH})
run_subcase("migrate_run2.py", timeout=60,
            env={"FLY_PROJ_PATH": PROJ_PATH, "FLY_NEW_PROJ_PATH": NEW_PATH})
run_subcase("migrate_run3.py", timeout=120,
            env={"FLY_NEW_PROJ_PATH": NEW_PATH})
INFO("[PASS] test_migrate_project: directory migration + meta/chain rewrite verified")
