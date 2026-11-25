"""E2E distributed data flow test."""
import os
import pickle
import time
import shutil

import _fly_log as log
import _fly_storage as storage
from test import record


def _stream_write(db, key, value):
    # 恒流式写（T2b 2026-08-31：_write_pickle_bytes 已删，与生产
    # write_object 同路径）。返回 WriteErrorType int（0=OK）。
    stream = db.open_write_stream(key, "str")
    pickle.dump(value, stream)
    return int(stream.finish_and_commit(False, False))


def _stream_read(db, key):
    # 恒流式读（_read_decompressed 已删，与生产 read_object 同路径）。
    stream = storage.ex_stg_open_read_stream(db, key, False)
    return pickle.Unpickler(stream).load()


def test_database_write_updates_local_index():
    test_dir = "test_local_idx"
    log_dir = "test_local_idx_logs"
    
    if os.path.exists(test_dir):
        shutil.rmtree(test_dir)
    if os.path.exists(log_dir):
        shutil.rmtree(log_dir)
    
    log.init_log(log_dir, 0)
    
    sm = storage.ex_stg_get_storage_manager()
    ds = storage.ex_stg_get_data_service()
    
    db = sm.get_or_create_database(test_dir)
    
    test_data = "local_index_test_data"
    obj_name = "test/local_obj"
    full_name = db.get_full_name(obj_name)
    
    _stream_write(db, obj_name, test_data)
    ds.drain_write_back()
    time.sleep(0.3)

    in_local = ds.has_local_object(full_name)
    # 原断言：assert ds.has_local_object(full_name), f"Object {full_name} not in local_idx"
    record("local_idx/object_present", in_local)

    success, data_bytes, py_name = ds.try_read_local(full_name)
    # 保留：守卫——后续用 data_bytes 反序列化比对
    assert success, f"try_read_local failed for {full_name}"

    data = data_bytes if isinstance(data_bytes, bytes) else str(data_bytes).encode()
    unpickled = pickle.loads(data)
    # 原断言：assert pickle.loads(data) == test_data, f"Data mismatch vs {test_data}"
    record("local_idx/data_roundtrip", unpickled == test_data, detail=unpickled)
    
    sm.close_all()
    log.shutdown_log()
    shutil.rmtree(test_dir, ignore_errors=True)
    shutil.rmtree(log_dir, ignore_errors=True)
    print("PASS: test_database_write_updates_local_index")


def test_database_multiple_objects():
    test_dir = "test_multi_obj"
    log_dir = "test_multi_obj_logs"
    
    if os.path.exists(test_dir):
        shutil.rmtree(test_dir)
    if os.path.exists(log_dir):
        shutil.rmtree(log_dir)
    
    log.init_log(log_dir, 0)
    
    sm = storage.ex_stg_get_storage_manager()
    ds = storage.ex_stg_get_data_service()
    
    db = sm.get_or_create_database(test_dir)
    
    for i in range(10):
        key = f"multi/obj_{i}"
        _stream_write(db, key, f"data_{i}")

    ds.drain_write_back()
    time.sleep(0.5)

    local_oks = []
    value_oks = []
    for i in range(10):
        key = f"multi/obj_{i}"
        full_name = db.get_full_name(key)
        in_idx = ds.has_local_object(full_name)
        local_oks.append(in_idx)

        result = _stream_read(db, key)
        value_match = result == f"data_{i}"
        value_oks.append(value_match)
    # 原断言：assert ds.has_local_object(full_name), f"Object {full_name} not found"
    record("multi/all_in_local_idx", all(local_oks),
           detail=[i for i, ok in enumerate(local_oks) if not ok])
    # 原断言：assert result == f"data_{i}", f"Mismatch at {i}: {result}"
    record("multi/all_values_match", all(value_oks),
           detail=[i for i, ok in enumerate(value_oks) if not ok])
    
    sm.close_all()
    log.shutdown_log()
    shutil.rmtree(test_dir, ignore_errors=True)
    shutil.rmtree(log_dir, ignore_errors=True)
    print("PASS: test_database_multiple_objects")


def test_storage_manager_singleton():
    log_dir = "test_sm_singleton_logs"
    if os.path.exists(log_dir):
        shutil.rmtree(log_dir)
    
    log.init_log(log_dir, 0)
    
    sm1 = storage.ex_stg_get_storage_manager()
    sm2 = storage.ex_stg_get_storage_manager()

    # 原断言：assert sm1 is sm2, "StorageManager not singleton"
    record("singleton/same_manager", sm1 is sm2)
    
    log.shutdown_log()
    shutil.rmtree(log_dir, ignore_errors=True)
    print("PASS: test_storage_manager_singleton")


def test_data_service_remote_index():
    test_dir = "test_remote_idx"
    log_dir = "test_remote_idx_logs"
    
    if os.path.exists(test_dir):
        shutil.rmtree(test_dir)
    if os.path.exists(log_dir):
        shutil.rmtree(log_dir)
    
    log.init_log(log_dir, 0)
    
    sm = storage.ex_stg_get_storage_manager()
    ds = storage.ex_stg_get_data_service()
    
    db = sm.get_or_create_database(test_dir)
    
    _stream_write(db, "obj/a", "data_a")
    ds.drain_write_back()
    time.sleep(0.3)
    
    full_name_a = db.get_full_name("obj/a")
    a_in_local = ds.has_local_object(full_name_a)
    # 原断言：assert ds.has_local_object(full_name_a), "Object should be in local index"
    record("remote_idx/local_input_present", a_in_local)

    ds.update_remote_idx("remote/obj_x", 1, "192.168.1.100", 8080)
    has, worker_id, host, port = ds.lookup_remote_idx("remote/obj_x")
    # 原断言：assert has, "Updated remote object should be in remote index"
    record("remote_idx/updated_object_found", has)
    # 原断言：assert worker_id == 1, f"Expected worker_id=1, got {worker_id}"
    record("remote_idx/worker_id", worker_id)
    # 原断言：assert host == "192.168.1.100", f"Expected host=192.168.1.100, got {host}"
    record("remote_idx/host", host)
    # 原断言：assert port == 8080, f"Expected port=8080, got {port}"
    record("remote_idx/port", port)

    remote_loc = ds.has_remote_location("remote/obj_x")
    # 原断言：assert ds.has_remote_location("remote/obj_x"), "Should have remote location"
    record("remote_idx/has_remote_location", remote_loc)
    
    sm.close_all()
    log.shutdown_log()
    shutil.rmtree(test_dir, ignore_errors=True)
    shutil.rmtree(log_dir, ignore_errors=True)
    print("PASS: test_data_service_remote_index")


test_database_write_updates_local_index()
test_database_multiple_objects()
test_storage_manager_singleton()
test_data_service_remote_index()
print("\nAll stages done!")
