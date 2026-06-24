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
from fly.runtime import get_agent

DB_PATH = os.environ.get("FLY_DB_PATH") or os.path.join(get_config().get_str("log_dir"), "db")

# 读取 run1 保存的 db_id
with open(os.path.join(DB_PATH, "_test_db_id")) as f:
    expected_db_id = f.read().strip()

master = get_agent()

# load_db 恢复之前的数据库（含正常数据 + 已 abort 的段）
# load_db 返回已 set_db_id 的 Database 对象，直接使用，不要 open_db（会创建新 db）
db = master.load_db(DB_PATH)
INFO(f"[RUN2] load_db result: {db}")

# load_db 会 spawn worker，等待连接
for _ in range(40):
    if master.worker_count >= 1:
        break
    time.sleep(0.5)
assert master.worker_count >= 1, "Worker should connect after load_db"

time.sleep(1.0)  # 等 idx load 完成

assert db.get_db_id() == expected_db_id, \
    f"db_id mismatch: {db.get_db_id()} != {expected_db_id}"

# 1. 正常数据应可读
baseline = db.read_object("baseline")
assert baseline == "base_value", f"baseline should be readable: {baseline}"
INFO("[RUN2] baseline data correctly loaded after abort cleanup")

# 2. 脏数据应不可读（ABORT 段被丢弃）
for key in ["dirty1", "dirty2", "dirty3", "dirty_clean"]:
    try:
        val = db.read_object(key)
        ERR(f"[FAIL] dirty object {key} unexpectedly readable after load_db: {val}")
        assert False, f"dirty object {key} should NOT be readable after load_db (abort cleanup)"
    except Exception:
        INFO(f"[RUN2] dirty object {key} correctly absent after load_db")

INFO("[RUN2] PASS: load_db correctly skips aborted segment data")
