"""fly.Project 基类机制验证（.pyt 包装，统一发现）。

test_project.py 含 4 test 函数（3 单进程 + 1 两进程 load_project）。
两进程部分（test_load_project_two_processes）内部仍用 subprocess 跑 run1/run2
（fly 内 subprocess）——因 4 函数共享 imports/helper（DemoProject/make_db/_wait_completed），
拆分为独立 sub case 脚本成本高，故整文件作为单 sub case 包装（run_subcase 跑 test_project.py）。
后续如需真拆，可分离 test_load_project_two_processes 为独立 .pyt 多 sub case。
"""
run_subcase("test_project.py", timeout=180)
