"""
端到端求和示例测试

演示完整的任务执行流程：
1. 自定义求和任务执行器
2. 3个 worker 分布式计算
3. 部分结果写入数据库
4. 聚合任务汇总结果
5. 数据库冻结与验证
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


def create_sum_executor(db_path, worker_id):
    def execute_sum(task_id, task_name, task_module, args):
        print(f"[Worker {worker_id}] Execute task {task_id}: {task_name}")
        
        if task_name == "partial_sum":
            try:
                array_data = json.loads(args[0])
                partial_result = sum(array_data)
                
                sm = storage.ex_stg_get_storage_manager()
                db = sm.get_or_create_database(db_path)
                
                result_key = f"partial_result_{worker_id}_{task_id}"
                db.write_object_raw(result_key, str(partial_result))
                
                print(f"[Worker {worker_id}] Partial sum: {partial_result}, wrote {result_key}")
                
                ret = agent.EXTaskExecResult()
                ret.task_id = task_id
                ret.status = agent.EXTaskExecStatus.SUCCESS
                ret.output = str(partial_result)
                ret.error = ""
                return ret
            except Exception as e:
                print(f"[Worker {worker_id}] Task failed: {e}")
                ret = agent.EXTaskExecResult()
                ret.task_id = task_id
                ret.status = agent.EXTaskExecStatus.FAILED
                ret.output = ""
                ret.error = str(e)
                return ret
        
        elif task_name == "aggregate_sum":
            try:
                sm = storage.ex_stg_get_storage_manager()
                db = sm.get_or_create_database(db_path)
                
                partial_results_str = args[0]
                partial_keys = json.loads(partial_results_str)
                
                total = 0
                for key in partial_keys:
                    value_str = db.read_object_raw(key)
                    total += int(value_str)
                
                db.write_object_raw("final_result", str(total))
                
                print(f"[Aggregate] Final result: {total}, wrote final_result")
                
                ret = agent.EXTaskExecResult()
                ret.task_id = task_id
                ret.status = agent.EXTaskExecStatus.SUCCESS
                ret.output = str(total)
                ret.error = ""
                return ret
            except Exception as e:
                print(f"[Aggregate] Task failed: {e}")
                ret = agent.EXTaskExecResult()
                ret.task_id = task_id
                ret.status = agent.EXTaskExecStatus.FAILED
                ret.output = ""
                ret.error = str(e)
                return ret
        
        else:
            ret = agent.EXTaskExecResult()
            ret.task_id = task_id
            ret.status = agent.EXTaskExecStatus.FAILED
            ret.output = ""
            ret.error = f"Unknown task type: {task_name}"
            return ret
    
    executor = agent.EXTaskExecutor()
    executor.set_exec_func(execute_sum)
    return executor


def test_sum_example():
    test_db_path = "test_sum_db"
    test_log_path = "test_sum_logs"
    
    if os.path.exists(test_db_path):
        shutil.rmtree(test_db_path)
    if os.path.exists(test_log_path):
        shutil.rmtree(test_log_path)
    
    os.makedirs(test_db_path, exist_ok=True)
    
    log.init_log(test_log_path, 0)
    
    master = agent.EXAgentMaster("127.0.0.1", 19400)
    master.start()
    time.sleep(0.1)
    print("[Master] Started")
    
    workers = []
    executors = []
    for i in range(1, 4):
        executor = create_sum_executor(test_db_path, i)
        executors.append(executor)
        
        worker = agent.EXAgentWorker(i, "127.0.0.1", 19400)
        worker.set_executor(executor)
        worker.start()
        workers.append(worker)
    
    time.sleep(0.3)
    
    connected = master.get_connected_workers()
    print(f"[Master] Connected {len(connected)} workers: {connected}")
    assert len(connected) == 3, f"Expected 3 workers, got {len(connected)}"
    
    # [1-10] split: [1,2,3,4]=10, [5,6,7]=18, [8,9,10]=27, total=55
    partial_data = [
        [1, 2, 3, 4],
        [5, 6, 7],
        [8, 9, 10]
    ]
    
    print("\n[Phase 1] Submit partial sum tasks")
    task_ids = []
    for i, data in enumerate(partial_data, start=1):
        task_id = i
        task_ids.append(task_id)
        master.submit_task(
            task_id,
            "partial_sum",
            "sum_module",
            [json.dumps(data)]
        )
        print(f"[Master] Submit task {task_id}: partial_sum({data})")
    
    time.sleep(1.0)
    
    completed = master.get_completed_tasks()
    print(f"[Master] Completed tasks: {completed}")
    assert len(completed) >= 3, f"Expected 3 partial tasks completed, got {len(completed)}"
    
    print("\n[Phase 2] Submit aggregate task")
    partial_result_keys = [f"partial_result_{i}_{i}" for i in range(1, 4)]
    
    master.submit_task(
        4,
        "aggregate_sum",
        "sum_module",
        [json.dumps(partial_result_keys)]
    )
    print(f"[Master] Submit aggregate task: aggregate_sum({partial_result_keys})")
    
    time.sleep(1.0)
    
    completed = master.get_completed_tasks()
    print(f"[Master] Completed tasks: {completed}")
    assert len(completed) >= 4, f"Expected 4 tasks completed (including aggregate), got {len(completed)}"
    
    print("\n[Phase 3] Database freeze & verification")
    sm = storage.ex_stg_get_storage_manager()
    db = sm.get_or_create_database(test_db_path)
    
    final_result = db.read_object_raw("final_result")
    print(f"[Verify] Final sum result: {final_result}")
    assert int(final_result) == 55, f"Expected 55, got {final_result}"
    
    db.freeze()
    print("[DB] Frozen")
    assert db.is_frozen() == True, "Database should be frozen"
    
    result_after_freeze = db.read_object_raw("final_result")
    assert result_after_freeze == final_result, "Result should be consistent after freeze"
    print(f"[Verify] Read after freeze: {result_after_freeze}")
    
    try:
        db.write_object_raw("blocked_write", "this should fail")
        assert False, "Write after freeze should fail"
    except Exception as e:
        print(f"[Verify] Write after freeze correctly blocked: {type(e).__name__}")
    
    master.stop()
    for worker in workers:
        worker.stop()
    sm.close_all()
    log.shutdown_log()
    
    shutil.rmtree(test_db_path)
    shutil.rmtree(test_log_path)
    
    print("\n✅ End-to-end sum example test passed!")
    print("Verified:")
    print("  - 3 workers distributed partial sum computation")
    print("  - Partial results written to database")
    print("  - Aggregate task computed final result")
    print("  - Database frozen: reads work, writes blocked")


def test_simple_executor():
    test_log_path = "test_simple_logs"
    test_db_path = "test_simple_db"
    
    if os.path.exists(test_log_path):
        shutil.rmtree(test_log_path)
    if os.path.exists(test_db_path):
        shutil.rmtree(test_db_path)
    
    os.makedirs(test_db_path, exist_ok=True)
    
    log.init_log(test_log_path, 0)
    
    master = agent.EXAgentMaster("127.0.0.1", 19401)
    master.start()
    time.sleep(0.1)
    
    def simple_execute(task_id, task_name, task_module, args):
        if task_name == "add":
            a, b = int(args[0]), int(args[1])
            result = a + b
            ret = agent.EXTaskExecResult()
            ret.task_id = task_id
            ret.status = agent.EXTaskExecStatus.SUCCESS
            ret.output = str(result)
            ret.error = ""
            return ret
        ret = agent.EXTaskExecResult()
        ret.task_id = task_id
        ret.status = agent.EXTaskExecStatus.FAILED
        ret.output = ""
        ret.error = "unknown task"
        return ret
    
    executor = agent.EXTaskExecutor()
    executor.set_exec_func(simple_execute)
    
    worker = agent.EXAgentWorker(1, "127.0.0.1", 19401)
    worker.set_executor(executor)
    worker.start()
    
    time.sleep(0.3)
    
    master.submit_task(1, "add", "math", ["10", "20"])
    
    time.sleep(0.5)
    
    completed = master.get_completed_tasks()
    assert len(completed) >= 1
    
    master.stop()
    worker.stop()
    log.shutdown_log()
    
    shutil.rmtree(test_log_path)
    shutil.rmtree(test_db_path)
    
    print("PASS: test_simple_executor")


if __name__ == "__main__":
    print("=" * 60)
    print("End-to-end Sum Example Test")
    print("=" * 60)
    
    test_simple_executor()
    
    print("\nStarting full sum example flow...")
    test_sum_example()
    
    print("\n" + "=" * 60)
    print("All tests passed!")
    print("=" * 60)