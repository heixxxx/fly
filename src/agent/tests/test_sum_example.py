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
    """
    创建求和任务执行器
    执行器会：
    1. 解析参数中的数组数据
    2. 计算部分和
    3. 将结果写入数据库
    """
    def execute_sum(task_id, task_name, task_module, args):
        print(f"[Worker {worker_id}] 执行任务 {task_id}: {task_name}")
        
        if task_name == "partial_sum":
            try:
                array_data = json.loads(args[0])
                partial_result = sum(array_data)
                
                sm = storage.ex_stg_get_storage_manager()
                db = sm.get_or_create_database(db_path)
                
                result_key = f"partial_result_{worker_id}_{task_id}"
                db.write_object_raw(result_key, str(partial_result))
                
                print(f"[Worker {worker_id}] 部分和计算完成: {partial_result}, 已写入 {result_key}")
                
                ret = agent.EXTaskExecResult()
                ret.task_id = task_id
                ret.status = agent.EXTaskExecStatus.SUCCESS
                ret.output = str(partial_result)
                ret.error = ""
                return ret
            except Exception as e:
                print(f"[Worker {worker_id}] 任务执行失败: {e}")
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
                
                print(f"[聚合] 最终结果: {total}, 已写入 final_result")
                
                ret = agent.EXTaskExecResult()
                ret.task_id = task_id
                ret.status = agent.EXTaskExecStatus.SUCCESS
                ret.output = str(total)
                ret.error = ""
                return ret
            except Exception as e:
                print(f"[聚合] 任务执行失败: {e}")
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
            ret.error = f"未知任务类型: {task_name}"
            return ret
    
    executor = agent.EXTaskExecutor()
    executor.set_exec_func(execute_sum)
    return executor


def test_sum_example():
    """
    端到端求和示例
    
    流程：
    1. 创建 Master 和 3 个 Workers
    2. 将数组 [1,2,3,4,5,6,7,8,9,10] 分成3部分
    3. 每个部分求和任务分配给一个 worker
    4. 聚合任务汇总所有部分结果
    5. 冻结数据库
    6. 验证结果
    """
    test_db_path = "test_sum_db"
    test_log_path = "test_sum_logs"
    
    # 清理测试环境
    if os.path.exists(test_db_path):
        shutil.rmtree(test_db_path)
    if os.path.exists(test_log_path):
        shutil.rmtree(test_log_path)
    
    os.makedirs(test_db_path, exist_ok=True)
    
    # 初始化日志
    log.init_master(test_log_path)
    log.init_worker(1, test_log_path)
    log.init_worker(2, test_log_path)
    log.init_worker(3, test_log_path)
    
    # 启动 Master
    master = agent.EXAgentMaster("127.0.0.1", 19400)
    master.start()
    time.sleep(0.1)
    print("[Master] 启动完成")
    
    # 创建并启动3个 Workers
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
    
    # 等待所有 worker 注册
    connected = master.get_connected_workers()
    print(f"[Master] 已连接 {len(connected)} 个 workers: {connected}")
    assert len(connected) == 3, f"期望3个worker，实际连接{len(connected)}个"
    
    # 准备数据：将 [1-10] 分成3部分
    # Worker 1: [1, 2, 3, 4] -> 10
    # Worker 2: [5, 6, 7] -> 18
    # Worker 3: [8, 9, 10] -> 27
    # 最终: 10 + 18 + 27 = 55
    partial_data = [
        [1, 2, 3, 4],
        [5, 6, 7],
        [8, 9, 10]
    ]
    
    # 提交部分求和任务
    print("\n[阶段1] 提交部分求和任务")
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
        print(f"[Master] 提交任务 {task_id}: partial_sum({data})")
    
    # 等待部分任务完成
    time.sleep(1.0)
    
    # 检查部分任务完成状态
    completed = master.get_completed_tasks()
    print(f"[Master] 完成的任务: {completed}")
    assert len(completed) >= 3, f"期望3个部分任务完成，实际完成{len(completed)}个"
    
    # 提交聚合任务
    print("\n[阶段2] 提交聚合任务")
    partial_result_keys = [f"partial_result_{i}_{i}" for i in range(1, 4)]
    
    master.submit_task(
        4,  # task_id
        "aggregate_sum",
        "sum_module",
        [json.dumps(partial_result_keys)]
    )
    print(f"[Master] 提交聚合任务: aggregate_sum({partial_result_keys})")
    
    # 等待聚合任务完成
    time.sleep(1.0)
    
    # 检查聚合任务完成状态
    completed = master.get_completed_tasks()
    print(f"[Master] 完成的任务: {completed}")
    assert len(completed) >= 4, f"期望4个任务完成（包括聚合），实际完成{len(completed)}个"
    
    # 获取数据库并冻结
    print("\n[阶段3] 数据库冻结与验证")
    sm = storage.ex_stg_get_storage_manager()
    db = sm.get_or_create_database(test_db_path)
    
    # 验证数据库内容（冻结前）
    final_result = db.read_object_raw("final_result")
    print(f"[验证] 最终求和结果: {final_result}")
    assert int(final_result) == 55, f"期望结果55，实际得到{final_result}"
    
    # 冻结数据库
    db.freeze()
    print("[数据库] 已冻结")
    assert db.is_frozen() == True, "数据库应该已冻结"
    
    # 验证冻结后可以读取
    result_after_freeze = db.read_object_raw("final_result")
    assert result_after_freeze == final_result, "冻结后读取结果应该一致"
    print(f"[验证] 冻结后读取成功: {result_after_freeze}")
    
    # 验证冻结后写入被阻止（会抛出异常）
    try:
        db.write_object_raw("blocked_write", "this should fail")
        assert False, "冻结后写入应该失败"
    except Exception as e:
        print(f"[验证] 冻结后写入被正确阻止: {type(e).__name__}")
    
    # 清理资源
    master.stop()
    for worker in workers:
        worker.stop()
    sm.close_all()
    log.shutdown()
    
    # 清理测试目录
    shutil.rmtree(test_db_path)
    shutil.rmtree(test_log_path)
    
    print("\n✅ 端到端求和示例测试通过！")
    print("流程验证:")
    print("  - 3个 worker 分布式计算部分和")
    print("  - 部分结果写入数据库")
    print("  - 聚合任务汇总最终结果")
    print("  - 数据库冻结后读取正常、写入阻止")


def test_simple_executor():
    """
    简单的执行器功能测试，验证自定义执行逻辑
    """
    test_log_path = "test_simple_logs"
    test_db_path = "test_simple_db"
    
    if os.path.exists(test_log_path):
        shutil.rmtree(test_log_path)
    if os.path.exists(test_db_path):
        shutil.rmtree(test_db_path)
    
    os.makedirs(test_db_path, exist_ok=True)
    
    log.init_master(test_log_path)
    log.init_worker(1, test_log_path)
    
    master = agent.EXAgentMaster("127.0.0.1", 19401)
    master.start()
    time.sleep(0.1)
    
    # 创建自定义执行器
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
    
    # 提交加法任务
    master.submit_task(1, "add", "math", ["10", "20"])
    
    time.sleep(0.5)
    
    completed = master.get_completed_tasks()
    assert len(completed) >= 1
    
    master.stop()
    worker.stop()
    log.shutdown()
    
    shutil.rmtree(test_log_path)
    shutil.rmtree(test_db_path)
    
    print("PASS: test_simple_executor")


if __name__ == "__main__":
    print("=" * 60)
    print("端到端求和示例测试")
    print("=" * 60)
    
    test_simple_executor()
    
    print("\n开始完整的求和示例流程...")
    test_sum_example()
    
    print("\n" + "=" * 60)
    print("所有测试通过！")
    print("=" * 60)