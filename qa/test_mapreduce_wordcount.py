"""E2E test: MapReduce word count — summary merge with finalize."""
from _fly_log import INFO
import time
import sys
import os
import shutil

DB_PATH = "/tmp/fly_e2e_mr_wordcount_db"

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '..', 'src'))

from fly import open_db, get_config, MapReduceJob, wait_tasks


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

texts = [
    "hello world hello",
    "world fly world",
    "hello fly hello",
]


def count_words(text):
    from collections import Counter
    return dict(Counter(text.split()))


def merge_counts(a, b):
    merged = dict(a)
    for k, v in b.items():
        merged[k] = merged.get(k, 0) + v
    return merged


def sort_by_freq(counts):
    return dict(sorted(counts.items(), key=lambda x: -x[1]))


mr = MapReduceJob(db, output_name="word_counts")
mr.set_partitioner(lambda data: list(data))
mr.set_processor(count_words)
mr.set_merger(merge_counts, "summary")
mr.set_finalizer(sort_by_freq)
mr.run(texts)

assert wait_tasks(timeout=60.0), \
    f"Tasks should complete, " \
    f"completed={len(master.completed_tasks)}, failed={len(master.failed_tasks)}"

assert len(master.failed_tasks) == 0, \
    f"No tasks should fail, failed={master.failed_tasks}"

result = mr.get()
assert result == {"hello": 4, "world": 3, "fly": 2}, \
    f"Expected {{hello:4, world:3, fly:2}}, got {result}"

master.stop()
INFO("[PASS] test_mapreduce_wordcount")
