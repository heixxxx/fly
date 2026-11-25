"""E2E database freeze protection test.

Verifies freeze semantics:
  - Database writes succeed before freeze
  - Database writes are rejected after freeze
  - Reads still work after freeze
  - DataService local_idx remains accessible after freeze
  - Multiple Database instances: freeze one doesn't affect others
"""
import os
import pickle
import time
import shutil

import _fly_log as log
import _fly_storage as storage

from test import record


def _raw_write(db, key, value: str):
    # 直写路径 _write_pickle_bytes 已删除（T2b 2026-08-31，生产零使用）——
    # 迁移到恒流式写（open_write_stream → finish_and_commit，同
    # WriteErrorType int 返回语义；frozen 时 open 返回 None → FROZEN_DB）。
    stream = db.open_write_stream(key, "str")
    if stream is None:
        return 1  # WriteErrorType.FROZEN_DB（common/cpp/error_types.h 枚举序）
    pickle.dump(value, stream)
    return int(stream.finish_and_commit(False, False))


def _raw_read(db, key) -> str:
    # 恒流式读（生产 read_object 同款原语；_read_decompressed 已删）。
    stream = storage.ex_stg_open_read_stream(db, key, False)
    return pickle.Unpickler(stream).load()



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

        _raw_write(db, "before_freeze_1", "data_1")
        _raw_write(db, "before_freeze_2", "data_2")
        ds.drain_write_back()
        time.sleep(0.3)

        db.freeze()
        db_frozen = db.is_frozen()
        # 原断言：assert db.is_frozen()
        record("rw/db_frozen", db_frozen, detail=db_frozen)

        read_1 = _raw_read(db, "before_freeze_1")
        # 原断言：assert _raw_read(db, "before_freeze_1") == "data_1"
        record("rw/read_before_freeze_1", read_1)
        read_2 = _raw_read(db, "before_freeze_2")
        # 原断言：assert _raw_read(db, "before_freeze_2") == "data_2"
        record("rw/read_before_freeze_2", read_2)

        result = _raw_write(db, "after_freeze", "should_fail")
        # _write_pickle_bytes returns WriteErrorType int: 0=OK, 1=FROZEN_DB, etc.
        write_rejected = result != 0
        # 原断言：assert result != 0, f"Write after freeze should fail (non-zero error code), got: {result!r}"
        record("rw/write_after_freeze_rejected", write_rejected, detail=result)

        sm.close_all()
        print("DONE: test_freeze_prevents_write_allows_read")

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

        _raw_write(db, "idx_obj", "idx_data")
        ds.drain_write_back()
        time.sleep(0.3)

        full_name = db.get_full_name("idx_obj")
        has_local = ds.has_local_object(full_name)
        # 原断言：assert ds.has_local_object(full_name)
        record("idx/has_local_object", has_local, detail=has_local)

        db.freeze()

        success, data_bytes, py_name = ds.try_read_local(full_name)
        # 原断言：assert success
        record("idx/try_read_local_success", success, detail=success)
        data = data_bytes if isinstance(data_bytes, bytes) else str(data_bytes).encode()
        idx_data = pickle.loads(data)
        # 原断言：assert pickle.loads(data) == "idx_data"
        record("idx/local_data", idx_data)

        sm.close_all()
        print("DONE: test_freeze_preserves_local_index")

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

        _raw_write(db1, "shared_key", "from_db1")
        _raw_write(db2, "shared_key", "from_db2")
        ds = storage.ex_stg_get_data_service()
        ds.drain_write_back()
        time.sleep(0.3)

        db1.freeze()

        result = _raw_write(db1, "after_freeze", "should_fail")
        write_rejected = result != 0
        # 原断言：assert result != 0, f"db1 should reject writes after freeze, got: {result!r}"
        record("iso/db1_write_after_freeze_rejected", write_rejected, detail=result)

        _raw_write(db2, "still_writable", "from_db2_after_freeze")
        ds.drain_write_back()

        still_writable_value = _raw_read(db2, "still_writable")
        # 原断言：assert _raw_read(db2, "still_writable") == "from_db2_after_freeze"
        record("iso/db2_still_writable_value", still_writable_value)

        sm.close_all()
        print("DONE: test_freeze_isolation_between_databases")

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

        # 恒流式 typed 读（_read_streaming 已删，T2b 2026-08-31）：freeze 后
        # C++ 对象仍可经权威重建路径读取。
        result = storage.EXStgIndexEntry._read_from_db(db, "typed/entry", "none")
        typed_object_name = result.object_name
        # 原断言：assert result.object_name == "test/entry"
        record("typed/entry_object_name", typed_object_name)
        # 原断言：assert result.offset == 100 and result.size == 512
        record("typed/entry_offset", result.offset)
        record("typed/entry_size", result.size)

        sm.close_all()
        print("DONE: test_freeze_with_typed_objects")

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
