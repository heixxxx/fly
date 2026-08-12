"""remove_object 多阶段验证（.pyt 编排，替代旧 subprocess wrapper）。

phase1 (remove_obj_phase1.py): write+remove 基本
phase2 (remove_obj_phase2.py): remove 后依赖 task 失败
phase3 (remove_obj_phase3.py): remove 一个，另一个可读

每个 phase 独立 db（各自 sub_log_dir/db），不共享。
"""
run_subcase("remove_obj_phase1.py", timeout=60)
run_subcase("remove_obj_phase2.py", timeout=60)
run_subcase("remove_obj_phase3.py", timeout=60)
INFO("[PASS] test_remove_object: all phases passed")
