"""E2E database freeze protection test.

Verifies freeze semantics:
  - Database writes succeed before freeze
  - Database writes are rejected after freeze
  - Reads still work after freeze
  - DataService local_idx remains accessible after freeze
  - Multiple Database instances: freeze one doesn't affect others
"""
import os
import time
import shutil

import _fly_log as log
import _fly_storage as storage



def test_freeze_prevents_write_allows_read():
    if os.path.exists("test_freeze_rw_logs"):
        shutil.rmtree("test_freeze_rw_logs")
    test_dir = "test_freeze_rw"
    if os.path.exists(test_dir):
        shutil.rmtree(test_dir)

    log.init_log("test_freeze_rw_logs", 0)

    try:
        sm = storage.ex_stg_get_storage_manager()
        ds = storage.ex_stg_get_data_service()
        db = sm.get_or_create_database(test_dir)

        db.write_object_raw("before_freeze_1", "data_1")
        db.write_object_raw("before_freeze_2", "data_2")
        ds.drain_write_back()
        time.sleep(0.3)

        db.freeze()
        assert db.is_frozen()

        assert db.read_object_raw("before_freeze_1") == "data_1"
        assert db.read_object_raw("before_freeze_2") == "data_2"

        result = db.write_object_raw("after_freeze", "should_fail")
        assert not result or result == "", f"Write after freeze should return empty, got: {result!r}"

        sm.close_all()
        print("PASS: test_freeze_prevents_write_allows_read")

    finally:
        log.shutdown_log()
        shutil.rmtree(test_dir, ignore_errors=True)
        shutil.rmtree("test_freeze_rw_logs", ignore_errors=True)


def test_freeze_preserves_local_index():
    if os.path.exists("test_freeze_idx_logs"):
        shutil.rmtree("test_freeze_idx_logs")
    test_dir = "test_freeze_idx"
    if os.path.exists(test_dir):
        shutil.rmtree(test_dir)

    log.init_log("test_freeze_idx_logs", 0)

    try:
        sm = storage.ex_stg_get_storage_manager()
        ds = storage.ex_stg_get_data_service()
        db = sm.get_or_create_database(test_dir)

        db.write_object_raw("idx_obj", "idx_data")
        ds.drain_write_back()
        time.sleep(0.3)

        full_name = db.get_obj_name("idx_obj")
        assert ds.has_local_object(full_name)

        db.freeze()

        success, data_bytes, py_name = ds.try_read_local(full_name)
        assert success
        data = data_bytes.decode('utf-8') if isinstance(data_bytes, bytes) else data_bytes
        assert "idx_data" in data

        sm.close_all()
        print("PASS: test_freeze_preserves_local_index")

    finally:
        log.shutdown_log()
        shutil.rmtree(test_dir, ignore_errors=True)
        shutil.rmtree("test_freeze_idx_logs", ignore_errors=True)


def test_freeze_isolation_between_databases():
    if os.path.exists("test_freeze_iso_logs"):
        shutil.rmtree("test_freeze_iso_logs")
    test_dir_1 = "test_freeze_iso_1"
    test_dir_2 = "test_freeze_iso_2"
    if os.path.exists(test_dir_1):
        shutil.rmtree(test_dir_1)
    if os.path.exists(test_dir_2):
        shutil.rmtree(test_dir_2)

    log.init_log("test_freeze_iso_logs", 0)

    try:
        sm = storage.ex_stg_get_storage_manager()
        db1 = sm.get_or_create_database(test_dir_1)
        db2 = sm.get_or_create_database(test_dir_2)

        db1.write_object_raw("shared_key", "from_db1")
        db2.write_object_raw("shared_key", "from_db2")
        ds = storage.ex_stg_get_data_service()
        ds.drain_write_back()
        time.sleep(0.3)

        db1.freeze()

        result = db1.write_object_raw("after_freeze", "should_fail")
        assert not result or result == "", f"db1 should reject writes after freeze, got: {result!r}"

        db2.write_object_raw("still_writable", "from_db2_after_freeze")
        ds.drain_write_back()

        assert db2.read_object_raw("still_writable") == "from_db2_after_freeze"

        sm.close_all()
        print("PASS: test_freeze_isolation_between_databases")

    finally:
        log.shutdown_log()
        shutil.rmtree(test_dir_1, ignore_errors=True)
        shutil.rmtree(test_dir_2, ignore_errors=True)
        shutil.rmtree("test_freeze_iso_logs", ignore_errors=True)


def test_freeze_with_typed_objects():
    if os.path.exists("test_freeze_typed_logs"):
        shutil.rmtree("test_freeze_typed_logs")
    test_dir = "test_freeze_typed"
    if os.path.exists(test_dir):
        shutil.rmtree(test_dir)

    log.init_log("test_freeze_typed_logs", 0)

    try:
        sm = storage.ex_stg_get_storage_manager()
        db = sm.get_or_create_database(test_dir)

        ds = storage.ex_stg_get_data_service()
        entry = storage.EXStgIndexEntry("test/entry", "data.dat", 100, 512, False, 0)
        entry._write_to_db(db, "typed/entry", "EXStgIndexEntry", False)
        ds.drain_write_back()
        time.sleep(0.3)

        db.freeze()

        data_bytes, py_name = db._read_streaming("typed/entry")
        assert py_name == "EXStgIndexEntry", f"Expected EXStgIndexEntry, got {py_name}"

        sm.close_all()
        print("PASS: test_freeze_with_typed_objects")

    finally:
        log.shutdown_log()
        shutil.rmtree(test_dir, ignore_errors=True)
        shutil.rmtree("test_freeze_typed_logs", ignore_errors=True)


test_freeze_prevents_write_allows_read()
print()
test_freeze_preserves_local_index()
print()
test_freeze_isolation_between_databases()
print()
test_freeze_with_typed_objects()
print("\nAll database freeze protection tests passed!")
