"""project task 级断点重跑 e2e。

断点粒度 = task 级（用户裁定）：不检测已完成 task（对象驱动调度），恢复 =
重投 failed_tasks.bin 的 FAILED/PENDING/RUNNING。失败记录按归属 db 落盘
（{db_path}/failed_tasks.bin，project 场景 db 目录在 project 下，自包含随迁移）。

run1 (project_resume_run1.py): project + 6 task，2 个完成后 SIGTERM 优雅中断
     （fast_exit persist RUNNING + 尾部 persist PENDING）→ bin 落归属 db 目录。
     预期被信号终止（expect_pass=False）。
run2 (project_resume_run2.py): load_project → 已完成对象 ready → resume() 遍历
     各 db 目录重投未完成 task → 全部完成且值正确 → bin 清空。

单测侧对应：FailedTasksFilePerOwnerPath（master_agent_test）。
"""
import os, shutil

PROJ_PATH = os.path.join(FLY_CASE_LOG_DIR, "proj_resume")

def setup_clean():
    if os.path.isdir(PROJ_PATH):
        shutil.rmtree(PROJ_PATH, ignore_errors=True)

r1 = run_subcase("project_resume_run1.py", timeout=120, setup=setup_clean,
                 env={"FLY_PROJ_PATH": PROJ_PATH})
assert os.path.isfile(os.path.join(PROJ_PATH, "workdb", "failed_tasks.bin")), \
    "failed_tasks.bin should persist into the owner db dir on graceful interrupt"

run_subcase("project_resume_run2.py", timeout=180,
            env={"FLY_PROJ_PATH": PROJ_PATH})
INFO("[PASS] test_project_resume: task-level resume verified")
