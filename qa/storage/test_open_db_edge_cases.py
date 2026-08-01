"""E2E test: open_db edge cases.

Scenarios:
  1. open_db on existing path auto-renames to path.1
  2. port property: master.port > 0 after start
  3. data_path parameter sets get_data_path()
  4. get_db_path() and get_data_path() return correct values
"""
from _fly_log import INFO
import time
import os
import shutil

DB_PATH_AUTO = "/tmp/fly_e2e_open_db_auto_db"
DB_PATH_DATAPATH = "/tmp/fly_e2e_open_db_datapath_db"
CUSTOM_DATA_PATH = "/tmp/fly_e2e_custom_data_path"


from fly import open_db, get_config
DB_PATH = os.path.join(get_config().get_str("log_dir"), "db")


def cleanup():
    for p in [DB_PATH, DB_PATH + ".1", DB_PATH_AUTO, DB_PATH_DATAPATH, CUSTOM_DATA_PATH]:
        if os.path.isdir(p):
            shutil.rmtree(p, ignore_errors=True)


def test_open_db_path_conflict_auto_rename():
    """open_db same path twice: second call auto-renames to path.1."""
    cleanup()

    from fly.runtime import get_agent
    master = get_agent()
    master.launch_local_workers([{}])
    for i in range(40):
        if master.worker_count >= 1:
            break
        time.sleep(0.5)
    assert master.worker_count >= 1, \
        "Worker should connect to master"

    db1 = open_db(DB_PATH)
    assert os.path.isdir(DB_PATH), \
        f"First DB dir should exist at {DB_PATH}"

    db2 = open_db(DB_PATH)
    assert os.path.isdir(DB_PATH + ".1"), \
        f"Second DB dir should auto-rename to {DB_PATH}.1"
    assert db1.get_db_path() == DB_PATH, \
        f"db1 db_path should be {DB_PATH}, got {db1.get_db_path()}"
    assert db2.get_db_path() == DB_PATH + ".1", \
        f"db2 db_path should be {DB_PATH}.1, got {db2.get_db_path()}"

    INFO("[PASS] test_open_db_path_conflict_auto_rename: "
          "second open_db auto-renames to path.1")


def test_port_property():
    """master.port > 0 after start."""
    from fly.runtime import get_agent
    master = get_agent()

    port = master.port
    assert port > 0, \
        f"Master port should be > 0 after start, got {port}"

    INFO("[PASS] test_port_property: "
          f"master.port={port}")


def test_data_path_parameter():
    """open_db(path, data_path=custom) sets get_data_path()."""
    if os.path.isdir(DB_PATH_DATAPATH):
        shutil.rmtree(DB_PATH_DATAPATH, ignore_errors=True)
    if os.path.isdir(CUSTOM_DATA_PATH):
        shutil.rmtree(CUSTOM_DATA_PATH, ignore_errors=True)

    from fly.runtime import get_agent
    master = get_agent()
    master.launch_local_workers([{}])
    for i in range(40):
        if master.worker_count >= 1:
            break
        time.sleep(0.5)
    assert master.worker_count >= 1

    db = open_db(DB_PATH_DATAPATH, data_path=CUSTOM_DATA_PATH)
    actual_data_path = db.get_data_path()
    assert actual_data_path == CUSTOM_DATA_PATH, \
        f"get_data_path() should return '{CUSTOM_DATA_PATH}', got '{actual_data_path}'"

    INFO("[PASS] test_data_path_parameter: "
          f"data_path={CUSTOM_DATA_PATH} returned by get_data_path()")


def test_get_db_path_get_data_path_getters():
    """get_db_path() == DB_PATH, get_data_path() returns '' or data_path."""
    cleanup()

    from fly.runtime import get_agent
    master = get_agent()
    master.launch_local_workers([{}])
    for i in range(40):
        if master.worker_count >= 1:
            break
        time.sleep(0.5)
    assert master.worker_count >= 1

    db = open_db(DB_PATH)
    db_path = db.get_db_path()
    data_path = db.get_data_path()
    assert db_path == DB_PATH, \
        f"get_db_path() should be '{DB_PATH}', got '{db_path}'"
    assert isinstance(data_path, str), \
        f"get_data_path() should return str, got {type(data_path)}"
    assert data_path == "", \
        f"get_data_path() should be '' when not set, got '{data_path}'"

    INFO("[PASS] test_get_db_path_get_data_path_getters: "
          f"db_path={db_path}, data_path='{data_path}'")


test_open_db_path_conflict_auto_rename()
INFO("")
test_port_property()
INFO("")
test_data_path_parameter()
INFO("")
test_get_db_path_get_data_path_getters()
INFO("\nAll open_db edge case E2E tests passed!")
