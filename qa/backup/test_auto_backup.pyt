# 自动生成：单进程 case 包装（原 test_auto_backup.py 退为 sub case）。
# 若此 case 实际是多阶段复合场景，改为手动编排多 run_subcase。
run_subcase("test_auto_backup.py", timeout=60)
