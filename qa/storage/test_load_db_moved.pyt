"""load_db moved-DB 生命周期（.pyt 编排，替代旧 wrapper 场景3）。

run1 建 db 在 path_a，move 到 path_b，run2 从 path_b load_db。db 在 case log 目录。
"""
import os, shutil

path_a = os.path.join(os.environ["FLY_CASE_LOG_DIR"], "load_db_p3_run1")
path_b = os.path.join(os.environ["FLY_CASE_LOG_DIR"], "load_db_p3_run2")

def setup_clean():
    for p in [path_a, path_b]:
        if os.path.isdir(p):
            shutil.rmtree(p, ignore_errors=True)

run_subcase("load_db_run1_moved.py", timeout=60, setup=setup_clean, env={"FLY_DB_PATH": path_a})
assert os.path.isdir(path_a), "DB should exist at path_a after run1"
assert os.path.isfile(os.path.join(path_a, "_DB_META")), "_DB_META should exist"

# move A → B（编排层做，sub case 间）
shutil.move(path_a, path_b)
assert os.path.isdir(path_b), "DB should exist at path_b after move"
assert not os.path.isdir(path_a), "DB should NOT exist at path_a after move"

run_subcase("load_db_run2_moved.py", timeout=60, env={"FLY_DB_PATH": path_b})
assert os.path.isfile(os.path.join(path_b, "_FROZEN")), "should be frozen after run2"
INFO("[PASS] test_load_db_moved_db: moved-DB lifecycle verified")
