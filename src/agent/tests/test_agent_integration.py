import sys
import os
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '../../../bazel-bin/src/agent/export'))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '../../../bazel-bin/src/log/export'))

import _fly_agent as agent
import _fly_log as log

def test_master_worker_register():
    log.init_master("test_logs/")
    log.init_worker(1, "test_logs/")
    
    master = agent.EXAgentMaster("127.0.0.1", 19090)
    master.start()
    time.sleep(0.1)
    
    worker = agent.EXAgentWorker(1, "127.0.0.1", 19090)
    worker.start()
    time.sleep(0.2)
    
    assert worker.is_registered() == True
    assert master.get_connection_count() == 1
    
    connected = master.get_connected_workers()
    assert len(connected) == 1
    assert connected[0] == 1
    
    master.stop()
    worker.stop()
    log.shutdown()
    print("PASS: test_master_worker_register")

def test_multiple_workers():
    log.init_master("test_logs/")
    log.init_worker(1, "test_logs/")
    log.init_worker(2, "test_logs/")
    
    master = agent.EXAgentMaster("127.0.0.1", 19091)
    master.start()
    time.sleep(0.1)
    
    worker1 = agent.EXAgentWorker(1, "127.0.0.1", 19091)
    worker2 = agent.EXAgentWorker(2, "127.0.0.1", 19091)
    worker1.start()
    worker2.start()
    time.sleep(0.3)
    
    assert master.get_connection_count() == 2
    
    master.stop()
    worker1.stop()
    worker2.stop()
    log.shutdown()
    print("PASS: test_multiple_workers")

def test_worker_disconnect():
    log.init_master("test_logs/")
    log.init_worker(1, "test_logs/")
    
    master = agent.EXAgentMaster("127.0.0.1", 19092)
    master.start()
    
    worker = agent.EXAgentWorker(1, "127.0.0.1", 19092)
    worker.start()
    time.sleep(0.2)
    
    assert master.get_connection_count() == 1
    
    worker.stop()
    time.sleep(0.2)
    
    assert master.get_connection_count() == 0
    
    master.stop()
    log.shutdown()
    print("PASS: test_worker_disconnect")

def test_master_restart():
    master = agent.EXAgentMaster("127.0.0.1", 19093)
    
    master.start()
    time.sleep(0.05)
    assert master.is_running() == True
    master.stop()
    time.sleep(0.05)
    assert master.is_running() == False
    
    master.start()
    time.sleep(0.05)
    assert master.is_running() == True
    master.stop()
    time.sleep(0.05)
    assert master.is_running() == False
    print("PASS: test_master_restart")

def test_executor_execute():
    executor = agent.EXTaskExecutor()
    result = executor.execute(1, "test_task", "test_module", ["arg1", "arg2"])
    
    assert result.task_id == 1
    assert result.status == agent.EXTaskExecStatus.SUCCESS
    print("PASS: test_executor_execute")

def test_submit_task():
    log.init_master("test_logs/")
    log.init_worker(1, "test_logs/")
    
    master = agent.EXAgentMaster("127.0.0.1", 19300)
    master.start()
    time.sleep(0.1)
    
    worker = agent.EXAgentWorker(1, "127.0.0.1", 19300)
    
    executor = agent.EXTaskExecutor()
    worker.set_executor(executor)
    worker.start()
    
    time.sleep(0.3)
    assert worker.is_registered() == True
    
    master.submit_task(1, "test_task", "test_module", [])
    time.sleep(0.8)
    
    completed = master.get_completed_tasks()
    assert len(completed) >= 1
    
    master.stop()
    worker.stop()
    log.shutdown()
    print("PASS: test_submit_task")

def test_enum_values():
    assert agent.EXTaskExecStatus.SUCCESS.value == 0
    assert agent.EXTaskExecStatus.FAILED.value == 1
    assert agent.EXTaskExecStatus.TIMEOUT.value == 2
    print("PASS: test_enum_values")

if __name__ == "__main__":
    import shutil
    if os.path.exists("test_logs"):
        shutil.rmtree("test_logs")
    
    test_master_worker_register()
    shutil.rmtree("test_logs")
    
    test_multiple_workers()
    shutil.rmtree("test_logs")
    
    test_worker_disconnect()
    shutil.rmtree("test_logs")
    
    test_master_restart()
    
    test_executor_execute()
    
    test_submit_task()
    shutil.rmtree("test_logs")
    
    test_enum_values()
    
    print("\nAll Python tests passed!")