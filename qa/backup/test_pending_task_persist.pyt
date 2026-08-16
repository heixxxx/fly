"""failed_tasks.bin 持久化 → 跨进程 restart 恢复（.pyt 两段式编排）。

run1 (pending_persist_run1.py): 提交任务（部分完成、部分失败：unresolvable dep + 缺 gpu 属性），
  停止前验证 failed_tasks.bin 落盘。
run2 (pending_persist_run2.py): 新 master + gpu worker，restart_failed_tasks 重放，
  验证 gpu 任务补齐完成。

2026-08-16 冗余清理重建：此前三个 .pyt（test_backup_data / test_backup_load_db_multi_worker /
test_pending_task_persist）的 sub case 是同一脚本的 md5 副本（名不副实），本 case 是
pending_persist 语义的正确两段式。
"""
import os, shutil

DB_PATH = os.path.join(FLY_CASE_LOG_DIR, "db_pending")

def setup_clean():
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)

r1 = run_subcase("pending_persist_run1.py", timeout=120, setup=setup_clean,
                 env={"FLY_DB_PATH": DB_PATH})
run1_log = os.path.dirname(r1.log_path)
failed_bin = os.path.join(run1_log, "failed_tasks.bin")
assert os.path.isfile(failed_bin), "run1 should leave failed_tasks.bin in its log dir"

run_subcase("pending_persist_run2.py", timeout=120,
            env={"FLY_RUN1_LOG_DIR": run1_log})
INFO("[PASS] test_pending_task_persist: persist + cross-process restart verified")
