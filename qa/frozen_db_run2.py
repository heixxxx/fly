"""Run 2 of frozen DB load test. load_db, verify frozen, verify write fails."""
import os
import sys
import time

DB_PATH = "/tmp/fly_e2e_frozen_load_db"

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '..', 'src'))

from fly import load_db
from fly import get_config
from fly.runtime import get_agent


def main():
    get_config().set_int("fail_unscheduleable_tasks", 0)

    master = get_agent()
    master.start()

    db = load_db(DB_PATH)

    assert db.is_frozen(), "DB should still be frozen after load_db"

    write_raised = False
    try:
        db.write_object("should_fail", 1)
    except Exception as e:
        write_raised = True
        print(f"[RUN2] write_object raised as expected: {e}", file=sys.stderr)

    assert write_raised, \
        "write_object on frozen DB should have raised an exception"

    assert os.path.isfile(os.path.join(DB_PATH, "_FROZEN")), \
        "_FROZEN marker should still exist after load_db"

    print("[RUN2] Verified: loaded DB is frozen, write_object correctly rejected",
          file=sys.stderr)


if __name__ == "__main__":
    main()
