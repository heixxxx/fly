"""E2E stress test: long dependency chain with serial tasks.

Verifies a 20-task serial dependency chain:
  write(A) -> read(A)+write(B) -> read(B)+write(C) -> ... -> read(T)+write(U)
Each task depends on the previous task's output.
"""
from _fly_log import INFO
import time
import os
import shutil



from test import write_data, increment
from fly import open_db, get_config
DB_PATH = os.path.join(get_config().get_str("log_dir"), "db")


def cleanup():
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)


def wait_for(condition, timeout=120.0, interval=0.5):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if condition():
            return True
        time.sleep(interval)
    return False


def test_dependency_chain():
    cleanup()
    get_config().set_int("fail_unscheduleable_tasks", 0)

    from fly.runtime import get_agent
    master = get_agent()

    master.launch_local_workers([{}])

    assert master.wait_for_workers(1), \
        "Worker should connect"

    db = open_db(DB_PATH)

    db.write_object("chain_0", 0)

    chain_length = 20
    for i in range(1, chain_length + 1):
        increment(db, f"chain_{i-1}", f"chain_{i}", [db.get_full_name(f"chain_{i-1}")])

    assert wait_for(lambda: len(master.completed_tasks) >= chain_length, timeout=120.0), \
        f"All {chain_length} tasks should complete, got {len(master.completed_tasks)} completed"

    assert len(master.completed_tasks) >= chain_length, \
        f"Expected >= {chain_length} completed, got {len(master.completed_tasks)}"
    assert len(master.failed_tasks) == 0, \
        f"Expected 0 failed, got {len(master.failed_tasks)}"

    final = db.read_object(f"chain_{chain_length}")
    assert final == chain_length, \
        f"chain_{chain_length} should be {chain_length}, got {final}"

    INFO(f"[PASS] test_dependency_chain: {chain_length}-step serial chain, final={final}")


test_dependency_chain()
INFO("\nAll tests passed!")
