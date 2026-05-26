"""Phase 1: Master writes data → @wait_obj sees it immediately → reads and verifies."""
import time
import sys
import os
import shutil

DB_PATH = "/tmp/fly_e2e_wait_obj_p1_db"

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '..', 'src'))

from fly import open_db, get_config, wait_obj
from fly.runtime import get_agent


def cleanup():
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)


def main():
    cleanup()
    get_config().set_int("fail_unscheduleable_tasks", 0)

    master = get_agent()
    master.start()
    master.launch_local_workers([{}])
    for i in range(40):
        if master._agent.get_connection_count() >= 1:
            break
        time.sleep(0.5)
    assert master._agent.get_connection_count() >= 1

    db = open_db(DB_PATH)

    @wait_obj(inputs=lambda d, k: [d.get_obj_name(k)])
    def wait_master_data(d, k):
        assert d.read_object(k) == "sync_data"
        return "ok"

    db.write_object("master_key", "sync_data")
    result = wait_master_data(db, "master_key")
    assert result == "ok"

    master.stop()
    print("[PASS] test_wait_obj_master_write", file=sys.stderr)


if __name__ == "__main__":
    main()
