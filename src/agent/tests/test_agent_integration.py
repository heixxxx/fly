import sys
import os

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '../../../bazel-bin/src/agent/export'))

import _fly_agent as agent

def test_task_executor_creation():
    """Test TaskExecutor basic creation"""
    executor = agent.EXTaskExecutor()
    assert executor.is_running() == False
    print("PASS: test_task_executor_creation")

def test_task_executor_result():
    """Test TaskExecResult structure"""
    result = agent.EXTaskExecResult()
    result.task_id = 123
    result.status = agent.EXTaskExecStatus.SUCCESS
    result.output = "test output"
    result.error = ""
    
    assert result.task_id == 123
    assert result.status == agent.EXTaskExecStatus.SUCCESS
    assert result.output == "test output"
    print("PASS: test_task_executor_result")

def test_task_executor_cancel():
    """Test TaskExecutor cancel operation"""
    executor = agent.EXTaskExecutor()
    executor.cancel()
    assert executor.is_running() == False
    print("PASS: test_task_executor_cancel")

def test_master_agent_creation():
    """Test MasterAgent basic creation"""
    master = agent.EXAgentMaster("127.0.0.1", 8080)
    assert master.is_running() == False
    print("PASS: test_master_agent_creation")

def test_master_agent_lifecycle():
    """Test MasterAgent start/stop lifecycle"""
    master = agent.EXAgentMaster("127.0.0.1", 8080)
    
    master.start()
    assert master.is_running() == True
    
    master.stop()
    assert master.is_running() == False
    print("PASS: test_master_agent_lifecycle")

def test_worker_agent_creation():
    """Test WorkerAgent basic creation"""
    worker = agent.EXAgentWorker(1, "127.0.0.1", 8080)
    assert worker.is_running() == False
    assert worker.get_worker_id() == 1
    print("PASS: test_worker_agent_creation")

def test_worker_agent_lifecycle():
    """Test WorkerAgent start/stop lifecycle"""
    worker = agent.EXAgentWorker(1, "127.0.0.1", 8080)
    
    worker.start()
    assert worker.is_running() == True
    
    worker.stop()
    assert worker.is_running() == False
    print("PASS: test_worker_agent_lifecycle")

def test_worker_agent_id():
    """Test WorkerAgent worker_id tracking"""
    worker1 = agent.EXAgentWorker(100, "127.0.0.1", 8080)
    worker2 = agent.EXAgentWorker(200, "127.0.0.1", 8080)
    
    assert worker1.get_worker_id() == 100
    assert worker2.get_worker_id() == 200
    print("PASS: test_worker_agent_id")

def test_enum_values():
    """Test TaskExecStatus enum values"""
    assert agent.EXTaskExecStatus.SUCCESS.value == 0
    assert agent.EXTaskExecStatus.FAILED.value == 1
    assert agent.EXTaskExecStatus.TIMEOUT.value == 2
    print("PASS: test_enum_values")

def test_master_worker_integration():
    """Test MasterAgent and WorkerAgent integration"""
    master = agent.EXAgentMaster("127.0.0.1", 9090)
    worker = agent.EXAgentWorker(1, "127.0.0.1", 9090)
    
    master.start()
    worker.start()

    assert master.is_running() == True
    assert worker.is_running() == True

    master.stop()
    worker.stop()
    
    assert master.is_running() == False
    assert worker.is_running() == False
    print("PASS: test_master_worker_integration")

if __name__ == "__main__":
    test_task_executor_creation()
    test_task_executor_result()
    test_task_executor_cancel()
    test_master_agent_creation()
    test_master_agent_lifecycle()
    test_worker_agent_creation()
    test_worker_agent_lifecycle()
    test_worker_agent_id()
    test_enum_values()
    test_master_worker_integration()
    print("\nAll agent integration tests passed!")