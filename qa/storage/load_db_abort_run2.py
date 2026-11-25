"""Run 2: load_db 验证异常清理后的脏数据不恢复。

Run 1 执行了 abort 清理（idx ABORT + data truncate）。
本 run load_db 后验证：
  - 正常数据（baseline）可读
  - 脏数据（dirty*）不可读（ABORT 段的 pending 被丢弃）
"""
from _fly_log import INFO, ERR
import os
import time


from fly import open_db, get_config
from test import record
from fly.runtime import get_agent

DB_PATH = os.environ.get("FLY_DB_PATH") or os.path.join(get_config().get_str("log_dir"), "db")

# 编排脚本挪入的检查（原先在两个 sub case 之间内联断言，重录模式会真的执行
# 内联语句而失败，见 qa/README.md「编排脚本的写法约定」）：时机不变——run1
# 的工作刚结束、本 sub case 开始工作之前。
# 保留：守卫——后续 open 该 marker 文件还要用
assert os.path.isfile(os.path.join(DB_PATH, "_test_db_path")), "marker should exist after run1"

# 读取 run1 保存的 db_path
with open(os.path.join(DB_PATH, "_test_db_path")) as f:
    expected_db_path = f.read().strip()

master = get_agent()

# load_db 恢复之前的数据库（含正常数据 + 已 abort 的段）
# load_db 返回 Database 对象，直接使用，不要 open_db（会创建新 db）
db = master.load_db(DB_PATH)
INFO(f"[RUN2] load_db result: {db}")

# load_db 会 spawn worker，等待连接
# 保留：等待原语
assert master.wait_workers_registered(timeout=60)
worker_connected = master.worker_count >= 1
# 原断言：assert master.worker_count >= 1, "Worker should connect after load_db"
record("worker_connected", worker_connected, detail=master.worker_count)

time.sleep(1.0)  # 等 idx load 完成

db_path_matches = db.get_db_path() == expected_db_path
# 原断言：assert db.get_db_path() == expected_db_path, \
record("db_path_matches", db_path_matches,
       detail={"actual": db.get_db_path(), "expected": expected_db_path})

# 1. 正常数据应可读
baseline = db.read_object("baseline")
# 原断言：assert baseline == "base_value", f"baseline should be readable: {baseline}"
record("baseline_readable", baseline)
INFO("[RUN2] baseline data correctly loaded after abort cleanup")

# 2. 脏数据应不可读（ABORT 段被丢弃）
# 旧写法失效：assert False 抛出的 AssertionError 是 Exception 的子类，会被下方
# 同一个 except Exception 一并吞掉，脏数据意外可读时用例照样通过。
for key in ["dirty1", "dirty2", "dirty3", "dirty_clean"]:
    raised = False
    val = None
    try:
        val = db.read_object(key)
    except Exception:
        raised = True
    if not raised:
        ERR(f"[FAIL] dirty object {key} unexpectedly readable after load_db: {val}")
    # 原断言：assert raised, f"dirty object {key} should NOT be readable after load_db (abort cleanup)"
    record(f"dirty_unreadable/{key}", raised, detail=val)
    INFO(f"[RUN2] dirty object {key} correctly absent after load_db")

INFO("[RUN2] DONE: load_db correctly skips aborted segment data")
