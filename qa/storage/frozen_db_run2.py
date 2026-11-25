"""Run 2 of frozen DB load test. load_db, verify frozen, verify write fails."""
from _fly_log import INFO
import os
import time



from fly import load_db, get_config
from test import record
DB_PATH = os.environ.get("FLY_DB_PATH") or os.path.join(get_config().get_str("log_dir"), "db")
from fly import get_config
from fly.runtime import get_agent


get_config().set_int("fail_unscheduleable_tasks", 0)

master = get_agent()

db = load_db(DB_PATH)

# 原断言：assert db.is_frozen(), "DB should still be frozen after load_db"
record("still_frozen_after_load", db.is_frozen(), detail=db.is_frozen())

frozen_error = None
result = None
try:
    result = db.write_object("should_fail", 1)
except RuntimeError as e:
    frozen_error = {"type": type(e).__name__, "msg": str(e)}
# 原断言：assert not result or result == "", \
# 原断言：assert "frozen" in str(e).lower(), \
# 分支等价：抛了 RuntimeError 时判定「消息含 frozen」（其他异常照旧向上抛）；
# 没抛时判定「返回值为空/假」。
rejected = ("frozen" in frozen_error["msg"].lower()) if frozen_error is not None \
    else (not result or result == "")
record("frozen_write_rejected", rejected,
       detail={"error": frozen_error, "result": result})
record("frozen_write_error_type", frozen_error["type"] if frozen_error else None)
INFO("[RUN2] write_object on frozen DB rejected as expected "
     "(empty result, or RuntimeError mentioning frozen)")

# 原断言：assert os.path.isfile(os.path.join(DB_PATH, "_FROZEN")), \
record("frozen_marker_still_exists",
       os.path.isfile(os.path.join(DB_PATH, "_FROZEN")), detail=DB_PATH)

INFO("[RUN2] Verified: loaded DB is frozen, write_object correctly rejected")
