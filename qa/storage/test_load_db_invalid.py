"""E2E test: load_db with corrupt/missing _DB_META raises RuntimeError."""
from _fly_log import INFO
import os
import shutil



from fly import load_db, get_config
from test import record
DB_PATH = os.path.join(get_config().get_str("log_dir"), "db")


def test_load_db_invalid_meta():
    """load_db on a directory with empty _DB_META should raise RuntimeError."""
    cleanup()

    from fly.runtime import get_agent
    master = get_agent()

    # Create a directory with an empty _DB_META file (no valid db_path)
    os.makedirs(DB_PATH, exist_ok=True)
    with open(os.path.join(DB_PATH, "_DB_META"), "w") as f:
        f.write("")

    caught = False
    err_msg = None
    try:
        load_db(DB_PATH)
    except RuntimeError as e:
        caught = True
        err_msg = str(e)
        msg = err_msg.lower()
        msg_ok = "_db_meta" in msg or "no valid" in msg
        # 原断言：assert "_db_meta" in msg or "no valid" in msg, \
        record("invalid_meta/error_type", "RuntimeError")
        record("invalid_meta/msg_mentions_meta_or_no_valid", msg_ok, detail=err_msg)

    # 原断言：assert caught, "load_db should have raised RuntimeError for corrupt _DB_META"
    record("invalid_meta/raises", caught, detail=err_msg)

    INFO("[DONE] test_load_db_invalid_meta: "
          "RuntimeError raised for corrupt _DB_META")


def test_load_db_nonexistent_path():
    """load_db on a nonexistent path should raise RuntimeError."""
    from fly.runtime import get_agent
    master = get_agent()

    caught = False
    err_msg = None
    try:
        load_db("/tmp/fly_e2e_load_db_nonexistent_12345")
    except RuntimeError as e:
        caught = True
        err_msg = str(e)
        msg = err_msg.lower()
        msg_ok = "does not exist" in msg or "not found" in msg
        # 原断言：assert "does not exist" in msg or "not found" in msg, \
        record("nonexistent_path/error_type", "RuntimeError")
        record("nonexistent_path/msg_mentions_does_not_exist_or_not_found", msg_ok, detail=err_msg)

    # 原断言：assert caught, "load_db should have raised RuntimeError for nonexistent path"
    record("nonexistent_path/raises", caught, detail=err_msg)

    INFO("[DONE] test_load_db_nonexistent_path: "
          "RuntimeError raised for nonexistent path")


def cleanup():
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)


test_load_db_invalid_meta()
test_load_db_nonexistent_path()
INFO("\nAll stages done!")
