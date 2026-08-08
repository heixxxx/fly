"""MapReduce Coverage Report — runs all MR paths.

When run with FLY_PYCOVERAGE=1, the Fly runtime automatically tracks
Python coverage for Master and Worker processes. No manual coverage
management needed in this script.
"""
from _fly_log import INFO, ERR
import time
import os
import shutil
import traceback


from fly import open_db, get_config, MapReduceJob, wait_tasks
from fly.runtime import get_agent
from test import mr_downstream_read

passed = 0
failed = 0

def run_test(name, fn):
    global passed, failed
    try:
        fn()
        INFO(f"[PASS] {name}")
        passed += 1
    except Exception as e:
        INFO(f"[FAIL] {name}: {e}")
        ERR(traceback.format_exc())
        failed += 1

def fresh_db(name):
    path = f"/tmp/fly_cov_{name}_db"
    if os.path.isdir(path):
        shutil.rmtree(path, ignore_errors=True)
    return open_db(path)

def wait_for(condition, timeout=30.0, interval=0.5):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if condition():
            return True
        time.sleep(interval)
    return False

get_config().set_int("fail_unscheduleable_tasks", 0)

# ══════════════════════════════════════════════════════
# TEST 1: Summary merge
# ══════════════════════════════════════════════════════
def test_summary_merge():
    master = get_agent()
    master.launch_local_workers([{}, {}, {}])
    assert wait_for(lambda: master.worker_count >= 3, timeout=10.0)

    db = fresh_db("summary")
    mr = MapReduceJob(db, output_name="sum_result")
    mr.set_partitioner(lambda data: [data[i::3] for i in range(3)])
    mr.set_processor(lambda x: sum(x))
    mr.set_merger(lambda a, b: a + b, "summary")
    mr.run([10, 20, 30, 40])

    assert wait_tasks(timeout=60.0)
    result = mr.get()
    assert result == 100, f"Expected 100, got {result}"
    master.stop()

# ══════════════════════════════════════════════════════
# TEST 2: Full merge
# ══════════════════════════════════════════════════════
def test_full_merge():
    master = get_agent()
    master.launch_local_workers([{}, {}])
    assert wait_for(lambda: master.worker_count >= 2, timeout=10.0)

    db = fresh_db("full")
    mr = MapReduceJob(db, output_name="full_result")
    mr.set_partitioner(lambda data: [data[i::3] for i in range(3)])
    mr.set_processor(lambda x: x)
    mr.set_merger(lambda a, b: sorted(a + b), "full")
    mr.run([3, 1, 4, 1, 5, 9])

    assert wait_tasks(timeout=60.0)
    result = mr.get()
    assert result == [1, 1, 3, 4, 5, 9], f"Got {result}"
    master.stop()

# ══════════════════════════════════════════════════════
# TEST 3: Pre-partitioned
# ══════════════════════════════════════════════════════
def test_pre_partitioned():
    master = get_agent()
    master.launch_local_workers([{}, {}])
    assert wait_for(lambda: master.worker_count >= 2, timeout=10.0)

    db = fresh_db("prepart")
    db.write_object("shard_0", [1, 2, 3])
    db.write_object("shard_1", [4, 5, 6])

    mr = MapReduceJob(db, output_name="prepart_result")
    mr.set_pre_partitioned(["shard_0", "shard_1"])
    mr.set_processor(lambda x: sum(x))
    mr.set_merger(lambda a, b: a + b, "summary")
    mr.run(None)

    assert wait_tasks(timeout=60.0)
    result = mr.get()
    assert result == 21, f"Got {result}"
    master.stop()

# ══════════════════════════════════════════════════════
# TEST 4: Multi-stage tree merge (16 partitions → 2-stage tree)
# ══════════════════════════════════════════════════════
def test_multi_stage_merge():
    master = get_agent()
    master.launch_local_workers([{}, {}])
    assert wait_for(lambda: master.worker_count >= 2, timeout=10.0)

    db = fresh_db("multistage")
    data = list(range(1, 17))
    mr = MapReduceJob(db, output_name="multi_result")
    mr.set_partitioner(lambda d: [d[i::16] for i in range(16)])
    mr.set_processor(lambda x: sum(x))
    mr.set_merger(lambda a, b: a + b, "summary")
    mr.run(data)

    assert wait_tasks(timeout=60.0)
    result = mr.get()
    assert result == 136, f"Got {result}"
    master.stop()

# ══════════════════════════════════════════════════════
# TEST 5: With finalizer
# ══════════════════════════════════════════════════════
def test_with_finalize():
    master = get_agent()
    master.launch_local_workers([{}, {}])
    assert wait_for(lambda: master.worker_count >= 2, timeout=10.0)

    db = fresh_db("finalize")
    mr = MapReduceJob(db, output_name="final_result")
    mr.set_partitioner(lambda data: [data[i::3] for i in range(3)])
    mr.set_processor(lambda x: sum(x))
    mr.set_merger(lambda a, b: a + b, "summary")
    mr.set_finalizer(lambda x: x * 10)
    mr.run([1, 2, 3])

    assert wait_tasks(timeout=60.0)
    result = mr.get()
    assert result == 60, f"Got {result}"
    master.stop()

# ══════════════════════════════════════════════════════
# TEST 6: Keep intermediate
# ══════════════════════════════════════════════════════
def test_keep_intermediate():
    master = get_agent()
    master.launch_local_workers([{}, {}])
    assert wait_for(lambda: master.worker_count >= 2, timeout=10.0)

    db = fresh_db("keep")
    mr = MapReduceJob(db, output_name="keep_result", keep_intermediate=True)
    mr.set_partitioner(lambda data: [data[i::2] for i in range(2)])
    mr.set_processor(lambda x: sum(x))
    mr.set_merger(lambda a, b: a + b, "summary")
    mr.run([10, 20])

    assert wait_tasks(timeout=60.0)
    result = mr.get()
    assert result == 30, f"Got {result}"

    read_count = 0
    for key in mr._processed_keys:
        try:
            db.read_object(key)
            read_count += 1
        except Exception:
            pass
    assert read_count > 0, f"0/{len(mr._processed_keys)} intermediates readable"
    master.stop()

# ══════════════════════════════════════════════════════
# TEST 7: Downstream dependency + pickle
# ══════════════════════════════════════════════════════
def test_downstream_dependency():
    master = get_agent()
    master.launch_local_workers([{}, {}])
    assert wait_for(lambda: master.worker_count >= 2, timeout=10.0)

    db = fresh_db("downstream")
    mr = MapReduceJob(db, output_name="upstream_result")
    mr.set_partitioner(lambda data: [data[i::2] for i in range(2)])
    mr.set_processor(lambda x: sum(x))
    mr.set_merger(lambda a, b: a + b, "summary")
    mr.run([100, 200])

    assert wait_tasks(timeout=60.0)

    import pickle
    pickled = pickle.dumps(mr)
    mr2 = pickle.loads(pickled)

    mr_downstream_read(db, mr2, "downstream_output")

    assert wait_tasks(timeout=60.0)
    master.stop()

# ══════════════════════════════════════════════════════
# TEST 8: Error paths
# ══════════════════════════════════════════════════════
def test_errors():
    try:
        MapReduceJob(fresh_db("err1"), output_name="e1").run([1])
        assert False
    except ValueError:
        pass

    try:
        mr = MapReduceJob(fresh_db("err2"), output_name="e2")
        mr.set_merger(lambda a, b: a + b, "summary")
        mr.run([1])
        assert False
    except ValueError:
        pass

    try:
        mr = MapReduceJob(fresh_db("err3"), output_name="e3")
        mr.set_processor(lambda x: x)
        mr.set_merger(lambda a, b: a + b, "summary")
        mr.run([1])
        assert False
    except ValueError:
        pass

    try:
        mr = MapReduceJob(fresh_db("err4"), output_name="e4")
        mr.set_processor(lambda x: x)
        mr.set_merger(lambda a, b: a + b, "summary")
        mr.run(None)
        assert False
    except ValueError:
        pass

# ══════════════════════════════════════════════════════
# RUN ALL
# ══════════════════════════════════════════════════════
run_test("summary_merge", test_summary_merge)
run_test("full_merge", test_full_merge)
run_test("pre_partitioned", test_pre_partitioned)
run_test("multi_stage_merge", test_multi_stage_merge)
run_test("with_finalize", test_with_finalize)
run_test("keep_intermediate", test_keep_intermediate)
run_test("downstream_dependency", test_downstream_dependency)
run_test("error_paths", test_errors)

# ── Done ──
INFO(f"\nResults: {passed} passed, {failed} failed")
INFO("=" * 70)
