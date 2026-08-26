"""迁移后断点恢复 e2e：路径快照失真的 uid 自愈全链路验收。

run1 (migrate_resume_run1.py): project db（分离 data_path）+ 失败 task
     （unresolvable dep）→ bin 落 {old}/workdb/。
run2 (migrate_resume_run2.py): migrate_project 搬迁 → meta/_DB_META 的
     db_path/data_path 改写断言，旧根消失。
run3 (migrate_resume_run3.py): load_project → 补依赖 → resume → 重投完成、
     对象正确、bin 删除、旧路径无幽灵目录、数据落新 data_path。

单测侧对应：RestartResolvesDbByUid / RestartAtomicOnUnresolvedUid。
"""
import os, shutil

PROJ_PATH = os.path.join(FLY_CASE_LOG_DIR, "proj_mres")
NEW_PATH = os.path.join(FLY_CASE_LOG_DIR, "proj_mres_migrated")

def setup_clean():
    for p in (PROJ_PATH, NEW_PATH):
        if os.path.isdir(p):
            shutil.rmtree(p, ignore_errors=True)

run_subcase("migrate_resume_run1.py", timeout=120, setup=setup_clean,
            env={"FLY_PROJ_PATH": PROJ_PATH})
run_subcase("migrate_resume_run2.py", timeout=120,
            env={"FLY_PROJ_PATH": PROJ_PATH, "FLY_NEW_PROJ_PATH": NEW_PATH})
run_subcase("migrate_resume_run3.py", timeout=180,
            env={"FLY_NEW_PROJ_PATH": NEW_PATH, "FLY_OLD_PROJ_PATH": PROJ_PATH})
INFO("[PASS] test_migrate_resume: migration + uid-resolved resume verified")
