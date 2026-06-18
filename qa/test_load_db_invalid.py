"""E2E test: load_db with corrupt/missing _DB_META raises RuntimeError."""
from _fly_log import INFO
import sys
import os
import shutil

DB_PATH = f"/tmp/fly_e2e_load_db_invalid_db_{os.getpid()}"

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '..', 'src'))

from fly import load_db


def test_load_db_invalid_meta():
    """load_db on a directory with empty _DB_META should raise RuntimeError."""
    cleanup()

    from fly.runtime import get_agent
    master = get_agent()

    # Create a directory with an empty _DB_META file (no valid db_id)
    os.makedirs(DB_PATH, exist_ok=True)
    with open(os.path.join(DB_PATH, "_DB_META"), "w") as f:
        f.write("")

    caught = False
    try:
        load_db(DB_PATH)
    except RuntimeError as e:
        caught = True
        msg = str(e).lower()
        assert "_db_meta" in msg or "no valid" in msg, \
            f"Expected error about _DB_META, got: {e}"

    assert caught, "load_db should have raised RuntimeError for corrupt _DB_META"

    INFO("[PASS] test_load_db_invalid_meta: "
          "RuntimeError raised for corrupt _DB_META")


def test_load_db_nonexistent_path():
    """load_db on a nonexistent path should raise RuntimeError."""
    from fly.runtime import get_agent
    master = get_agent()

    caught = False
    try:
        load_db("/tmp/fly_e2e_load_db_nonexistent_12345")
    except RuntimeError as e:
        caught = True
        msg = str(e).lower()
        assert "does not exist" in msg or "not found" in msg, \
            f"Expected error about nonexistent path, got: {e}"

    assert caught, "load_db should have raised RuntimeError for nonexistent path"

    INFO("[PASS] test_load_db_nonexistent_path: "
          "RuntimeError raised for nonexistent path")


def cleanup():
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)


test_load_db_invalid_meta()
test_load_db_nonexistent_path()
INFO("\nAll tests passed!")
