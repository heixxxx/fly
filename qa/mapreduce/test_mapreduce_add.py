"""E2E test: MapReduce addition — summary merge with 3 partitions."""
from _fly_log import INFO
import time
import os
import shutil



from fly import open_db, get_config, MapReduceJob, wait_tasks
from test import record
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
assert master.wait_workers_registered(timeout=60.0), \
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

# 原断言：assert len(master.failed_tasks) == 0, \
record("mr_add/no_failed_tasks", len(master.failed_tasks) == 0,
       detail=master.failed_tasks)

result = mr.get()
# 原断言：assert result == 55, f"Expected sum=55, got {result}"
record("mr_add/sum", result, detail=result)

result_via_db = db.read_object("sum_result")
# 原断言：assert result_via_db == 55, f"Expected sum=55 via db.read_object, got {result_via_db}"
record("mr_add/sum_via_db", result_via_db, detail=result_via_db)

master.stop()
INFO("[DONE] test_mapreduce_add")
