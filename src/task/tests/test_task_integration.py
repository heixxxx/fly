import sys
import os

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '../../../bazel-bin/src/task/export'))

import _fly_task as task

def test_dependency_graph():
    graph = task.EXTaskDependencyGraph()
    graph.add_task(1, [])
    
    ready = graph.get_ready_tasks()
    assert len(ready) == 1
    assert ready[0] == 1
    print("PASS: test_dependency_graph")

def test_dependency_graph_with_deps():
    graph = task.EXTaskDependencyGraph()
    graph.add_task(1, ["input/a"])
    
    ready = graph.get_ready_tasks()
    assert len(ready) == 0
    
    graph.mark_data_ready("input/a")
    ready = graph.get_ready_tasks()
    assert len(ready) == 1
    print("PASS: test_dependency_graph_with_deps")

def test_worker_manager():
    manager = task.EXTaskWorkerManager()
    manager.register_worker(1, "127.0.0.1", 8080, ["python", "gpu"])
    
    assert manager.get_worker_count() == 1
    assert manager.get_idle_worker_count() == 1
    
    idle = manager.get_idle_workers()
    assert len(idle) == 1
    assert idle[0] == 1
    
    gpu_workers = manager.get_workers_with_capability("gpu")
    assert len(gpu_workers) == 1
    print("PASS: test_worker_manager")

def test_worker_lifecycle():
    manager = task.EXTaskWorkerManager()
    manager.register_worker(1, "127.0.0.1", 8080, [])
    
    manager.assign_task(1, 100)
    assert manager.get_idle_worker_count() == 0
    
    manager.complete_task(1)
    assert manager.get_idle_worker_count() == 1
    print("PASS: test_worker_lifecycle")

def test_task_scheduler():
    graph = task.EXTaskDependencyGraph()
    manager = task.EXTaskWorkerManager()
    
    graph.add_task(1, [])
    manager.register_worker(1, "127.0.0.1", 8080, [])
    
    scheduler = task.EXTaskTaskScheduler(graph, manager)
    result = scheduler.schedule_next()
    
    assert result.scheduled == True
    assert result.task_id == 1
    assert result.worker_id == 1
    print("PASS: test_task_scheduler")

def test_task_manager():
    meta = task.EXTaskManager()
    meta.create_task(1, "test_task", ["input/a"], ["output/b"], "{}")
    
    assert meta.has_task(1) == True
    meta.update_task_status(1, task.EXTaskTaskStatus.RUNNING)
    
    tasks = meta.get_tasks_by_status(task.EXTaskTaskStatus.RUNNING)
    assert len(tasks) == 1
    assert tasks[0].name == "test_task"
    print("PASS: test_metadata_manager")

def test_heartbeat_monitor():
    manager = task.EXTaskWorkerManager()
    manager.register_worker(1, "127.0.0.1", 8080, [])
    
    monitor = task.EXTaskHeartbeatMonitor(manager, 30)
    monitor.check_all_workers(100)
    
    dead = monitor.get_dead_workers()
    assert len(dead) == 1
    assert dead[0] == 1
    print("PASS: test_heartbeat_monitor")

def test_heartbeat_monitor_timeout():
    manager = task.EXTaskWorkerManager()
    manager.register_worker(1, "127.0.0.1", 8080, [])
    
    monitor = task.EXTaskHeartbeatMonitor(manager, 30)
    assert monitor.get_timeout() == 30
    
    monitor.set_timeout(60)
    assert monitor.get_timeout() == 60
    print("PASS: test_heartbeat_monitor_timeout")

def test_enum_values():
    assert task.EXTaskWorkerStatus.IDLE.value == 0
    assert task.EXTaskWorkerStatus.BUSY.value == 1
    assert task.EXTaskWorkerStatus.DEAD.value == 2
    
    assert task.EXTaskTaskStatus.PENDING.value == 0
    assert task.EXTaskTaskStatus.RUNNING.value == 1
    assert task.EXTaskTaskStatus.COMPLETED.value == 2
    print("PASS: test_enum_values")

if __name__ == "__main__":
    test_dependency_graph()
    test_dependency_graph_with_deps()
    test_worker_manager()
    test_worker_lifecycle()
    test_task_scheduler()
    test_metadata_manager()
    test_heartbeat_monitor()
    test_heartbeat_monitor_timeout()
    test_enum_values()
    print("\nAll integration tests passed!")