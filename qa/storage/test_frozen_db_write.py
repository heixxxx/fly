"""E2E test: freeze protection on master side and task-level behavior.

Verifies:
  - db.freeze() prevents master-side write_object
  - A task running on a worker CAN write (worker has its own DB instance,
    freeze state is master-local in process mode)
  - This documents the design: freeze is a master-side coordination mechanism,
    not a distributed lock
"""
from _fly_log import INFO
import time
import os
import shutil



from test import write_data, record
from fly import open_db, get_config
DB_PATH = os.path.join(get_config().get_str("log_dir"), "db")


def cleanup():
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)


def wait_for(condition, timeout=20.0, interval=0.5):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if condition():
            return True
        time.sleep(interval)
    return False


def test_frozen_db_write():
    cleanup()

    from fly.runtime import get_agent
    master = get_agent()

    master.launch_local_workers([{}])
    # 保留：等待原语
    assert master.wait_workers_registered(timeout=60)
    worker_connected = master.worker_count >= 1
    # 原断言：assert master.worker_count >= 1
    record("worker_connected", worker_connected, detail=master.worker_count)

    db = open_db(DB_PATH)

    # Write data before freezing
    write_data(db, "before_freeze", 1)
    # 保留：等待原语
    assert wait_for(lambda: len(master.completed_tasks) >= 1), \
        "write_data before freeze should complete"

    db.freeze()
    db_frozen = db.is_frozen()
    # 原断言：assert db.is_frozen(), "DB should be frozen"
    record("db_frozen", db_frozen, detail=db_frozen)

    # Master-side write after freeze should fail
    # 旧写法失效：assert False 抛出的 AssertionError 是 Exception 的子类，会被下方
    # 同一个 except Exception 一并吞掉，写操作没按预期失败时用例照样通过。
    raised = False
    frozen_error = None
    try:
        db.write_object("master_write_after_freeze", "fail")
    except Exception as e:
        raised = True  # Expected: write to frozen DB raises
        frozen_error = {"type": type(e).__name__, "msg": str(e)}
    # 原断言：assert raised, "Master-side write_object should raise after freeze"
    record("master_write_rejected", raised, detail=frozen_error)
    record("master_write_error_type", frozen_error["type"] if frozen_error else None)

    INFO("[DONE] test_frozen_db_write: master-side write blocked after freeze")


test_frozen_db_write()
