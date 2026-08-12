"""10MB 对象远程传输 + 内存拷贝 profiling（单进程 case，.pyt 包装）。

master launch 2 worker（gpu/cpu），gpu 写大对象，cpu 远程读（触发 wire transfer）。
原 test_*.py 退为 sub case（launch_local_workers 内的 Popen 是 worker 启动，非多阶段编排，
故非复合 wrapper——脚本因 Popen 关键字误判跳过，此 .pyt 手动补单 sub case 包装）。
"""
run_subcase("test_large_transfer_profile.py", timeout=120)
