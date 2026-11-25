"""frozen db 两进程生命周期（.pyt 编排，救活孤儿脚本对）。

run1 建库 + worker 写 3 个对象 + freeze；run2 load_db 后仍冻结、写入被拒。
db 在 case log 目录（自动随 case 清）；两个子阶段经环境变量 FLY_DB_PATH 共享
数据库路径（写法参照 test_load_db_two_processes.pyt）。
"""
import os, shutil

db_path = os.path.join(FLY_CASE_LOG_DIR, "frozen_db_p2")
shared_env = {"FLY_DB_PATH": db_path}

def setup_clean():
    if os.path.isdir(db_path):
        shutil.rmtree(db_path, ignore_errors=True)

run_subcase("frozen_db_run1.py", timeout=60, setup=setup_clean, env=shared_env,
            check_list=["result.json"])

run_subcase("frozen_db_run2.py", timeout=60, env=shared_env,
            check_list=["result.json"])
INFO("[PASS] test_frozen_db_load_two_processes: two-process frozen lifecycle verified")
