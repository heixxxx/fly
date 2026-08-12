"""@wait_obj 多场景验证（.pyt 编排，替代旧 subprocess wrapper）。

5 个独立 sub case（各一个 @wait_obj 场景），循环 run_subcase。
每个 helper 独立 db（各自 sub_log_dir/db），不共享。
"""
PHASES = [
    ("Master write → immediate read", "wait_obj_p1_master.py"),
    ("Worker write → block & read", "wait_obj_p2_worker.py"),
    ("Multi deps → wait for all", "wait_obj_p3_multi.py"),
    ("Timeout → phantom data", "wait_obj_p4_timeout.py"),
    ("Cross-worker @wait_obj (2 workers)", "wait_obj_p5_inside_task.py"),
]

for name, script in PHASES:
    INFO(f"── {name}: {script} ──")
    run_subcase(script, timeout=120)
INFO("All wait_obj E2E tests passed!")
