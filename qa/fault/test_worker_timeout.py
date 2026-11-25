"""E2E worker timeout and heartbeat test."""
import os
import time
import shutil

import _fly_log as log
from _fly_agent import EXAgentMaster
from _fly_task import EXTaskWorkerManager, EXTaskHeartbeatMonitor
from test import record


def test_heartbeat_monitor_timeout():
    log_dir = "test_timeout_logs"
    if os.path.exists(log_dir):
        shutil.rmtree(log_dir)
    
    log.init_log(log_dir, 0)
    
    wm = EXTaskWorkerManager()
    wm.register_worker(1, "127.0.0.1", 9000, [])
    
    hm = EXTaskHeartbeatMonitor(wm, 2)
    
    timeout_value = hm.get_timeout()
    # 原断言：assert hm.get_timeout() == 2
    record("heartbeat_monitor/timeout", timeout_value, detail=timeout_value)
    
    wm.record_heartbeat(1)
    
    dead_before = hm.get_dead_workers()
    # 原断言：assert len(dead_before) == 0, f"Should have no dead workers initially: {dead_before}"
    record("heartbeat_monitor/dead_initially", dead_before, detail=dead_before)
    
    current = int(time.time())
    hm.check_all_workers(current + 5)
    
    dead_after = hm.get_dead_workers()
    has_dead = len(dead_after) > 0
    # 原断言：assert len(dead_after) > 0, f"Should have dead workers after timeout: {dead_after}"
    record("heartbeat_monitor/dead_after_timeout", has_dead, detail=dead_after)
    worker1_dead = 1 in dead_after
    # 原断言：assert 1 in dead_after, "Worker 1 should be marked dead"
    record("heartbeat_monitor/worker1_marked_dead", worker1_dead, detail=dead_after)
    
    log.shutdown_log()
    shutil.rmtree(log_dir, ignore_errors=True)
    print("DONE: test_heartbeat_monitor_timeout")


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
    # 原断言：assert len(dead) == 0, f"Worker should stay alive with heartbeats: {dead}"
    record("heartbeat_alive/dead_workers", dead, detail=dead)
    
    log.shutdown_log()
    shutil.rmtree(log_dir, ignore_errors=True)
    print("DONE: test_heartbeat_keeps_worker_alive")


def test_master_agent_timeout_detection():
    log_dir = "test_master_timeout_logs"
    if os.path.exists(log_dir):
        shutil.rmtree(log_dir)
    
    log.init_log(log_dir, 0)
    
    master = EXAgentMaster("127.0.0.1", 0)

    connected = master.get_connected_workers()
    # 原断言：assert len(connected) == 0, "No workers initially"
    record("master_agent/connected_initially", connected, detail=connected)
    
    time.sleep(0.5)
    
    log.shutdown_log()
    shutil.rmtree(log_dir, ignore_errors=True)
    print("DONE: test_master_agent_timeout_detection")


def test_worker_manager_unregister():
    log_dir = "test_unregister_logs"
    if os.path.exists(log_dir):
        shutil.rmtree(log_dir)
    
    log.init_log(log_dir, 0)
    
    wm = EXTaskWorkerManager()
    
    wm.register_worker(10, "127.0.0.1", 9000, [])
    count_after_register = wm.get_worker_count()
    # 原断言：assert wm.get_worker_count() == 1
    record("unregister/count_after_register", count_after_register, detail=count_after_register)
    
    wm.unregister_worker(10)
    count_after_unregister = wm.get_worker_count()
    # 原断言：assert wm.get_worker_count() == 0
    record("unregister/count_after_unregister", count_after_unregister, detail=count_after_unregister)
    
    log.shutdown_log()
    shutil.rmtree(log_dir, ignore_errors=True)
    print("DONE: test_worker_manager_unregister")


test_heartbeat_monitor_timeout()
print()
test_heartbeat_keeps_worker_alive()
print()
test_master_agent_timeout_detection()
print()
test_worker_manager_unregister()
print("\nAll worker timeout stages done!")
