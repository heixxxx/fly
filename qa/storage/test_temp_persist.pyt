"""temp 落盘跨进程恢复 e2e（task 级断点基建）。

temp 对象（save_to_db=False）从纯内存（TempStore LRU）改为"内存 LRU + db 目录
专用文件"（temp_data_{wid}_{NNN}.dat + {wid}.temp.idx）。本用例验证：
  run1: worker 写 temp + 正式对象（不 freeze）。
  run2: load_db 跨进程恢复 temp（IdxLoad 加载 .temp.idx → 下游可读）+ freeze
        后 temp 文件清理。

单测侧对应：TempPersistRoundtripAcrossRestart / TempAbortRollsBackFiles /
TempFreezeDeletesFiles / TempIdxUnclosedSegmentDropped（database_test）。
"""
import os, shutil

DB_PATH = os.path.join(FLY_CASE_LOG_DIR, "db_temp")

def setup_clean():
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)

run_subcase("temp_persist_run1.py", timeout=120, setup=setup_clean,
            env={"FLY_DB_PATH": DB_PATH})
run_subcase("temp_persist_run2.py", timeout=120,
            env={"FLY_DB_PATH": DB_PATH})
INFO("[PASS] test_temp_persist: temp persisted to db dir + restored across processes")
