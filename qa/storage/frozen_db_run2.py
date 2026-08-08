"""Run 2 of frozen DB load test. load_db, verify frozen, verify write fails."""
from _fly_log import INFO
import os
import time



from fly import load_db, get_config
DB_PATH = os.environ.get("FLY_DB_PATH") or os.path.join(get_config().get_str("log_dir"), "db")
from fly import get_config
from fly.runtime import get_agent


get_config().set_int("fail_unscheduleable_tasks", 0)

master = get_agent()

db = load_db(DB_PATH)

assert db.is_frozen(), "DB should still be frozen after load_db"

try:
    result = db.write_object("should_fail", 1)
    assert not result or result == "", \
        f"write_object on frozen DB should return empty, got: {result!r}"
except RuntimeError as e:
    assert "frozen" in str(e).lower(), \
        f"Expected frozen DB error, got: {e}"
    INFO(f"[RUN2] write_object on frozen DB raised RuntimeError as expected: {e}")
INFO("[RUN2] write_object on frozen DB returned empty as expected")

assert os.path.isfile(os.path.join(DB_PATH, "_FROZEN")), \
    "_FROZEN marker should still exist after load_db"

INFO("[RUN2] Verified: loaded DB is frozen, write_object correctly rejected")
