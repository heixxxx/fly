from _fly_log import INFO
import time
import sys
import os
import shutil

DB_PATH = "/tmp/fly_e2e_read_cache_db"

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '..', 'src'))

from e2e_tasks import write_data
from fly import open_db, get_config


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


def test_read_cache_basic():
    cleanup()
    get_config().set_int("fail_unscheduleable_tasks", 0)

    from fly.runtime import get_agent
    master = get_agent()

    master.launch_local_workers([{}])
    assert master.wait_for_workers(1)

    db = open_db(DB_PATH)

    n = 5
    for i in range(n):
        write_data(db, f"cache_{i}", i * 100)

    assert wait_for(lambda: len(master.completed_tasks) >= n, timeout=30.0), \
        f"Expected {n} writes to complete, got {len(master.completed_tasks)}"

    for i in range(n):
        val = db.read_object(f"cache_{i}", cache="low")
        assert val == i * 100, f"Expected {i * 100}, got {val}"

    for i in range(n):
        val = db.read_object(f"cache_{i}", cache="low")
        assert val == i * 100, f"LOW cache hit failed: expected {i * 100}, got {val}"

    for i in range(n):
        val = db.read_object(f"cache_{i}", cache="high")
        assert val == i * 100, f"HIGH cache failed: expected {i * 100}, got {val}"

    for i in range(n):
        val = db.read_object(f"cache_{i}", cache="high")
        assert val == i * 100, f"HIGH cache hit failed: expected {i * 100}, got {val}"

    for i in range(n):
        val = db.read_object(f"cache_{i}", cache="none")
        assert val == i * 100, f"NONE cache failed: expected {i * 100}, got {val}"

    INFO(f"[PASS] test_read_cache_basic: {n} objects, all cache modes verified")


def test_read_cache_cross_db():
    cleanup()
    get_config().set_int("fail_unscheduleable_tasks", 0)

    from fly.runtime import get_agent
    master = get_agent()

    master.launch_local_workers([{}])
    assert master.wait_for_workers(1)

    db1_path = DB_PATH + "_db1"
    db2_path = DB_PATH + "_db2"
    if os.path.isdir(db1_path):
        shutil.rmtree(db1_path, ignore_errors=True)
    if os.path.isdir(db2_path):
        shutil.rmtree(db2_path, ignore_errors=True)

    db1 = open_db(db1_path)
    db2 = open_db(db2_path)

    write_data(db1, "shared_key", 42)
    write_data(db2, "shared_key", 99)

    assert wait_for(lambda: len(master.completed_tasks) >= 2, timeout=30.0)

    val1 = db1.read_object("shared_key", cache="low")
    val2 = db2.read_object("shared_key", cache="low")
    assert val1 == 42, f"db1 expected 42, got {val1}"
    assert val2 == 99, f"db2 expected 99, got {val2}"

    val1 = db1.read_object("shared_key", cache="high")
    val2 = db2.read_object("shared_key", cache="high")
    assert val1 == 42, f"db1 HIGH expected 42, got {val1}"
    assert val2 == 99, f"db2 HIGH expected 99, got {val2}"

    INFO(f"[PASS] test_read_cache_cross_db: cross-DB cache isolation verified")


def test_read_cache_large_objects():
    cleanup()
    get_config().set_int("fail_unscheduleable_tasks", 0)

    from fly.runtime import get_agent
    master = get_agent()

    master.launch_local_workers([{}])
    assert master.wait_for_workers(1)

    db = open_db(DB_PATH + "_large")

    large_data = list(range(10000))
    write_data(db, "large_obj", large_data)

    assert wait_for(lambda: len(master.completed_tasks) >= 1, timeout=30.0)

    val = db.read_object("large_obj", cache="low")
    assert val == large_data, "Large object read failed"

    val = db.read_object("large_obj", cache="high")
    assert val == large_data, "Large object HIGH cache failed"

    val = db.read_object("large_obj", cache="high")
    assert val == large_data, "Large object HIGH cache hit failed"

    INFO(f"[PASS] test_read_cache_large_objects: large object caching verified")


test_read_cache_basic()
test_read_cache_cross_db()
test_read_cache_large_objects()
INFO("\nAll read cache tests passed!")
