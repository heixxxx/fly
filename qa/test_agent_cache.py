"""Test agent local cache — cross-task data sharing on the same worker."""
from _fly_log import INFO
import sys
import os
import shutil

DB_PATH = "/tmp/fly_e2e_cache_db"

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '..', 'src'))

from e2e_tasks import write_data
from fly import open_db, get_config, as_task
from fly.runtime import get_agent


def wait_for(condition, timeout=20.0, interval=0.5):
    import time
    t0 = time.time()
    while time.time() - t0 < timeout:
        if condition():
            return True
        time.sleep(interval)
    return False


def cleanup():
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)


# ── Task definitions ──

@as_task(inputs=lambda db: [])
def cache_producer(db):
    """Write data to DB and store intermediate in agent cache."""
    from fly import put_cache
    db.write_object("produced", 42)
    put_cache("intermediate", [1, 2, 3])
    put_cache("label", "hello")


@as_task(inputs=lambda db: [db.get_obj_name("produced")])
def cache_consumer(db):
    """Read from agent cache — should find data left by producer."""
    from fly import get_cache, has_cache
    assert has_cache("intermediate"), "cache_consumer: 'intermediate' not found"
    val = get_cache("intermediate")
    assert val == [1, 2, 3], f"cache_consumer: expected [1,2,3], got {val}"

    label = get_cache("label")
    assert label == "hello", f"cache_consumer: expected 'hello', got {label}"

    # Key that was never set — should return default
    missing = get_cache("no_such_key", default="MISSING")
    assert missing == "MISSING", f"cache_consumer: default broken, got {missing}"

    db.write_object("consumed", val[0] + val[1] + val[2])


@as_task(inputs=lambda db: [db.get_obj_name("consumed")])
def cache_remover(db):
    """Remove a cache entry and verify it's gone."""
    from fly import remove_cache, has_cache, get_cache
    remove_cache("intermediate")
    assert not has_cache("intermediate"), "cache_remover: key still present after remove"

    # 'label' should still be there
    assert has_cache("label"), "cache_remover: 'label' was removed unexpectedly"

    # Verify 'consumed' value
    val = db.read_object("consumed")
    assert val == 6, f"cache_remover: expected consumed=6, got {val}"
    db.write_object("done", True)


# ── Test ──

cleanup()
get_config().set_int("fail_unscheduleable_tasks", 1)

master = get_agent()
master.launch_local_workers([{}])
assert master.wait_for_workers(), \
    "Worker should connect"
INFO("  Phase 1 OK: worker connected")

# Test Master-side cache API
master.put_cache("mkey", "mval")
assert master.get_cache("mkey") == "mval", "Master cache put/get broken"
assert master.has_cache("mkey"), "Master has_cache broken"
master.remove_cache("mkey")
assert not master.has_cache("mkey"), "Master remove_cache broken"
assert master.get_cache("mkey", default="X") == "X", "Master default broken"
INFO("  Phase 2 OK: Master cache API works")

# Test Master clear_cache
master.put_cache("a", 1)
master.put_cache("b", 2)
assert master.has_cache("a") and master.has_cache("b")
master.clear_cache()
assert not master.has_cache("a") and not master.has_cache("b")
INFO("  Phase 3 OK: Master clear_cache works")

db = open_db(DB_PATH)

# Run 3 tasks on the same worker — they share the agent cache
cache_producer(db)
cache_consumer(db)
cache_remover(db)

assert wait_for(lambda: len(master.completed_tasks) >= 3), \
    f"Expected 3 completed, got {len(master.completed_tasks)}"
INFO("  Phase 4 OK: all tasks completed")

# Verify final data
done = db.read_object("done")
assert done is True, f"Expected done=True, got {done}"
INFO("  Phase 5 OK: data verified")

master.stop()
INFO("[PASS] test_agent_cache")
