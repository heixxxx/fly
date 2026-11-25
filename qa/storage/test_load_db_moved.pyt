"""load_db moved-DB 生命周期（.pyt 编排，替代旧 wrapper 场景3）。

run1 建 db 在 path_a，move 到 path_b，run2 从 path_b load_db。db 在 case log 目录。
"""
import os, shutil

path_a = os.path.join(FLY_CASE_LOG_DIR, "load_db_p3_run1")
path_b = os.path.join(FLY_CASE_LOG_DIR, "load_db_p3_run2")

def setup_clean():
    for p in [path_a, path_b]:
        if os.path.isdir(p):
            shutil.rmtree(p, ignore_errors=True)

def setup_move():
    # 编排脚本挪入的检查与文件操作（原先是 sub case 之间的内联语句和断言，
    # 重录模式会真的执行它们而失败，见 qa/README.md「编排脚本的写法约定」）：
    # 全部放在 run2 的 setup 里，时机不变——run1 的工作刚结束（产物检查）、
    # 搬移、本 sub case 开始工作之前。
    assert os.path.isdir(path_a), "DB should exist at path_a after run1"
    assert os.path.isfile(os.path.join(path_a, "_DB_META")), "_DB_META should exist"
    # move A → B
    shutil.move(path_a, path_b)
    assert os.path.isdir(path_b), "DB should exist at path_b after move"
    assert not os.path.isdir(path_a), "DB should NOT exist at path_a after move"

run_subcase("load_db_run1_moved.py", timeout=60, setup=setup_clean, env={"FLY_DB_PATH": path_a})
run_subcase("load_db_run2_moved.py", timeout=60, setup=setup_move, env={"FLY_DB_PATH": path_b})
INFO("[PASS] test_load_db_moved_db: moved-DB lifecycle verified")
