"""E2E test: automatic backup triggered by access frequency.

Verifies:
1. Objects written WITHOUT backup=True get auto-backed up after exceeding access threshold
2. Auto-backup creates replicas on additional workers (remote_idx shows 2+ workers)
3. 20-round stability test with low threshold
"""
import time
import sys
import os
import shutil

DB_PATH = "/tmp/fly_e2e_auto_backup_db"

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '..', 'src'))

from e2e_tasks import write_data, read_data
from fly import open_db, get_config

import _fly_storage as storage


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


def test_auto_backup_basic():
    """Write data on worker, read from another worker repeatedly, verify auto-backup triggers."""
    cleanup()
    get_config().set_int("fail_unscheduleable_tasks", 0)
    get_config().set_int("auto_backup_enabled", 1)
    get_config().set_int("backup_threshold", 2)       # 3 workers → max 2 remote reads (one DataQuery per worker)
    get_config().set_int("backup_replicas", 2)

    from fly.runtime import get_agent
    master = get_agent()

    master.launch_local_workers([{}, {}, {}])

    assert master.wait_for_workers(3), \
        f"3 workers should connect, got {master.worker_count}"

    db = open_db(DB_PATH)

    # Write data WITHOUT backup=True
    write_data(db, "auto_key_1", 42)

    assert wait_for(lambda: len(master.completed_tasks) >= 1, timeout=30.0), \
        "write_data task should complete"

    ds = storage.ex_stg_get_data_service()
    full_name = db.get_obj_name("auto_key_1")

    # Initially only 1 worker has the data
    initial_workers = len(ds.get_remote_workers(full_name))
    assert initial_workers >= 1, \
        f"Should have at least 1 worker, got {initial_workers}"

    # Read the data multiple times from other workers (triggers DataQueryMessage)
    # Each read_data task runs on a worker and reads the object
    for i in range(10):
        read_data(db, "auto_key_1", [full_name])

    assert wait_for(lambda: len(master.completed_tasks) >= 11, timeout=60.0), \
        f"All read tasks should complete, got {len(master.completed_tasks)} completed, {len(master.failed_tasks)} failed"

    # Wait for auto-backup to trigger and complete
    assert wait_for(lambda: len(ds.get_remote_workers(full_name)) >= 2, timeout=30.0), \
        f"Auto-backup should create replica, got {len(ds.get_remote_workers(full_name))} workers"

    # Verify data is still correct
    val = db.read_object("auto_key_1")
    assert val == 42, f"Expected 42, got {val}"

    master.stop()
    print(f"[PASS] test_auto_backup_basic: auto-backup triggered after reads, {len(ds.get_remote_workers(full_name))} workers",
          file=sys.stderr)


def test_auto_backup_disabled():
    """Verify auto-backup does NOT trigger when disabled."""
    cleanup()
    get_config().set_int("fail_unscheduleable_tasks", 0)
    get_config().set_int("auto_backup_enabled", 0)   # DISABLED
    get_config().set_int("backup_threshold", 5)

    from fly.runtime import get_agent
    master = get_agent()

    master.launch_local_workers([{}, {}])

    assert master.wait_for_workers(2), \
        f"2 workers should connect, got {master.worker_count}"

    db = open_db(DB_PATH)

    write_data(db, "disabled_key", 99)

    assert wait_for(lambda: len(master.completed_tasks) >= 1, timeout=30.0), \
        "write_data task should complete"

    ds = storage.ex_stg_get_data_service()
    full_name = db.get_obj_name("disabled_key")

    for i in range(10):
        read_data(db, "disabled_key", [full_name])

    assert wait_for(lambda: len(master.completed_tasks) >= 11, timeout=60.0), \
        "All read tasks should complete"

    # With auto_backup disabled, should still have only 1 worker
    time.sleep(3)  # Give extra time in case auto-backup would trigger
    workers = len(ds.get_remote_workers(full_name))
    assert workers == 1, \
        f"Auto-backup disabled: should have exactly 1 worker, got {workers}"

    master.stop()
    print(f"[PASS] test_auto_backup_disabled: no auto-backup when disabled",
          file=sys.stderr)


def test_auto_backup_stability():
    """20-round stability test with low threshold."""
    cleanup()
    get_config().set_int("fail_unscheduleable_tasks", 0)
    get_config().set_int("auto_backup_enabled", 1)
    get_config().set_int("backup_threshold", 2)
    get_config().set_int("backup_replicas", 2)

    from fly.runtime import get_agent
    master = get_agent()

    master.launch_local_workers([{}, {}, {}])

    assert master.wait_for_workers(3), \
        f"3 workers should connect, got {master.worker_count}"

    db = open_db(DB_PATH)
    ds = storage.ex_stg_get_data_service()

    ROUNDS = 20
    for i in range(ROUNDS):
        write_data(db, f"stab_{i}", i * 10)

    assert wait_for(lambda: len(master.completed_tasks) >= ROUNDS, timeout=60.0), \
        f"All writes should complete, got {len(master.completed_tasks)}"

    # Read each object multiple times to trigger auto-backup
    for i in range(ROUNDS):
        full_name = db.get_obj_name(f"stab_{i}")
        for _ in range(5):
            read_data(db, f"stab_{i}", [full_name])

    expected_reads = ROUNDS * 5
    assert wait_for(
        lambda: len(master.completed_tasks) >= ROUNDS + expected_reads,
        timeout=120.0), \
        f"All reads should complete, got {len(master.completed_tasks)}"

    # Wait for all objects to get auto-backed up (2+ workers)
    assert wait_for(
        lambda: all(
            len(ds.get_remote_workers(db.get_obj_name(f"stab_{i}"))) >= 2
            for i in range(ROUNDS)
        ),
        timeout=60.0), \
        "Not all objects got auto-backed up"

    # Verify data integrity
    for i in range(ROUNDS):
        val = db.read_object(f"stab_{i}")
        assert val == i * 10, f"stab_{i}: expected {i * 10}, got {val}"

    assert len(master.failed_tasks) == 0, \
        f"Expected 0 failed tasks, got {len(master.failed_tasks)}"

    master.stop()
    print(f"[PASS] test_auto_backup_stability: {ROUNDS} objects auto-backed up, all readable",
          file=sys.stderr)


def test_auto_backup_master_local_write():
    """Master local writes should auto-backup to a Worker when auto_backup_enabled."""
    cleanup()
    get_config().set_int("fail_unscheduleable_tasks", 0)
    get_config().set_int("auto_backup_enabled", 1)
    get_config().set_int("backup_replicas", 2)

    from fly.runtime import get_agent
    master = get_agent()

    master.launch_local_workers([{}])

    assert master.wait_for_workers(1), \
        f"1 worker should connect, got {master.worker_count}"

    db = open_db(DB_PATH)

    db.write_object("master_key", 42)

    ds = storage.ex_stg_get_data_service()
    full_name = db.get_obj_name("master_key")

    assert wait_for(lambda: len(ds.get_remote_workers(full_name)) >= 2, timeout=30.0), \
        f"Master write should auto-backup to Worker, got {len(ds.get_remote_workers(full_name))} workers"

    val = db.read_object("master_key")
    assert val == 42, f"Expected 42, got {val}"

    master.stop()
    print(f"[PASS] test_auto_backup_master_local_write: Master data auto-backed up to Worker",
          file=sys.stderr)


test_auto_backup_basic()
test_auto_backup_disabled()
test_auto_backup_stability()
test_auto_backup_master_local_write()
print("\nAll auto-backup tests passed!")
