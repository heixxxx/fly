# 自动生成：单进程 case 包装（原 test_peer_stream_read_perf.py 退为 sub case）。
# 若此 case 实际是多阶段复合场景，改为手动编排多 run_subcase。
import os
# PEER_RPC_PERF_FULL=1 开启加重模式（含 512MB 大档，明显更耗时）：-t 对 .pyt
# 用例不起作用，加重模式的时限只能由包装自己给——加重 180 秒，缺省 60 秒。
timeout = 180 if os.environ.get("PEER_RPC_PERF_FULL") == "1" else 60
run_subcase("test_peer_stream_read_perf.py", timeout=timeout)
