# fatal message 场景（子进程隔离断言退出码 80）：master fast_exit + StopNow
# 停 worker 耗时较长，放宽 subcase 超时。
run_subcase("test_fatal_exit.py", timeout=240)
