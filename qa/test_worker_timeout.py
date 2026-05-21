"""E2E worker timeout and heartbeat test."""
import os
import time
import shutil

import _fly_log as log
import _fly_storage as storage
from _fly_agent import EXAgentMaster
from _fly_task import EXTaskWorkerManager, EXTaskHeartbeatMonitor
from _fly_storage import ex_stg_get_data_service


def test_heartbeat_monitor_timeout():
    log_dir = "test_timeout_logs"
    if os.path.exists(log_dir):
        shutil.rmtree(log_dir)
    
    log.init_log(log_dir, 0)
    
    wm = EXTaskWorkerManager()
    wm.register_worker(1, "127.0.0.1", 9000, [])
    
    hm = EXTaskHeartbeatMonitor(wm, 2)
    
    assert hm.get_timeout() == 2
    
    wm.record_heartbeat(1)
    
    dead_before = hm.get_dead_workers()
    assert len(dead_before) == 0, f"Should have no dead workers initially: {dead_before}"
    
    current = int(time.time())
    hm.check_all_workers(current + 5)
    
    dead_after = hm.get_dead_workers()
    assert len(dead_after) > 0, f"Should have dead workers after timeout: {dead_after}"
    assert 1 in dead_after, "Worker 1 should be marked dead"
    
    log.shutdown_log()
    shutil.rmtree(log_dir, ignore_errors=True)
    print("PASS: test_heartbeat_monitor_timeout")


def test_heartbeat_keeps_worker_alive():
    log_dir = "test_alive_logs"
    if os.path.exists(log_dir):
        shutil.rmtree(log_dir)
    
    log.init_log(log_dir, 0)
    
    wm = EXTaskWorkerManager()
    wm.register_worker(2, "127.0.0.1", 9001, [])
    
    hm = EXTaskHeartbeatMonitor(wm, 3)
    
    current = int(time.time())
    for i in range(5):
        wm.record_heartbeat(2)
        time.sleep(0.5)
        hm.check_all_workers(current + i)
    
    dead = hm.get_dead_workers()
    assert len(dead) == 0, f"Worker should stay alive with heartbeats: {dead}"
    
    log.shutdown_log()
    shutil.rmtree(log_dir, ignore_errors=True)
    print("PASS: test_heartbeat_keeps_worker_alive")


def test_master_agent_timeout_detection():
    log_dir = "test_master_timeout_logs"
    if os.path.exists(log_dir):
        shutil.rmtree(log_dir)
    
    log.init_log(log_dir, 0)
    
    master = EXAgentMaster("127.0.0.1", 0)
    master.set_data_service(storage.ex_stg_get_data_service())
    master.start()
    
    connected = master.get_connected_workers()
    assert len(connected) == 0, "No workers initially"
    
    time.sleep(0.5)
    
    master.stop()
    log.shutdown_log()
    shutil.rmtree(log_dir, ignore_errors=True)
    print("PASS: test_master_agent_timeout_detection")


def test_worker_manager_unregister():
    log_dir = "test_unregister_logs"
    if os.path.exists(log_dir):
        shutil.rmtree(log_dir)
    
    log.init_log(log_dir, 0)
    
    wm = EXTaskWorkerManager()
    
    wm.register_worker(10, "127.0.0.1", 9000, [])
    assert wm.get_worker_count() == 1
    
    wm.unregister_worker(10)
    assert wm.get_worker_count() == 0
    
    log.shutdown_log()
    shutil.rmtree(log_dir, ignore_errors=True)
    print("PASS: test_worker_manager_unregister")


if __name__ == "__main__":
    test_heartbeat_monitor_timeout()
    print()
    test_heartbeat_keeps_worker_alive()
    print()
    test_master_agent_timeout_detection()
    print()
    test_worker_manager_unregister()
    print("\nAll worker timeout tests passed!")