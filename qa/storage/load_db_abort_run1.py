"""Run 1: 异常清理后验证 data 文件 truncate + idx ABORT 持久化。

task 写入若干对象后抛异常 → worker 执行 abort_task_writes：
  - idx 打 ABORT 标记
  - data 文件 truncate 回滚点
本 run 验证 data 文件确实被 truncate（磁盘占用减小）。
"""
from _fly_log import INFO
import os
import glob


from test import partial_write_then_fail, write_data
from fly import open_db, get_config

DB_PATH = os.environ.get("FLY_DB_PATH") or os.path.join(get_config().get_str("log_dir"), "db")

import shutil
if os.path.isdir(DB_PATH):
    shutil.rmtree(DB_PATH, ignore_errors=True)

from fly.runtime import get_agent

master = get_agent()
master.launch_local_workers([{}])
assert master.wait_workers_registered(timeout=60)
assert master.worker_count >= 1, "Worker should connect"

db = open_db(DB_PATH)

# 1. 先写一个正常 task（产生段外 ADD 的基准数据 + 已提交段）
write_data(db, "baseline", "base_value")
from test import wait_until
assert wait_until(lambda: len(master.completed_tasks) >= 1, timeout=20), \
    "baseline write should complete"

# 记录 baseline 完成后 data 文件大小
dat_files = glob.glob(os.path.join(DB_PATH, "data_*.dat"))
baseline_size = sum(os.path.getsize(f) for f in dat_files)
INFO(f"[RUN1] baseline data size after normal task: {baseline_size} bytes, files={len(dat_files)}")

# 2. 失败 task：写入 dirty 对象后抛异常（应被 abort 清理）
partial_write_then_fail(db, ["dirty1", "dirty2", "dirty3"], "dirty_clean", "crash_recovery_test")

assert wait_until(lambda: len(master.failed_tasks) >= 1, timeout=20), \
    "partial_write_then_fail should fail"


# 等 abort 完成：data 文件大小回落到 baseline（事件驱动，替代固定 sleep）
def _abort_cleanup_done():
    dat = glob.glob(os.path.join(DB_PATH, "data_*.dat"))
    return sum(os.path.getsize(f) for f in dat) <= baseline_size + 1


assert wait_until(_abort_cleanup_done, timeout=10), \
    "abort cleanup did not shrink data file within 10s"

# 3. 验证 data 文件被 truncate：abort 后大小应 <= baseline
dat_files_after = glob.glob(os.path.join(DB_PATH, "data_*.dat"))
after_size = sum(os.path.getsize(f) for f in dat_files_after)
INFO(f"[RUN1] data size after abort: {after_size} bytes, files={len(dat_files_after)}")

assert after_size <= baseline_size + 1, \
    f"data file should be truncated after abort: baseline={baseline_size}, after={after_size}"

INFO("[RUN1] data file truncated after task failure (dirty bytes recovered)")

# 4. 保存 db_path 给 run2
with open(os.path.join(DB_PATH, "_test_db_path"), "w") as f:
    f.write(db.get_db_path())

INFO(f"[RUN1] db_path={db.get_db_path()}")
INFO("[RUN1] PASS: abort cleanup verified, exiting for load_db verification")
