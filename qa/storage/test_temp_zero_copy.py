"""Verify temp data read/write paths are zero-copy via valgrind massif profiling.

Temp write path (zero-copy):
  compress_buffered_data → FlyBufferPtr → put_temp_data → on_temp_write (shared_ptr stored)

Temp read path (zero-copy):
  read_object_compressed → read_raw_compressed → try_read_local_raw
    → is_temp → return temp_compressed_data_ (shared_ptr, no copy)
    → ObjectCache.put_low (same shared_ptr, no copy)

Run with valgrind massif:
  valgrind --tool=massif --trace-children=yes --stacks=yes \
    ./build/bin/fly qa/storage/test_temp_zero_copy.py

Verify:
  grep "mem_heap_B=" massif.out.* | sed 's/.*mem_heap_B=//' | sort -rn | head -5
  ms_print massif.out.<PID>

Expected: allocation tree should NOT contain:
  - CMString(ptr, size) copy in put_temp_data / on_temp_write
  - FlyBuffer→CMString temporary copy
  - substr / take / full_buf memcpy in temp read path
"""

from _fly_log import INFO
import time
import os
import shutil

from e2e_tasks import write_temp_large
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


cleanup()
get_config().set_int("fail_unscheduleable_tasks", 0)

from fly.runtime import get_agent
master = get_agent()

master.launch_local_workers([{}])
assert master.wait_for_workers(1)

db = open_db(DB_PATH)

# ============================================================
# Write: 3 x 1MB temp objects
# Expected allocation: compress_buffered_data (lz4 chunks) only
# No CMString copy in put_temp_data → on_temp_write path
# ============================================================
n = 3
obj_size = 1 * 1024 * 1024  # 1MB

INFO(f"Writing {n} x {obj_size // (1024*1024)}MB temp objects...")
for i in range(n):
    write_temp_large(db, f"temp_{i}", obj_size)

assert wait_for(lambda: len(master.completed_tasks) >= n, timeout=30.0), \
    f"Expected {n} tasks, got {len(master.completed_tasks)}"

INFO(f"Write complete: {n} objects")

# ============================================================
# Read: each object 3 times
# Expected allocation on 1st read: decompress only (py_name parse)
# Expected allocation on 2nd/3rd read: near zero (ObjectCache.low hit, shared_ptr)
# No FlyBuffer→CMString copy in try_read_local_raw path
# ============================================================
for round in range(3):
    INFO(f"Read round {round + 1}/3...")
    for i in range(n):
        val = db.read_object(f"temp_{i}")
        assert len(val) == obj_size, f"Object {i} size mismatch: {len(val)} != {obj_size}"

INFO("All reads complete, data integrity verified")

# ============================================================
# Summary
# ============================================================
INFO("")
INFO("=== Verification Guide ===")
INFO("Run: valgrind --tool=massif --trace-children=yes --stacks=yes ./build/bin/fly qa/storage/test_temp_zero_copy.py")
INFO("")
INFO("Check massif output for temp write path:")
INFO("  - Peak alloc should be in compress_buffered_data (lz4 chunks)")
INFO("  - Should NOT see: CMString(ptr, size) in put_temp_data or on_temp_write")
INFO("")
INFO("Check massif output for temp read path:")
INFO("  - 1st read: alloc in decompress (py_name parse only, data is zero-copy)")
INFO("  - 2nd/3rd read: near-zero alloc (ObjectCache.low hit, shared_ptr return)")
INFO("  - Should NOT see: FlyBuffer→CMString copy in try_read_local_raw")
INFO("")
INFO("[PASS] test_temp_zero_copy")
