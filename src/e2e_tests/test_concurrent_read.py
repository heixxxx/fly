import sys
import os
import time
import shutil

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '..', 'bazel-bin', 'src', 'test', 'py'))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '..', 'test', 'py'))

from helpers import setup_master_n_workers
from fly import open_db
from test import entry_task
from test import write_after_freeze
from test import qa_tmp


def test_concurrent_read():
    for p in [qa_tmp("fly_e2e_concurrent")]:
        if os.path.isdir(p):
            shutil.rmtree(p, ignore_errors=True)

    master = setup_master_n_workers(3)

    db5 = open_db(qa_tmp("fly_e2e_concurrent"))
    entry_task(db5)

    completed = master.wait_for_all_tasks(timeout=60)
    new_count = len(completed)
    assert new_count >= 15, f"Expected 15 tasks (10 writes + 3 reads + 1 freeze + entry), got {new_count}"
    print(f"  {new_count} tasks completed", file=sys.stderr)

    s1_total, s1_local, s1_remote = db5.read_object("summary.1")
    s2_total, s2_local, s2_remote = db5.read_object("summary.2")
    s3_total, s3_local, s3_remote = db5.read_object("summary.3")

    assert s1_total == 6, f"summary.1 total: expected 6, got {s1_total}"
    assert s2_total == 18, f"summary.2 total: expected 18, got {s2_total}"
    assert s3_total == 30, f"summary.3 total: expected 30, got {s3_total}"

    total_remote = s1_remote + s2_remote + s3_remote
    print(f"  Reads: local={s1_local+s2_local+s3_local}, remote={total_remote}", file=sys.stderr)

    assert db5.is_frozen(), "DB should be frozen"
    print("  DB is frozen", file=sys.stderr)

    write_after_freeze(db5, "blocked", "nope")
    time.sleep(3)
    s1_again = db5.read_object("summary.1")
    assert s1_again[0] == 6, "Data corrupted after write to frozen DB"
    print("  Frozen DB write safety verified", file=sys.stderr)

    print("[PASS] test_concurrent_read", file=sys.stderr)

    del db5
    master.stop()


if __name__ == "__main__":
    test_concurrent_read()
