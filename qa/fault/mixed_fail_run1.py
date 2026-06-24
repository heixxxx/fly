"""Run 1: 混合大小对象写入后失败 → 持久化。

task 写入：3 个小对象 + 1 个大对象(>1MB, 触发 rollover) + 2 个小对象 + dirty，
然后抛异常。abort 清理：删除 rollover 产生的 .dat + truncate 原 .dat + idx ABORT。
"""
from _fly_log import INFO, ERR
import os
import time

from e2e_tasks import mixed_size_write_fail
from fly import open_db, get_config

DB_PATH = os.environ.get("FLY_DB_PATH") or os.path.join(get_config().get_str("log_dir"), "db")

import shutil
if os.path.isdir(DB_PATH):
    shutil.rmtree(DB_PATH, ignore_errors=True)

from fly.runtime import get_agent

# 设小 aggregation_threshold（100KB），让 50000 元素的 list（~400KB pickle）
# 必然触发 rollover，同时保持数据量小、传输快
get_config().set_int("aggregation_threshold", 102400)

master = get_agent()

master.launch_local_workers([{}])
for _ in range(40):
    if master.worker_count >= 1:
        break
    time.sleep(0.5)
assert master.worker_count >= 1, "Worker should connect"

db = open_db(DB_PATH)
db_id = db.get_db_id()

# 提交混合大小写入 task（3 小 + 1 大(200000 元素 ~1.6MB) + 2 小 + dirty），然后失败
mixed_size_write_fail(db, 3, 50000, True)

INFO("[RUN1] Submitted mixed_size_write_fail")

# 等待失败
def wait_failed(timeout=15.0):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if master.failed_tasks:
            return True
        time.sleep(0.3)
    return False

assert wait_failed(), "task should fail"
INFO(f"[RUN1] task failed: {master.failed_tasks}")

time.sleep(0.5)  # 等 abort 完成

# 验证所有脏对象不可读（被 abort 清理）
dirty_keys = ["mixed/small_0", "mixed/small_1", "mixed/small_2",
              "mixed/large", "mixed/after_large_0", "mixed/after_large_1",
              "mixed/dirty"]
for key in dirty_keys:
    try:
        db.read_object(key)
        ERR(f"[RUN1] WARN: {key} unexpectedly readable after abort")
    except Exception:
        pass
INFO("[RUN1] all dirty objects cleaned by abort")

# 验证 failed_tasks.bin 持久化
failed_file = os.path.join(get_config().get_str("log_dir"), "failed_tasks.bin")
assert os.path.isfile(failed_file), f"failed_tasks.bin should exist: {failed_file}"
INFO(f"[RUN1] failed_tasks.bin persisted")

with open(os.path.join(DB_PATH, "_test_db_id"), "w") as f:
    f.write(db_id)

INFO(f"[RUN1] db_id={db_id}, exiting for run2")
