"""Dynamic 多右端项连续求解（EmIR dynamic IR drop 场景）e2e。

subcase：
  1. rasgd_basic.py         基础：T=3 全程 + 数值/warm start/缓存复用/controller 数据流
  2. rasgd_early_stop.py    提前终止：update_rhs 返回 None
  3. rasgd_restart_run1.py  失败注入中途挂（组原子传染 + 结果保留 + bin 落盘）
  4. rasgd_restart_run2.py  全新 run 断点续跑（temp 恢复 + 重投 + 链自恢复）
"""
import os
import shutil

RESTART_DB = os.path.join(FLY_CASE_LOG_DIR, "db_restart")


def _clean_restart_db():
    if os.path.isdir(RESTART_DB):
        shutil.rmtree(RESTART_DB, ignore_errors=True)


run_subcase("rasgd_basic.py", timeout=240)
run_subcase("rasgd_early_stop.py", timeout=240)

run_subcase("rasgd_restart_run1.py", timeout=300, setup=_clean_restart_db,
            env={"FLY_RASG_FAIL_AT": "2:1", "FLY_DB_PATH": RESTART_DB})
run_subcase("rasgd_restart_run2.py", timeout=300,
            env={"FLY_DB_PATH": RESTART_DB})

INFO("[PASS] test_ras_graph_dynamic")
