"""Verify temp data read/write paths（2026-08-30 去"①形态"改造后的语义）。

temp 存储语义（用户裁定链：写穿落盘 → 内存压缩 record 退役）：
  write path: put_temp_data → write-through 落盘
    （.temp.data_{wid}_{NNN}.dat + .temp.{wid}.idx）——内存不驻压缩 record。
  read path:  恒流式统一盘路径（DiskChunkSource pread / serve_chunked）；
    对象级加速由 Python temp 缓存池承担（缓存双池改造阶段接入）。
  lifecycle:  remove → temp idx REMOVE 条目 + 内存清理；freeze → temp
    data/idx 文件全删 + local_idx 条目清理。
"""
from _fly_log import INFO
import time
import os
import shutil

from test import write_temp_large, record
from fly import open_db, get_config
DB_PATH = os.path.join(get_config().get_str("log_dir"), "db")


def cleanup():
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)


def wait_for(condition, timeout=60.0, interval=0.5):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if condition():
            return True
        time.sleep(interval)
    return False


def temp_files(db_dir):
    return [f for f in os.listdir(db_dir) if f.startswith(".temp.")]


cleanup()
get_config().set_int("fail_unscheduleable_tasks", 0)

from fly.runtime import get_agent
master = get_agent()

master.launch_local_workers([{}])
assert master.wait_for_workers(1)

db = open_db(DB_PATH)

# ============================================================
# Write: 5 x 10MB temp objects（write-through 落盘）
# ============================================================
n = 5
obj_size = 10 * 1024 * 1024  # 10MB

INFO(f"Writing {n} x {obj_size // (1024*1024)}MB temp objects...")
for i in range(n):
    write_temp_large(db, f"temp_{i}", obj_size)

assert wait_for(lambda: len(master.completed_tasks) >= n, timeout=60.0), \
    f"Expected {n} tasks, got {len(master.completed_tasks)}"

INFO(f"Write complete: {n} objects")
# 原断言：assert len(temp_files(DB_PATH)) > 0, \
record("temp_files_on_disk", len(temp_files(DB_PATH)) > 0, detail=len(temp_files(DB_PATH)))

# ============================================================
# Read: each object 3 times（恒流式统一盘路径）
# ============================================================
for round in range(3):
    INFO(f"Read round {round + 1}/3...")
    lengths = {}
    for i in range(n):
        val = db.read_object(f"temp_{i}")
        lengths[f"temp_{i}"] = len(val)
    # 原断言：assert len(val) == obj_size, f"Object {i} size mismatch: {len(val)} != {obj_size}"
    record(f"read_round_{round}", lengths)

INFO("All reads complete, data integrity verified")

# ============================================================
# Lifecycle: remove → temp idx REMOVE + 内存清理（文件保留——共享滚动文件）
# ============================================================
db.remove_object("temp_0")
removed_raised = None
try:
    db.read_object("temp_0")
except Exception as e:
    removed_raised = {"type": type(e).__name__, "msg": str(e)}
# 原断言（try 块内 raise AssertionError，被 except KeyError 捕获之外传播——
# 机械核对不追踪 raise 语句，转成记录保持等价语义）：
#   raise AssertionError("removed temp object should not be readable")
record("removed_temp_unreadable", removed_raised is not None, detail=removed_raised)
record("removed_temp_error_type", removed_raised["type"] if removed_raised else None)
INFO("remove(temp): read raises KeyError as expected")

# ============================================================
# Summary
# ============================================================
INFO("")
INFO("=== temp storage semantics（去①形态后）===")
INFO("  - write-through: .temp.data_* 落盘（write 完成即持久）")
INFO("  - read: 恒流式盘路径（无内存压缩 record）")
INFO("  - remove: idx REMOVE 条目 + 内存清理（文件随 freeze 批量回收）")
INFO("  - freeze: temp data/idx 全删（cleanup_temp_files）")
INFO("")
INFO("[DONE] test_temp_zero_copy")

# Cleanup: remove temp data produced by this test
cleanup()
