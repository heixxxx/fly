"""E2E test: merge_db 空 idx 防误删源。

清单全空必须区分两种成因：
  真空 db（无 .idx 文件）→ 允许空合并；
  idx 存在但读出 0 条目（损坏/读失败被误当真空）→ 拒绝 merge——
  否则 0 个 task 全部"成功"走"全成功删源"，源 .dat 全删而产物为空 = 数据丢失。

注入：正常写对象落盘后，用垃圾字节覆盖 .idx 文件（load 失败 → 清单空），
断言 merge_db 拒绝且源 .dat 保留。
"""
from _fly_log import INFO
import os
import shutil
import time

from test import write_data_no_cache
from fly import open_db, merge_db, get_config
DB_PATH = os.path.join(get_config().get_str("log_dir"), "merge_empty_idx_db")
from fly.runtime import get_agent


def cleanup():
    for p in (DB_PATH, DB_PATH + ".merged_data"):
        if os.path.isdir(p):
            shutil.rmtree(p, ignore_errors=True)


def wait_for(condition, timeout=30.0, interval=0.5):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if condition():
            return True
        time.sleep(interval)
    return False


cleanup()
get_config().set_int("fail_unscheduleable_tasks", 0)

master = get_agent()
master.launch_local_workers([{"host": "host_A"}])
assert master.wait_workers_registered(timeout=60), "host_A worker should register"

db = open_db(DB_PATH)
write_data_no_cache(db, "guard/obj", "guard_payload")
assert wait_for(lambda: len(master.completed_tasks) >= 1), "write should complete"
time.sleep(0.5)

db.freeze()
assert db.is_frozen(), "db should be frozen"

src_dats = [f for f in os.listdir(DB_PATH)
            if f.startswith("data_") and f.endswith(".dat")
            and os.path.getsize(os.path.join(DB_PATH, f)) > 0]
assert src_dats, "source should have non-empty .dat"

# ── 注入：垃圾字节覆盖所有 .idx（load 失败 → Phase 3 清单全空）──
idx_files = [f for f in os.listdir(DB_PATH) if f.endswith(".idx")]
assert idx_files, "written db must have idx files"
for f in idx_files:
    with open(os.path.join(DB_PATH, f), "wb") as fh:
        fh.write(b"\x00GARBAGE_NOT_A_VALID_IDX")
INFO(f"[GUARD] corrupted {len(idx_files)} idx file(s)")

# ── merge_db 必须拒绝（防数据丢失），源 .dat 保留 ──
rejected = False
try:
    merge_db(DB_PATH, delete_source=True)
except RuntimeError as e:
    rejected = True
    assert "idx" in str(e).lower(), f"error should mention idx corruption: {e}"
    INFO(f"[GUARD] merge_db refused as expected: {e}")
assert rejected, "merge_db with corrupted-but-present idx must be refused"

src_dats_after = [f for f in os.listdir(DB_PATH)
                  if f.startswith("data_") and f.endswith(".dat")
                  and os.path.getsize(os.path.join(DB_PATH, f)) > 0]
assert src_dats_after == src_dats, "source .dat must be preserved (no deletion)"

INFO("[PASS] test_merge_empty_idx_guard")
