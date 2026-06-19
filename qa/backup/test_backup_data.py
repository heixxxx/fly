"""E2E test: data backup via write_object(backup=True) and read_object(backup=True).

Verifies:
1. write_object(backup=True) replicates data to a second worker
2. read_object(backup=True) persists remote data locally and registers with master
3. 50-round stability under concurrent backup operations
"""
import time
import sys
import os
import shutil


from e2e_tasks import write_data_backup, read_data_backup, write_data, read_data
from fly import open_db, get_config

import _fly_storage as storage

# DB lives under this run's log directory (unique per case-run via --log-dir),
# so parallel cases never collide on the same /tmp DB path.
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


def test_write_backup():
    cleanup()
    get_config().set_int("fail_unscheduleable_tasks", 0)

    from fly.runtime import get_agent
    master = get_agent()

    master.launch_local_workers([{}, {}])

    assert master.wait_for_workers(2), \
        f"Both workers should connect, got {master.worker_count}"

    db = open_db(DB_PATH)

    write_data_backup(db, "backup_key_1", 42)

    assert wait_for(lambda: len(master.completed_tasks) >= 1, timeout=30.0), \
        "write_data_backup task should complete"

    ds = storage.ex_stg_get_data_service()
    full_name = db.get_obj_name("backup_key_1")

    assert wait_for(lambda: len(ds.get_remote_workers(full_name)) >= 2, timeout=30.0), \
        f"Backup should replicate to 2 workers, got {len(ds.get_remote_workers(full_name))}"

    val = db.read_object("backup_key_1")
    assert val == 42, f"Expected 42, got {val}"

    master.stop()
    print(f"[PASS] test_write_backup: data replicated to {len(ds.get_remote_workers(full_name))} workers",
          file=sys.stderr)


def test_read_backup():
    cleanup()
    get_config().set_int("fail_unscheduleable_tasks", 0)

    from fly.runtime import get_agent
    master = get_agent()

    master.launch_local_workers([{}, {}, {}])

    assert master.wait_for_workers(3), \
        f"3 workers should connect, got {master.worker_count}"

    db = open_db(DB_PATH)

    write_data(db, "read_backup_key", 99)

    assert wait_for(lambda: len(master.completed_tasks) >= 1, timeout=30.0), \
        "write_data task should complete"

    ds = storage.ex_stg_get_data_service()
    full_name = db.get_obj_name("read_backup_key")

    initial_workers = ds.get_remote_workers(full_name)
    assert len(initial_workers) >= 1, "Should have at least 1 worker after write"

    for i in range(5):
        read_data_backup(db, "read_backup_key", [full_name])

    assert wait_for(lambda: len(master.completed_tasks) >= 6, timeout=30.0), \
        f"read_data_backup tasks should complete, got {len(master.completed_tasks)} completed, {len(master.failed_tasks)} failed"

    assert wait_for(lambda: len(ds.get_remote_workers(full_name)) >= 2, timeout=30.0), \
        f"Read backup should add reading worker to remote_idx, got {len(ds.get_remote_workers(full_name))}"

    val = db.read_object("read_backup_key")
    assert val == 99, f"Expected 99, got {val}"

    master.stop()
    print(f"[PASS] test_read_backup: reading worker added to remote_idx, total workers={len(ds.get_remote_workers(full_name))}",
          file=sys.stderr)


def test_backup_stability_50_rounds():
    cleanup()
    get_config().set_int("fail_unscheduleable_tasks", 0)
    get_config().set_int("data_server_threads", 4)

    from fly.runtime import get_agent
    master = get_agent()

    master.launch_local_workers([{}, {}, {}])

    assert master.wait_for_workers(3), \
        f"3 workers should connect, got {master.worker_count}"

    db = open_db(DB_PATH)
    ds = storage.ex_stg_get_data_service()

    ROUNDS = 50
    BATCH_SIZE = 5
    for batch_start in range(0, ROUNDS, BATCH_SIZE):
        batch_end = min(batch_start + BATCH_SIZE, ROUNDS)
        for i in range(batch_start, batch_end):
            write_data_backup(db, f"stab_{i}", i * 10)

        last_in_batch = db.get_obj_name(f"stab_{batch_end - 1}")
        assert wait_for(
            lambda: len(ds.get_remote_workers(last_in_batch)) >= 2,
            timeout=60.0), \
            f"Batch {batch_start}-{batch_end} backup should complete"

    assert wait_for(lambda: len(master.completed_tasks) >= ROUNDS, timeout=120.0), \
        f"All {ROUNDS} backup writes should complete, got {len(master.completed_tasks)}"

    assert len(master.failed_tasks) == 0, \
        f"Expected 0 failed, got {len(master.failed_tasks)}"

    for i in range(ROUNDS):
        full_name = db.get_obj_name(f"stab_{i}")
        workers = ds.get_remote_workers(full_name)
        assert len(workers) >= 2, \
            f"stab_{i} should have >= 2 workers in remote_idx, got {len(workers)}"

    for i in range(ROUNDS):
        val = db.read_object(f"stab_{i}")
        assert val == i * 10, f"stab_{i} should be {i * 10}, got {val}"

    print(f"[PASS] test_backup_stability_50_rounds: {ROUNDS} objects backed up, all readable from 2+ workers",
          file=sys.stderr)


test_write_backup()
test_read_backup()
test_backup_stability_50_rounds()
print("\nAll tests passed!")
