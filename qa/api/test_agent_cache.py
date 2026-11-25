"""Test agent local cache — cross-task data sharing on the same worker."""
from _fly_log import INFO
import os
import shutil



from test import write_data, record
from fly import open_db, get_config, as_task
DB_PATH = os.path.join(get_config().get_str("log_dir"), "db")
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


@as_task(inputs=lambda db: [db.get_full_name("produced")])
def cache_consumer(db):
    """Read from agent cache — should find data left by producer."""
    from fly import get_cache, has_cache
    # 保留：@as_task 任务函数内的断言，record 只能在子阶段的主进程里调用
    assert has_cache("intermediate"), "cache_consumer: 'intermediate' not found"
    val = get_cache("intermediate")
    # 保留：@as_task 任务函数内的断言，record 只能在子阶段的主进程里调用
    assert val == [1, 2, 3], f"cache_consumer: expected [1,2,3], got {val}"

    label = get_cache("label")
    # 保留：@as_task 任务函数内的断言，record 只能在子阶段的主进程里调用
    assert label == "hello", f"cache_consumer: expected 'hello', got {label}"

    # Key that was never set — should return default
    missing = get_cache("no_such_key", default="MISSING")
    # 保留：@as_task 任务函数内的断言，record 只能在子阶段的主进程里调用
    assert missing == "MISSING", f"cache_consumer: default broken, got {missing}"

    db.write_object("consumed", val[0] + val[1] + val[2])


@as_task(inputs=lambda db: [db.get_full_name("consumed")])
def cache_remover(db):
    """Remove a cache entry and verify it's gone."""
    from fly import remove_cache, has_cache, get_cache
    remove_cache("intermediate")
    # 保留：@as_task 任务函数内的断言，record 只能在子阶段的主进程里调用
    assert not has_cache("intermediate"), "cache_remover: key still present after remove"

    # 'label' should still be there
    # 保留：@as_task 任务函数内的断言，record 只能在子阶段的主进程里调用
    assert has_cache("label"), "cache_remover: 'label' was removed unexpectedly"

    # Verify 'consumed' value
    val = db.read_object("consumed")
    # 保留：@as_task 任务函数内的断言，record 只能在子阶段的主进程里调用
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
mkey_val = master.get_cache("mkey")
# 原断言：assert master.get_cache("mkey") == "mval", "Master cache put/get broken"
record("master_api/get_mkey", mkey_val)
mkey_present = master.has_cache("mkey")
# 原断言：assert master.has_cache("mkey"), "Master has_cache broken"
record("master_api/mkey_present_after_put", mkey_present, detail=mkey_present)
master.remove_cache("mkey")
mkey_gone = not master.has_cache("mkey")
# 原断言：assert not master.has_cache("mkey"), "Master remove_cache broken"
record("master_api/mkey_removed", mkey_gone, detail=mkey_gone)
default_val = master.get_cache("mkey", default="X")
# 原断言：assert master.get_cache("mkey", default="X") == "X", "Master default broken"
record("master_api/default_for_missing", default_val)
INFO("  Phase 2 OK: Master cache API works")

# Test Master clear_cache
master.put_cache("a", 1)
master.put_cache("b", 2)
has_a, has_b = master.has_cache("a"), master.has_cache("b")
# 原断言：assert master.has_cache("a") and master.has_cache("b")
record("clear_cache/a_b_present", has_a and has_b, detail={"a": has_a, "b": has_b})
master.clear_cache()
has_a2, has_b2 = master.has_cache("a"), master.has_cache("b")
# 原断言：assert not master.has_cache("a") and not master.has_cache("b")
record("clear_cache/a_b_cleared", not has_a2 and not has_b2,
       detail={"a": has_a2, "b": has_b2})
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
# 原断言：assert done is True, f"Expected done=True, got {done}"
record("consumed_sum/done_is_true", done is True, detail=done)
INFO("  Phase 5 OK: data verified")

master.stop()
INFO("[DONE] test_agent_cache")
