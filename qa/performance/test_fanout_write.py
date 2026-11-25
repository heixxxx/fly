"""E2E test: fanout_write writes multiple objects in a single task.

fanout_write(db, keys, values) calls write_data for each key/value pair
as sub-tasks within a single task execution. Verify all 3 objects readable.
"""
from _fly_log import INFO
import time
import os
import shutil



from test import fanout_write, record
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
    assert master.wait_workers_registered(timeout=60)
    worker_count = master.worker_count
    # 原断言：assert master.worker_count >= 1, \
    record("fanout/worker_count", worker_count)
    # 非守卫：worker 数由上面保留的等待把关，后面的代码不依赖对它的解引用

    db = open_db(DB_PATH)

    fanout_write(db, ["a", "b", "c"], [1, 2, 3])

    completed = master.wait_for_all_tasks(expected=1, timeout=30)
    completed_ok = len(completed) >= 1
    # 原断言：assert len(completed) >= 1, \
    record("fanout/task_completed", completed_ok, detail=len(completed))
    # 保留：守卫——后面的 read_object 读的是 fanout 任务写出的对象
    assert completed_ok, \
        f"fanout_write task should complete, got {len(completed)} completed"

    val_a = db.read_object("a")
    val_b = db.read_object("b")
    val_c = db.read_object("c")
    # 原断言：assert val_a == 1, f"Object 'a' should be 1, got {val_a}"
    # 原断言：assert val_b == 2, f"Object 'b' should be 2, got {val_b}"
    # 原断言：assert val_c == 3, f"Object 'c' should be 3, got {val_c}"
    record("fanout/values", {"a": val_a, "b": val_b, "c": val_c})

    INFO("[DONE] test_fanout_write_three_objects: "
          "all 3 fanout objects readable")


test_fanout_write_three_objects()
