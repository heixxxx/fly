"""E2E test: MapReduce addition — summary merge with 3 partitions."""
from _fly_log import INFO
import time
import os
import shutil



from fly import open_db, get_config, MapReduceJob, wait_tasks
DB_PATH = os.path.join(get_config().get_str("log_dir"), "db")


def cleanup():
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)


def wait_for(condition, timeout=30.0, interval=0.5):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if condition():
            return True
        time.sleep(interval)
    return False


cleanup()
get_config().set_int("fail_unscheduleable_tasks", 0)

from fly.runtime import get_agent
master = get_agent()

master.launch_local_workers([{}, {}, {}])
assert wait_for(lambda: master.worker_count >= 3, timeout=10.0), \
    f"3 workers should connect, got {master.worker_count}"

db = open_db(DB_PATH)

mr = MapReduceJob(db, output_name="sum_result")
mr.set_partitioner(lambda data: [data[i::3] for i in range(3)])
mr.set_processor(lambda part: sum(part))
mr.set_merger(lambda a, b: a + b, "summary")
mr.run([1, 2, 3, 4, 5, 6, 7, 8, 9, 10])

try:
    wait_tasks(timeout=60.0)
except RuntimeError:
    for tid in master.failed_tasks:
        INFO(f"Task {tid} failed: {master.get_task_error(tid)}")
    raise

assert len(master.failed_tasks) == 0, \
    f"No tasks should fail, errors={[master.get_task_error(t) for t in master.failed_tasks]}"

result = mr.get()
assert result == 55, f"Expected sum=55, got {result}"

result_via_db = db.read_object("sum_result")
assert result_via_db == 55, f"Expected sum=55 via db.read_object, got {result_via_db}"

master.stop()
INFO("[PASS] test_mapreduce_add")
