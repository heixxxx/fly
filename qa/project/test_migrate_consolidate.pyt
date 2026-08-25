"""project 迁移 consolidate 组合 e2e：跨 host 数据集中（逐 db merge_db，要求
全 frozen）→ 目录搬迁 → 新位置 load 可读。

migrate_project(path, new_path, consolidate=True)：数据自包含化（.merged_data
落在 project 目录内随迁）。全程无超时（数据量与集群 IO 不可预估）。
"""
import os, shutil

PROJ_PATH = os.path.join(FLY_CASE_LOG_DIR, "proj_consol")
NEW_PATH = os.path.join(FLY_CASE_LOG_DIR, "proj_consol_migrated")

def setup_clean():
    for p in (PROJ_PATH, NEW_PATH):
        if os.path.isdir(p):
            shutil.rmtree(p, ignore_errors=True)

run_subcase("migrate_consol_run1.py", timeout=120, setup=setup_clean,
            env={"FLY_PROJ_PATH": PROJ_PATH})
run_subcase("migrate_consol_run2.py", timeout=180,
            env={"FLY_PROJ_PATH": PROJ_PATH, "FLY_NEW_PROJ_PATH": NEW_PATH})
run_subcase("migrate_consol_run3.py", timeout=120,
            env={"FLY_NEW_PROJ_PATH": NEW_PATH})
INFO("[PASS] test_migrate_consolidate: cross-host consolidation + migration verified")
