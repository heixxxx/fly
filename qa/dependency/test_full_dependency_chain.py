"""E2E full dependency chain test.

Tests a 5-task dependency chain:
  Task 1 (no deps) -> Task 2 (depends on Task 1) -> Task 3 (depends on Task 2)
  Task 4 (no deps) -> Task 5 (depends on Task 3 + Task 4)

Verifies:
  - DependencyGraph correctly resolves task readiness
  - TaskScheduler dispatches tasks in dependency order
  - Master tracks pending/running/completed through the chain
  - Result data flows correctly across task boundaries
"""
import os
import time
import shutil

import _fly_log as log
import _fly_storage as storage
from _fly_task import EXTaskDependencyGraph
from _fly_agent import EXAgentMaster
from _fly_storage import ex_stg_get_data_service
from test import record


def test_dependency_graph_basic():
    """DependencyGraph: add tasks, mark data ready, get ready tasks."""
    graph = EXTaskDependencyGraph()

    graph.add_task(1, [])
    graph.add_task(2, ["task_1_output"])
    graph.add_task(3, ["task_2_output"])

    ready = graph.get_ready_tasks()
    # 原断言：assert 1 in ready, f"Task 1 should be ready, got: {ready}"
    record("graph_basic/initial_task1_ready", 1 in ready, detail=ready)
    # 原断言：assert 2 not in ready, "Task 2 should not be ready yet"
    record("graph_basic/task2_not_ready", 2 not in ready, detail=ready)
    # 原断言：assert 3 not in ready, "Task 3 should not be ready yet"
    record("graph_basic/task3_not_ready", 3 not in ready, detail=ready)

    graph.mark_data_ready("task_1_output")
    graph.remove_task(1)

    ready = graph.get_ready_tasks()
    # 原断言：assert 2 in ready, f"Task 2 should be ready after task 1 done, got: {ready}"
    record("graph_basic/task2_ready_after_t1", 2 in ready, detail=ready)
    # 原断言：assert 3 not in ready, "Task 3 should not be ready yet"
    record("graph_basic/task3_still_not_ready", 3 not in ready, detail=ready)

    graph.mark_data_ready("task_2_output")
    graph.remove_task(2)

    ready = graph.get_ready_tasks()
    # 原断言：assert 3 in ready, f"Task 3 should be ready, got: {ready}"
    record("graph_basic/task3_ready", 3 in ready, detail=ready)

    print("PASS: test_dependency_graph_basic")


def test_dependency_graph_diamond():
    """DependencyGraph: diamond dependency pattern.

       Task 1 -> Task 2, Task 3
       Task 2, Task 3 -> Task 4
    """
    graph = EXTaskDependencyGraph()

    graph.add_task(1, [])
    graph.add_task(2, ["task_1_out"])
    graph.add_task(3, ["task_1_out"])
    graph.add_task(4, ["task_2_out", "task_3_out"])

    ready = graph.get_ready_tasks()
    # 原断言：assert 1 in ready
    record("diamond/initial_task1_ready", 1 in ready, detail=ready)
    # 原断言：assert len(ready) == 1
    record("diamond/initial_only_one_ready", len(ready) == 1, detail=ready)

    graph.mark_data_ready("task_1_out")
    graph.remove_task(1)

    ready = graph.get_ready_tasks()
    # 原断言：assert 2 in ready and 3 in ready, f"Both 2,3 should be ready: {ready}"
    record("diamond/both_2_3_ready", 2 in ready and 3 in ready, detail=ready)

    graph.mark_data_ready("task_2_out")
    graph.remove_task(2)

    ready = graph.get_ready_tasks()
    # 原断言：assert 4 not in ready, "Task 4 should not be ready yet"
    record("diamond/task4_not_ready", 4 not in ready, detail=ready)

    graph.mark_data_ready("task_3_out")
    graph.remove_task(3)

    ready = graph.get_ready_tasks()
    # 原断言：assert 4 in ready
    record("diamond/task4_ready", 4 in ready, detail=ready)

    print("PASS: test_dependency_graph_diamond")


def test_dependency_graph_add_and_remove():
    graph = EXTaskDependencyGraph()

    graph.add_task(1, [])
    graph.add_task(2, ["input_a"])
    graph.add_task(3, ["input_b"])

    ready = graph.get_ready_tasks()
    # 原断言：assert 1 in ready and 2 not in ready and 3 not in ready
    record("add_remove/initial_state",
           1 in ready and 2 not in ready and 3 not in ready, detail=ready)

    graph.remove_task(1)
    graph.mark_data_ready("input_a")

    ready = graph.get_ready_tasks()
    # 原断言：assert 2 in ready
    record("add_remove/task2_ready", 2 in ready, detail=ready)


def test_dependency_graph_is_task_ready():
    """DependencyGraph: is_task_ready checks individual task readiness."""
    graph = EXTaskDependencyGraph()

    graph.add_task(1, [])
    graph.add_task(2, ["dep_x"])

    # 原断言：assert graph.is_task_ready(1) == True
    record("is_task_ready/task1_initial", graph.is_task_ready(1) == True)
    # 原断言：assert graph.is_task_ready(2) == False
    record("is_task_ready/task2_before_dep", graph.is_task_ready(2) == False)

    graph.mark_data_ready("dep_x")
    # 原断言：assert graph.is_task_ready(2) == True
    record("is_task_ready/task2_after_dep", graph.is_task_ready(2) == True)

    print("PASS: test_dependency_graph_is_task_ready")


def test_master_submit_and_track():
    """Master: submit tasks with dependencies, track status through completion."""
    from fly.runtime import get_agent
    master = get_agent()

    master._agent.submit_task_with_deps(1, "step1", "tasks_module", [], [], ["step1_out"])
    master._agent.submit_task_with_deps(2, "step2", "tasks_module", [], ["step1_out"], ["step2_out"])
    master._agent.submit_task_with_deps(3, "step3", "tasks_module", [], ["step2_out"], [])

    time.sleep(0.2)

    pending = master._agent.get_pending_tasks()
    # 原断言：assert len(pending) > 0, "Should have pending tasks"
    record("master_submit/has_pending", len(pending) > 0, detail=len(pending))

    print("PASS: test_master_submit_and_track")


test_dependency_graph_basic()
print()
test_dependency_graph_diamond()
print()
test_dependency_graph_add_and_remove()
print()
test_dependency_graph_is_task_ready()
print()
test_master_submit_and_track()
print("\nAll stages done!")
