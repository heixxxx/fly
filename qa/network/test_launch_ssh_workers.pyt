# 手工薄包装（原脚本含子进程调用，迁移脚本跳过）：原 test_launch_ssh_workers.py
# 整体作为一个 sub case，行为不变。
run_subcase("test_launch_ssh_workers.py", timeout=60, check_list=["result.json"])
