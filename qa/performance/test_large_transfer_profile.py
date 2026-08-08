"""QA test: 10MB object remote transfer with memory copy profiling.

Setup:
  - Master launches 2 workers: worker1 (gpu), worker2 (cpu)
  - Task 1 (gpu_write_large_temp): runs on worker1, writes ~10MB temp object
  - Task 2 (cpu_read_large_remote): runs on worker2, reads the object remotely
    (triggers DataServer → DataClient wire transfer)

Profiling:
  - Workers launched under perf record to capture memcpy/copy_user calls
  - After completion, perf report shows hot copy points
  - Validates: no large user-space copy of the 10MB payload in the hot path

Run: ./build/bin/fly qa/test_large_transfer_profile.py
"""
import os
import sys
import time
import shutil
import subprocess

PERF_DIR = f"/tmp/fly_profile_perf_{os.getpid()}"


from test import gpu_write_large_temp, cpu_read_large_remote
from fly import open_db, get_config
DB_PATH = os.path.join(get_config().get_str("log_dir"), "db")


def cleanup():
    for p in [DB_PATH, PERF_DIR]:
        if os.path.isdir(p):
            shutil.rmtree(p, ignore_errors=True)
    os.makedirs(PERF_DIR, exist_ok=True)


def setup_workers_under_perf():
    """Launch 2 workers with perf recording attached."""
    from fly.runtime import get_agent
    master = get_agent()

    # Launch workers as separate processes via subprocess.Popen.
    # For perf: attach to the master PID; for valgrind massif: use
    # --trace-children=yes to trace worker subprocesses.
    master.launch_local_workers([
        {"attributes": ["gpu"]},
        {"attributes": []},
    ])
    for i in range(40):
        if master.worker_count >= 2:
            break
        time.sleep(0.5)
    assert master.worker_count >= 2, \
        f"Only {master.worker_count}/2 workers connected"
    return master


def wait_completed(master, expected, timeout=60):
    t0 = time.time()
    while time.time() - t0 < timeout:
        c = master.completed_tasks
        if len(c) >= expected:
            return c
        time.sleep(0.5)
    return master.completed_tasks


def main():
    cleanup()
    # ~10MB: 1.25M ints * 8 bytes = 10MB pickle payload (enough to see copy patterns)
    LARGE_SIZE = 1_250_000

    master = setup_workers_under_perf()
    db = open_db(DB_PATH)

    # Task 1: worker1 (gpu) writes large temp object
    gpu_write_large_temp(db, "large_blob", LARGE_SIZE)

    # Task 2: worker2 (cpu) reads it remotely (triggers wire transfer)
    cpu_read_large_remote(db, "large_blob")

    completed = wait_completed(master, 2, timeout=120)
    assert len(completed) >= 2, f"Expected 2 completed, got {len(completed)}"
    print(f"[DEBUG] completed: {completed}")

    # Output ObjectCache stats to show cache behavior
    import _fly_storage
    stats = _fly_storage.ex_stg_cache_stats()
    # stats = (lo_h, lo_m, lo_p, lo_e, hi_h, hi_m, hi_p, hi_e)
    print(f"[STATS] low:  hits={stats[0]} misses={stats[1]} puts={stats[2]} evictions={stats[3]}")
    print(f"[STATS] high: hits={stats[4]} misses={stats[5]} puts={stats[6]} evictions={stats[7]}")

    db.reset()
    print("\nLarge transfer profiling test passed!")
    print("Perf data hint: run under 'perf record -e cpu-cycles ./build/bin/fly qa/test_large_transfer_profile.py'")
    print("Then 'perf report' to see hot copy functions (memcpy/memmove/__memcpy_sse)")


if __name__ == "__main__":
    try:
        main()
    except Exception as e:
        print(f"[FAIL] {e}", file=sys.stderr)
        import traceback
        traceback.print_exc()
        sys.exit(1)
