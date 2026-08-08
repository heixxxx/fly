"""E2E test: fanout_write writes multiple objects in a single task.

fanout_write(db, keys, values) calls write_data for each key/value pair
as sub-tasks within a single task execution. Verify all 3 objects readable.
"""
from _fly_log import INFO
import time
import os
import shutil



from test import fanout_write
from fly import open_db, get_config
DB_PATH = os.path.join(get_config().get_str("log_dir"), "db")


def cleanup():
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)


def test_fanout_write_three_objects():
    """fanout_write(db, ["a","b","c"], [1,2,3]) produces 1 task writing 3 objects."""
    cleanup()
    get_config().set_int("fail_unscheduleable_tasks", 0)

    from fly.runtime import get_agent
    master = get_agent()
    master.launch_local_workers([{}])
    for i in range(40):
        if master.worker_count >= 1:
            break
        time.sleep(0.5)
    assert master.worker_count >= 1, \
        "Worker should connect to master"

    db = open_db(DB_PATH)

    fanout_write(db, ["a", "b", "c"], [1, 2, 3])

    completed = master.wait_for_all_tasks(expected=1, timeout=30)
    assert len(completed) >= 1, \
        f"fanout_write task should complete, got {len(completed)} completed"

    val_a = db.read_object("a")
    val_b = db.read_object("b")
    val_c = db.read_object("c")
    assert val_a == 1, f"Object 'a' should be 1, got {val_a}"
    assert val_b == 2, f"Object 'b' should be 2, got {val_b}"
    assert val_c == 3, f"Object 'c' should be 3, got {val_c}"

    INFO("[PASS] test_fanout_write_three_objects: "
          "all 3 fanout objects readable")


test_fanout_write_three_objects()
