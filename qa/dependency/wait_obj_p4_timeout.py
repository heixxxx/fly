"""Phase 4: @wait_obj raises RuntimeError when data can never appear."""
from _fly_log import INFO
import time
import os
import shutil


from fly import open_db, get_config, wait_obj
from test import record
DB_PATH = os.environ.get("FLY_DB_PATH") or os.path.join(get_config().get_str("log_dir"), "db")
from fly.runtime import get_agent


def cleanup():
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)


cleanup()
get_config().set_int("fail_unscheduleable_tasks", 0)

master = get_agent()
master.launch_local_workers([{}])
assert master.wait_workers_registered(timeout=60)
# 原断言：assert master.worker_count >= 1
record("setup/worker_connected", master.worker_count >= 1,
       detail=master.worker_count)

db = open_db(DB_PATH)

@wait_obj(inputs=lambda d, k: [d.get_full_name(k)])
def wait_phantom(d, k):
    return d.read_object(k)

try:
    wait_phantom(db, "phantom")
    # 保留：否定性检查 try 块内的 assert False（未被 except RuntimeError 捕获）
    assert False, "Should have raised RuntimeError"
except RuntimeError as e:
    phantom_err = str(e)
    # 原断言：assert "cannot be produced" in str(e)
    record("phantom/msg_mentions_cannot_be_produced", "cannot be produced" in phantom_err,
           detail=phantom_err)
except Exception as e:
    # 保留：兜底分支——非 RuntimeError 时让脚本以 AssertionError 失败
    assert False, f"Expected RuntimeError, got {type(e).__name__}: {e}"

INFO("[DONE] test_wait_obj_timeout")
