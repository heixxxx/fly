"""E2E test: automatic backup triggered by access frequency.

Verifies:
1. Objects written WITHOUT backup=True get auto-backed up after exceeding access threshold
2. Auto-backup creates replicas on additional workers (remote_idx shows 2+ workers)
3. 20-round stability test with low threshold

Design note on determinism
--------------------------
auto-backup's read_count only counts *cross-worker* DataQuery requests (see
docs/architecture.md §"自动备份（访问频率触发）"). A read that hits the worker's
local cache does NOT generate a DataQuery and thus does not increment the
counter. Because the scheduler prefers idle workers, a freshly-written object
tends to be re-read on the *same* worker that wrote it (now idle + warm cache),
so a naive "read N times" loop can leave read_count stuck below threshold.

To make this test deterministic (no scheduling-dependent flakiness), each
worker is given a unique capability and read tasks use a dynamic
``requires=lambda ...: [cap]`` (task.py supports callable requires) to pin each
read to a specific, distinct worker. Writing is pinned to a dedicated writer
worker, so reads from the reader workers are guaranteed cross-worker and
guaranteed to increment read_count.
"""
import time
import sys
import os
import shutil


from fly import as_task, open_db, get_config

import _fly_storage as storage

# DB lives under this run's log directory (set via --log-dir by runqa, resolved
# to a unique ".N" variant per process by the logger). This makes the DB path
# unique per case-run, so parallel cases (runqa -j) never collide on the same
# /tmp path and each run starts from a clean directory.
DB_PATH = os.path.join(get_config().get_str("log_dir"), "db")


# --- tasks pinned to specific workers via dynamic requires ------------------
# write_on pins the write to the "writer" worker; read_on pins the read to an
# arbitrary worker capability passed as the ``cap`` argument. Because requires
# accepts a callable, the target worker is chosen per-call from the arguments.

@as_task(requires=lambda db, key, value: ["writer"])
def write_on(db, key, value):
    db.write_object(key, value)


@as_task(inputs=lambda db, key, deps, cap: list(deps),
         requires=lambda db, key, deps, cap: [cap])
def read_on(db, key, deps, cap):
    return db.read_object(key)


READER_CAPS = ["reader_a", "reader_b", "reader_c"]


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


def launch_tiered_workers(n_readers):
    """Launch 1 writer + n_readers reader workers, each with a unique capability."""
    from fly.runtime import get_agent
    master = get_agent()
    configs = [{"attributes": ["writer"]}]
    configs.extend({"attributes": [cap]} for cap in READER_CAPS[:n_readers])
    master.launch_local_workers(configs)
    expected = 1 + n_readers
    assert master.wait_for_workers(expected), \
        f"{expected} workers should connect, got {master.worker_count}"
    return master


def test_auto_backup_basic():
    """Write on writer, read from two distinct reader workers, verify auto-backup triggers."""
    cleanup()
    get_config().set_int("fail_unscheduleable_tasks", 0)
    get_config().set_int("auto_backup_enabled", 1)
    get_config().set_int("backup_threshold", 2)       # 2 cross-worker reads trigger backup
    get_config().set_int("backup_replicas", 2)

    master = launch_tiered_workers(n_readers=2)

    db = open_db(DB_PATH)

    # Write data WITHOUT backup=True (pinned to the writer worker)
    write_on(db, "auto_key_1", 42)

    assert wait_for(lambda: len(master.completed_tasks) >= 1, timeout=30.0), \
        "write_data task should complete"

    ds = storage.ex_stg_get_data_service()
    full_name = db.get_obj_name("auto_key_1")

    # Initially only the writer worker has the data
    initial_workers = len(ds.get_remote_workers(full_name))
    assert initial_workers >= 1, \
        f"Should have at least 1 worker, got {initial_workers}"

    # Read from two distinct reader workers — each is a guaranteed cross-worker
    # DataQuery (the object lives only on the writer worker), so read_count
    # reaches the threshold deterministically.
    read_on(db, "auto_key_1", [full_name], "reader_a")
    read_on(db, "auto_key_1", [full_name], "reader_b")

    assert wait_for(lambda: len(master.completed_tasks) >= 3, timeout=60.0), \
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

    master = launch_tiered_workers(n_readers=2)

    db = open_db(DB_PATH)

    write_on(db, "disabled_key", 99)

    assert wait_for(lambda: len(master.completed_tasks) >= 1, timeout=30.0), \
        "write_data task should complete"

    ds = storage.ex_stg_get_data_service()
    full_name = db.get_obj_name("disabled_key")

    # Cross-worker reads from both readers (would trigger backup if enabled)
    read_on(db, "disabled_key", [full_name], "reader_a")
    read_on(db, "disabled_key", [full_name], "reader_b")

    assert wait_for(lambda: len(master.completed_tasks) >= 3, timeout=60.0), \
        "All read tasks should complete"

    # With auto_backup disabled, evaluate_auto_backup is never called, so the
    # object stays on the writer worker only. No sleep needed: once the read
    # tasks have completed, every DataQuery they produced has been fully
    # processed by the master.
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

    master = launch_tiered_workers(n_readers=3)

    db = open_db(DB_PATH)
    ds = storage.ex_stg_get_data_service()

    ROUNDS = 20
    for i in range(ROUNDS):
        write_on(db, f"stab_{i}", i * 10)

    assert wait_for(lambda: len(master.completed_tasks) >= ROUNDS, timeout=60.0), \
        f"All writes should complete, got {len(master.completed_tasks)}"

    # Read each object from two distinct reader workers. Each reader worker is
    # different from the writer worker, so every read is a guaranteed
    # cross-worker DataQuery that increments read_count. Two such reads push
    # read_count to the threshold (2) deterministically, regardless of
    # scheduler ordering.
    for i in range(ROUNDS):
        full_name = db.get_obj_name(f"stab_{i}")
        read_on(db, f"stab_{i}", [full_name], "reader_a")
        read_on(db, f"stab_{i}", [full_name], "reader_b")

    expected_reads = ROUNDS * 2
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
