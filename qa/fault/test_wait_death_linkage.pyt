"""无限等待的判死联动 e2e：数据规模相关等待已无超时（load_db 屏障 / merge wait /
delete ack / cleanup 屏障），无限等待只能被显式失败信号终结——worker 判死联动
（settle_pending_for_dead_worker）是安全性前提。

run1 (wait_linkage_run1.py): 独立 host worker 写 20 对象 + freeze。
run2 (wait_linkage_run2.py): load_db（无限屏障）进行中 SIGKILL 加载 worker →
     断连即死（reconnect=0）→ settle 置 -1 → load_db 显式 RuntimeError 而非死等。

单测侧对应：WorkerDeathSettlesPendingRpc（master_agent_test）、
WaitForNonPositiveTimeoutWaitsForever（pending_rpc_map_test）。
"""
import os, shutil

DB_PATH = os.path.join(FLY_CASE_LOG_DIR, "db_linkage")

def setup_clean():
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)

run_subcase("wait_linkage_run1.py", timeout=120, setup=setup_clean,
            env={"FLY_DB_PATH": DB_PATH})
assert os.path.isfile(os.path.join(DB_PATH, "_FROZEN")), "db should be frozen after run1"

run_subcase("wait_linkage_run2.py", timeout=120,
            env={"FLY_DB_PATH": DB_PATH})
INFO("[PASS] test_wait_death_linkage: infinite wait settled explicitly by worker death")
