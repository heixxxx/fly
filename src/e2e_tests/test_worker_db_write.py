import sys
import os

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '..', 'bazel-bin', 'src', 'test', 'py'))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from helpers import cleanup, setup_master, wait_completed, DB_PATH
from fly import open_db
from e2e_tasks import write_data


def test_worker_db_write():
    cleanup()
    master = setup_master()
    db = open_db(DB_PATH)

    write_data(db, "x", 42)
    write_data(db, "y", "hello")

    completed = wait_completed(master, 2)
    assert len(completed) >= 2, f"Expected 2 completed, got {len(completed)}"

    assert db.read_object("x") == 42
    assert db.read_object("y") == "hello"
    print("[PASS] test_worker_db_write", file=sys.stderr)

    del db
    master.stop()


if __name__ == "__main__":
    test_worker_db_write()
