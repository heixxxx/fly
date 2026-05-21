import sys
import os
import time
import shutil

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '../../../bazel-bin/src/agent/export'))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '../../../bazel-bin/src/log/export'))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '../../../bazel-bin/src/storage/export'))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '../../../bazel-bin/src/core/export'))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '..'))

import _fly_log as log
from fly import open_db, as_task, task_name
from fly.agent import Master
from fly.runtime import reset
from fly.task import _serialize_args


def test_database_get_obj_name():
    db = open_db("/tmp/fly_api_test_db1")
    obj_name = db.get_obj_name("output/result")
    assert ":" in obj_name
    assert obj_name.endswith(":output/result")
    assert obj_name.startswith(db.get_db_id())
    print("PASS: test_database_get_obj_name")


def test_serialize_args():
    db = open_db("/tmp/fly_api_test_db2")
    args = _serialize_args([db, "file1", 42])
    assert args[0].startswith("__fly_db__:"), f"Expected db marker, got {args[0]}"
    assert args[1] != "file1"
    assert args[2] != "42"
    print("PASS: test_serialize_args")


def test_master_submit():
    if os.path.exists("test_fly_api_logs"):
        shutil.rmtree("test_fly_api_logs")
    
    log.init_log("test_fly_api_logs", 0)

    try:
        master = Master()
        master.launch_local_workers([{"role": "hybrid"}], mode="thread")
        print(f"  Master started on auto-assigned port: {master.port}")

        import fly.runtime as rt
        rt._agent = master
        time.sleep(0.5)

        print(f"  connected_workers: {master._agent.get_connected_workers()}")
        print(f"  idle_workers: {master._agent.get_idle_workers()}")
        print(f"  connection_count: {master._agent.get_connection_count()}")

        @as_task()
        @task_name("simple_task")
        def simple_task():
            pass

        simple_task()

        for i in range(10):
            pending = master.pending_tasks
            running = master.running_tasks
            completed = master.completed_tasks
            print(f"  [{i}] pending={pending} running={running} completed={completed}")
            if completed:
                break
            time.sleep(0.5)

        completed = master.completed_tasks
        assert len(completed) >= 1, f"Expected 1+ completed, got {completed}"
        print("PASS: test_master_submit")
    finally:
        master.stop()
        log.shutdown_log()
        shutil.rmtree("test_fly_api_logs", ignore_errors=True)
        reset()


if __name__ == "__main__":
    test_database_get_obj_name()
    test_serialize_args()
    test_master_submit()
    print("\nAll fly API tests passed!")