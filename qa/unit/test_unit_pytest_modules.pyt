"""qa/unit 下 pytest 风格模块的自动化执行（smoke_test.py、storage_test.py）。

这两个模块从入库起就没有被任何自动化流程执行过；2026-11-26 起由本用例调度，
每个模块各起一个独立的 pytest 进程，逐个测试函数的结果由运行器与基准文件比对。
"""
run_subcase("test_unit_pytest_modules.py", timeout=120, check_list=["result.json"])
INFO("[DONE] test_unit_pytest_modules")
