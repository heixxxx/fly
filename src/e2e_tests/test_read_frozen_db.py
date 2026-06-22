import sys
import os

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '..', 'bazel-bin', 'src', 'test', 'py'))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from helpers import cleanup, setup_master, wait_completed, DB_PATH
from fly import open_db
from e2e_tasks import write_data, freeze_db, read_data


def test_read_frozen_db():
    cleanup()
    master = setup_master()
    db2 = open_db(DB_PATH + "_frozen")

    write_data(db2, "frozen_data", "can_read")
    wait_completed(master, 1)

    obj_name = db2.get_full_name("frozen_data")
    freeze_db(db2, [obj_name])
    wait_completed(master, 2)
    assert db2.is_frozen()

    finish_obj = db2.get_full_name("finish")
    read_data(db2, "frozen_data", [finish_obj])

    completed = wait_completed(master, 3, timeout=15)
    assert len(completed) >= 3, "Read from frozen db task should complete"
    print("[PASS] test_read_frozen_db", file=sys.stderr)

    del db2
    master.stop()


if __name__ == "__main__":
    test_read_frozen_db()
