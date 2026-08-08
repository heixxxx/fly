import sys
import os
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '..', 'bazel-bin', 'src', 'test', 'py'))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from helpers import cleanup, setup_master, wait_completed, DB_PATH
from fly import open_db
from test import write_data, freeze_db, write_after_freeze


def test_write_frozen_db_fails():
    cleanup()
    master = setup_master()
    db3 = open_db(DB_PATH + "_blocked")

    write_data(db3, "pre", "exists")
    wait_completed(master, 1)

    freeze_db(db3, [db3.get_full_name("pre")])
    wait_completed(master, 2)
    assert db3.is_frozen()

    write_after_freeze(db3, "blocked", "nope")

    time.sleep(5)

    assert db3.read_object("pre") == "exists"
    print("[PASS] test_write_frozen_db_fails", file=sys.stderr)

    del db3
    master.stop()


if __name__ == "__main__":
    test_write_frozen_db_fails()
