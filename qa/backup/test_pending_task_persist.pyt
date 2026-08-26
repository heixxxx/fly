"""failed_tasks.bin 持久化 → 跨进程 restart 恢复（.pyt 两段式编排）。

run1 (pending_persist_run1.py): 提交任务（部分完成、部分失败：unresolvable dep + 缺 gpu 属性），
  停止前验证 failed_tasks.bin 按归属 db 落盘（{db_path}/failed_tasks.bin）。
run2 (pending_persist_run2.py): 新 master + gpu worker，restart_failed_tasks（传 db 路径自动
  搜索归属 bin）重放，验证 gpu 任务补齐完成。
"""
import os, shutil

DB_PATH = os.path.join(FLY_CASE_LOG_DIR, "db_pending")

def setup_clean():
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)

r1 = run_subcase("pending_persist_run1.py", timeout=120, setup=setup_clean,
                 env={"FLY_DB_PATH": DB_PATH})
failed_bin = os.path.join(DB_PATH, "failed_tasks.bin")
assert os.path.isfile(failed_bin), "run1 should leave failed_tasks.bin in its owner db dir"

run_subcase("pending_persist_run2.py", timeout=120,
            env={"FLY_DB_PATH": DB_PATH})
INFO("[PASS] test_pending_task_persist: persist + cross-process restart verified")
