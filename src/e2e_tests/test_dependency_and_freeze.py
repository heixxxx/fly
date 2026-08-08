import sys
import os

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '..', 'bazel-bin', 'src', 'test', 'py'))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from helpers import cleanup, setup_master, wait_completed, DB_PATH
from fly import open_db
from test import write_data, freeze_db


def test_dependency_and_freeze():
    cleanup()
    master = setup_master()
    db = open_db(DB_PATH)

    write_data(db, "data1", "value1")
    write_data(db, "data2", "value2")
    wait_completed(master, 2)

    db.read_object("data1")
    db.read_object("data2")
    obj1 = db.get_full_name("data1")
    obj2 = db.get_full_name("data2")

    freeze_db(db, [obj1, obj2])

    completed = wait_completed(master, 3, timeout=20)
    assert len(completed) >= 3, f"Expected 3 tasks, got {len(completed)}"

    assert db.is_frozen(), "DB should be frozen after freeze_db task"
    assert db.read_object("finish") == 1, "finish marker should be 1"
    print("[PASS] test_dependency_and_freeze", file=sys.stderr)

    del db
    master.stop()


if __name__ == "__main__":
    test_dependency_and_freeze()
