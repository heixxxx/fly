"""Task db 归属规则 e2e：失败记录按归属 db 落盘 + restart_failed_tasks(db list) 自动搜索。

run1 (task_owner_run1.py): 双 db（stage_a=自动推导归属 / stage_b=显式 owner 覆盖），
     各 1 成功 + 1 失败（unresolvable dep，确定性）→ 断言两个 {db}/failed_tasks.bin
     分散落盘且互不混入。
run2 (task_owner_run2.py): load_project → 补依赖 → restart_failed_tasks(db_path list)
     → 重投 2 个 → 全部对象就绪 → 两个 bin 均删除。

单测侧对应：FailedTasksFilePerOwnerPath / OwnerDbPathSurviveRoundTrip。
"""
import os, shutil

PROJ_PATH = os.path.join(FLY_CASE_LOG_DIR, "proj_task_owner")

def setup_clean():
    if os.path.isdir(PROJ_PATH):
        shutil.rmtree(PROJ_PATH, ignore_errors=True)

run_subcase("task_owner_run1.py", timeout=120, setup=setup_clean,
            env={"FLY_PROJ_PATH": PROJ_PATH})
run_subcase("task_owner_run2.py", timeout=180,
            env={"FLY_PROJ_PATH": PROJ_PATH})
INFO("[PASS] test_task_owner: per-owner persist + db-list restart verified")
