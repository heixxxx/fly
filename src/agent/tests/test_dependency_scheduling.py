"""
端到端依赖调度测试

验证任务依赖等待和自动调度：
1. 任务A执行完成，outputs被标记为就绪
2. 任务B依赖A的outputs，等待A完成
3. A完成后，B自动被调度执行
4. 最终所有任务完成
"""

import sys
import os
import time
import json
import shutil

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '../../../bazel-bin/src/agent/export'))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '../../../bazel-bin/src/log/export'))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '../../../bazel-bin/src/storage/export'))

import _fly_agent as agent
import _fly_log as log
import _fly_storage as storage


def test_dependency_scheduling():
    """
    测试依赖调度流程：
    
    任务结构：
    - Task 1: write "output/a" (无依赖)
    - Task 2: depends on "output/a", write "output/b"
    - Task 3: depends on "output/b", write "output/c"
    
    验证：
    1. Task 1 立即执行（无依赖）
    2. Task 2 等待 Task 1 完成
    3. Task 1 完成后，Task 2 自动调度
    4. Task 2 完成后，Task 3 自动调度
    5. 最终所有任务完成
    """
    test_log_path = "test_dep_logs"
    test_db_path = "test_dep_db"
    
    if os.path.exists(test_log_path):
        shutil.rmtree(test_log_path)
    if os.path.exists(test_db_path):
        shutil.rmtree(test_db_path)
    
    os.makedirs(test_db_path, exist_ok=True)
    
    log.init_log(test_log_path, 0)
    
    master = agent.EXAgentMaster("127.0.0.1", 19500)
    master.start()
    time.sleep(0.1)
    
    # worker_id=1 for the worker
    log.shutdown_log()
    log.init_log(test_log_path, 1)
    
    def chain_executor(task_id, task_name, task_module, args):
        ret = agent.EXTaskExecResult()
        ret.task_id = task_id
        ret.status = agent.EXTaskExecStatus.SUCCESS
        ret.output = ""
        ret.error = ""
        
        output_path = args[0] if args else ""
        sm = storage.ex_stg_get_storage_manager()
        db = sm.get_or_create_database(test_db_path)
        
        if output_path:
            db.write_object_raw(output_path, f"data_from_task_{task_id}")
            ret.outputs = [output_path]
            print(f"[Task {task_id}] Wrote {output_path}")
        
        return ret
    
    executor = agent.EXTaskExecutor()
    executor.set_exec_func(chain_executor)
    
    worker = agent.EXAgentWorker(1, "127.0.0.1", 19500)
    worker.set_executor(executor)
    worker.start()
    
    time.sleep(0.3)
    assert worker.is_registered() == True
    
    # Reset logger back to master mode for master operations
    log.shutdown_log()
    log.init_log(test_log_path, 0)
    
    print("\n=== Dependency Scheduling Test ===")
    
    master.submit_task_with_deps(
        3, "task_c", "chain", ["output/c"],
        ["output/b"], ["output/c"]
    )
    print("[Master] Submitted Task 3: depends on output/b")
    
    pending = master.get_pending_tasks()
    print(f"[Master] pending tasks: {pending}")
    assert 3 in pending, "Task 3 should be pending (waiting for dependency)"
    
    master.submit_task_with_deps(
        2, "task_b", "chain", ["output/b"],
        ["output/a"], ["output/b"]
    )
    print("[Master] Submitted Task 2: depends on output/a")
    
    pending = master.get_pending_tasks()
    print(f"[Master] pending tasks: {pending}")
    assert 2 in pending, "Task 2 should be pending (waiting for dependency)"
    
    master.submit_task_with_deps(
        1, "task_a", "chain", ["output/a"],
        [], ["output/a"]
    )
    print("[Master] Submitted Task 1: no dependencies")
    
    time.sleep(0.3)
    
    print(f"[Status] pending: {master.get_pending_tasks()}, running: {master.get_running_tasks()}, completed: {master.get_completed_tasks()}")
    
    time.sleep(3.0)
    
    completed = master.get_completed_tasks()
    print(f"[Master] Final completed tasks: {completed}")
    
    completed = master.get_completed_tasks()
    print(f"[Master] Final completed tasks: {completed}")
    
    sm = storage.ex_stg_get_storage_manager()
    db = sm.get_or_create_database(test_db_path)
    
    data_a = db.read_object_raw("output/a")
    print(f"[Verify] output/a: {data_a}")
    assert data_a == "data_from_task_1"
    
    data_b = db.read_object_raw("output/b")
    print(f"[Verify] output/b: {data_b}")
    assert data_b == "data_from_task_2"
    
    data_c = db.read_object_raw("output/c")
    print(f"[Verify] output/c: {data_c}")
    assert data_c == "data_from_task_3"
    
    assert len(completed) >= 3, f"Expected 3 tasks completed, got {len(completed)}"
    assert 1 in completed and 2 in completed and 3 in completed
    
    print("\n✅ Dependency scheduling test passed!")
    
    master.stop()
    worker.stop()
    sm.close_all()
    log.shutdown_log()
    
    shutil.rmtree(test_log_path)
    shutil.rmtree(test_db_path)


def test_simple_dependency():
    """
    Simple dependency test:
    - Task 1: no dependencies
    - Task 2: depends on Task 1's output
    """
    test_log_path = "test_simple_dep_logs"
    
    if os.path.exists(test_log_path):
        shutil.rmtree(test_log_path)
    
    log.init_log(test_log_path, 0)
    
    master = agent.EXAgentMaster("127.0.0.1", 19501)
    master.start()
    time.sleep(0.1)
    
    def simple_executor(task_id, task_name, task_module, args):
        ret = agent.EXTaskExecResult()
        ret.task_id = task_id
        ret.status = agent.EXTaskExecStatus.SUCCESS
        ret.output = f"result_{task_id}"
        ret.error = ""
        
        if task_id == 1:
            ret.outputs = ["data/result_1"]
        elif task_id == 2:
            ret.outputs = ["data/result_2"]
        else:
            ret.outputs = []
        
        print(f"[Task {task_id}] Completed, outputs: {ret.outputs}")
        return ret
    
    executor = agent.EXTaskExecutor()
    executor.set_exec_func(simple_executor)
    
    worker = agent.EXAgentWorker(1, "127.0.0.1", 19501)
    worker.set_executor(executor)
    worker.start()
    
    time.sleep(0.3)
    
    master.submit_task_with_deps(2, "task_2", "test", [], ["data/result_1"], ["data/result_2"])
    master.submit_task_with_deps(1, "task_1", "test", [], [], ["data/result_1"])
    
    time.sleep(1.5)
    
    completed = master.get_completed_tasks()
    print(f"completed: {completed}")
    
    assert len(completed) >= 2
    assert 1 in completed and 2 in completed
    
    master.stop()
    worker.stop()
    log.shutdown_log()
    shutil.rmtree(test_log_path)
    
    print("PASS: test_simple_dependency")


if __name__ == "__main__":
    print("=" * 60)
    print("End-to-end Dependency Scheduling Test")
    print("=" * 60)
    
    test_simple_dependency()
    
    print("\nStarting full dependency chain test...")
    test_dependency_scheduling()
    
    print("\n" + "=" * 60)
    print("All dependency scheduling tests passed!")
    print("=" * 60)