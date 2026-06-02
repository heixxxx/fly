"""E2E distributed data flow test."""
import os
import time
import shutil

import _fly_log as log
import _fly_storage as storage


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
    full_name = db.get_obj_name(obj_name)
    
    db.write_object_raw(obj_name, test_data)
    ds.drain_write_back()
    time.sleep(0.3)
    
    assert ds.has_local_object(full_name), f"Object {full_name} not in local_idx"
    
    success, data_bytes, py_name = ds.try_read_local(full_name)
    assert success, f"try_read_local failed for {full_name}"
    
    data = data_bytes.decode('utf-8') if isinstance(data_bytes, bytes) else data_bytes
    assert data == test_data, f"Data mismatch: {data} != {test_data}"
    
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
        data = f"data_{i}"
        db.write_object_raw(key, data)
    
    ds.drain_write_back()
    time.sleep(0.5)
    
    for i in range(10):
        key = f"multi/obj_{i}"
        full_name = db.get_obj_name(key)
        assert ds.has_local_object(full_name), f"Object {full_name} not found"
        
        result = db.read_object_raw(key)
        assert result == f"data_{i}", f"Mismatch at {i}: {result}"
    
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
    
    assert sm1 is sm2, "StorageManager not singleton"
    
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
    
    db.write_object_raw("obj/a", "data_a")
    ds.drain_write_back()
    time.sleep(0.3)
    
    full_name_a = db.get_obj_name("obj/a")
    assert ds.has_local_object(full_name_a), "Object should be in local index"
    
    ds.update_remote_idx("remote/obj_x", 1, "192.168.1.100", 8080)
    has, worker_id, host, port = ds.lookup_remote_idx("remote/obj_x")
    assert has, "Updated remote object should be in remote index"
    assert worker_id == 1, f"Expected worker_id=1, got {worker_id}"
    assert host == "192.168.1.100", f"Expected host=192.168.1.100, got {host}"
    assert port == 8080, f"Expected port=8080, got {port}"
    
    assert ds.has_remote_location("remote/obj_x"), "Should have remote location"
    
    sm.close_all()
    log.shutdown_log()
    shutil.rmtree(test_dir, ignore_errors=True)
    shutil.rmtree(log_dir, ignore_errors=True)
    print("PASS: test_data_service_remote_index")


test_database_write_updates_local_index()
test_database_multiple_objects()
test_storage_manager_singleton()
test_data_service_remote_index()
print("\nAll distributed data flow tests passed!")
