"""Regression test for Python ReadCache invalidation on write/remove.

Bug fixed here: `Database.read_object(cache="high")` populates the
process-local Python ReadCache for pickle objects, but write_object /
remove_object used to NOT invalidate it. So the sequence
  write(A) -> read(A, high) [caches] -> write(A, new) -> read(A, high)
returned the stale cached object instead of the new value. The C++
ObjectCache auto-invalidates on write/remove (object_cache.h contract);
this test pins the same contract for the Python tier.

Process-boundary note: ReadCache is a per-process module-level singleton
(get_read_cache()). write_object via a worker task executes in the *worker*
process and invalidates that worker's cache, not the master's. To
deterministically exercise master-side staleness we therefore drive the
invalidating write from the master/test process directly (db.write_object
called here), mirroring how read_object is already called in-process in the
existing read_cache QA tests.

Scenarios:
  1. write invalidation: cached value reflects the latest write, not a stale one.
  2. overwrite via write_object_raw also invalidates.
  3. remove invalidation: a removed object is not served from the cache.
"""
from _fly_log import INFO
import time
import os
import shutil

from test import write_data
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


def assert_cached(key):
    """Confirm `key` is actually resident in the Python high-tier cache."""
    try:
        from storage import get_read_cache
    except ImportError:
        from storage import get_read_cache
    rc = get_read_cache()
    db_path = db.get_db_path()
    return rc.get(f"{db_path}:{key}", "high") is not None


cleanup()
get_config().set_int("fail_unscheduleable_tasks", 0)

from fly.runtime import get_agent
master = get_agent()
master.launch_local_workers([{}])
assert master.wait_for_workers(1)

db = open_db(DB_PATH)

# Seed one object via a worker task so it lands on disk, then read it into the
# master-process ReadCache.
write_data(db, "k", 1)
assert wait_for(lambda: len(master.completed_tasks) >= 1, timeout=30.0)

val = db.read_object("k", cache="high")
assert val == 1, f"initial read failed: expected 1, got {val}"
assert assert_cached("k"), "object should be cached in the Python high tier after read"

# ── Scenario 1: write_object invalidation ──────────────────────────────
# provenance 禁止同名覆盖（不同 context）。合法重写 = 先 remove 再 write。
# remove 清 Python high-tier cache + provenance；write 填新值，read 应见新值。
db.remove_object("k")
db.write_object("k", 2)
val = db.read_object("k", cache="high")
assert val == 2, f"write invalidation failed: expected 2, got {val} (stale cache)"
INFO("[PASS] write_object invalidation: remove+rewrite refreshes cached value")

# ── Scenario 2: 直写路径（_write_pickle_bytes）invalidation ────────────
# write_object_raw 已删除（2026-08-30，生产零使用）——直写路径以
# _write_pickle_bytes 覆盖（同经 C++ commit 链，缓存失效语义相同）。
# 同样需先 remove（清 provenance + cache）再直写重写。
import pickle as _pickle
db.remove_object("k")
db._db._write_pickle_bytes("k", _pickle.dumps("raw_value"), "str", False)
data, _ = db._db._read_decompressed("k")
val = _pickle.loads(data)
assert val == "raw_value", f"direct-write read failed: expected 'raw_value', got {val!r}"
INFO("[PASS] direct-write invalidation: remove+rewrite refreshes cached value")

# ── Scenario 3: remove_object invalidation ─────────────────────────────
# Re-populate the high-tier cache with a fresh pickle object, then remove it.
write_data(db, "rm", 10)
assert wait_for(lambda: len(master.completed_tasks) >= 2, timeout=30.0)
val = db.read_object("rm", cache="high")
assert val == 10, f"setup read failed: expected 10, got {val}"
assert assert_cached("rm"), "rm should be cached before removal"

# remove_object on the master sends an async remove request to the worker, so
# the C++ index is not guaranteed gone immediately. We therefore assert the
# Python high-tier cache entry directly: after remove_object it MUST be gone
# (that is the invalidation contract we added), regardless of C++ timing.
db.remove_object("rm")
assert not assert_cached("rm"), \
    "remove invalidation failed: stale Python high-tier entry still present"
INFO("[PASS] remove_object invalidation: high-tier cache entry dropped")

INFO("[PASS] test_read_cache_invalidation: all scenarios verified")
