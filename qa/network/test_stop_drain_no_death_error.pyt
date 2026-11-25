# 自动生成：单进程 case 包装（原 test_stop_drain_no_death_error.py 退为 sub case）。
# 若此 case 实际是多阶段复合场景，改为手动编排多 run_subcase。
run_subcase("test_stop_drain_no_death_error.py", timeout=60, check_list=["result.json"])
