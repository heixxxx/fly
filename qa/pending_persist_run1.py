"""Helper: Run 1 for pending_task_persist test.

Submit tasks: some complete, some fail with unresolvable deps.
Stop master, verify failed_tasks.bin is created.
"""
import time
import sys
import os
import shutil

DB_PATH = "/tmp/fly_e2e_pending_persist_db"

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '..', 'src'))

from e2e_tasks import write_data, read_data, gpu_write
from fly import open_db, get_config


def wait_for(condition, timeout=20.0, interval=0.5):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if condition():
            return True
        time.sleep(interval)
    return False


def run1():
    get_config().set_int("fail_unscheduleable_tasks", 1)

    from fly.runtime import get_agent
    master = get_agent()
    if not master._running:
        master.start()

    master.launch_local_workers([{}])
    assert wait_for(lambda: master._agent.get_connection_count() >= 1), \
        "Worker should connect"

    db = open_db(DB_PATH)
    log_dir = get_config().get_str("log_dir")
    failed_file = os.path.join(log_dir, "failed_tasks.bin")

    # Submit tasks that will complete
    write_data(db, "real_key_1", 10)
    write_data(db, "real_key_2", 20)

    # Submit task with unresolvable dep -> will FAIL
    read_data(db, "result_1", [db.get_obj_name("phantom_data")])

    # Submit task requiring GPU -> will FAIL (no gpu worker)
    gpu_write(db, "gpu_result", 42)

    # Wait for completions and failures
    assert wait_for(lambda: len(master.completed_tasks) >= 2), \
        f"Expected 2 completed, got {len(master.completed_tasks)}"
    assert wait_for(lambda: len(master.failed_tasks) >= 2), \
        f"Expected 2 failed, got {len(master.failed_tasks)}"

    completed = len(master.completed_tasks)
    failed = len(master.failed_tasks)
    print(f"  Run1: {completed} completed, {failed} failed", file=sys.stderr)

    # Verify failed_tasks.bin exists before stop
    assert wait_for(lambda: os.path.isfile(failed_file)), \
        f"failed_tasks.bin should exist at {failed_file}"

    file_size = os.path.getsize(failed_file)
    assert file_size > 0, "failed_tasks.bin should not be empty"
    print(f"  Run1: failed_tasks.bin exists ({file_size} bytes)", file=sys.stderr)

    del db
    master.stop()
    print("  Run1: master stopped", file=sys.stderr)


if __name__ == "__main__":
    run1()
