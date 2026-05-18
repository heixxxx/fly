import sys
import os

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '..', 'bazel-bin', 'src', 'test', 'py'))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from helpers import cleanup, setup_master, wait_completed, DB_PATH
from fly import open_db
from e2e_tasks import fanout_write


def test_recursive_submit():
    cleanup()
    master = setup_master()
    db4 = open_db(DB_PATH + "_fanout")

    fanout_write(db4, ["a", "b", "c"], [10, 20, 30])

    completed = wait_completed(master, 4, timeout=30)
    new_count = len(completed)
    assert new_count >= 4, f"Expected 4 tasks, got {new_count}"
    print(f"[PASS] test_recursive_submit: {new_count} tasks completed", file=sys.stderr)

    for k, v in [("a", 10), ("b", 20), ("c", 30)]:
        assert db4.read_object(k) == v, f"Expected {v} for {k}, got {db4.read_object(k)}"
    print("  Verified db reads: a=10, b=20, c=30", file=sys.stderr)

    del db4
    master.stop()


if __name__ == "__main__":
    test_recursive_submit()
