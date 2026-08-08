"""E2E test: MapReduce comprehensive coverage — full merge, pre-partitioned,
keep_intermediate, multi-stage tree, downstream dependency, error paths."""
from _fly_log import INFO
import time
import os
import shutil

BASE_PATH = f"/tmp/fly_e2e_mr_coverage_{os.getpid()}"


from fly import open_db, get_config, MapReduceJob, wait_tasks


def cleanup(path):
    if os.path.isdir(path):
        shutil.rmtree(path, ignore_errors=True)
    # Also clean auto-incremented variants
    for d in [path, path + ".1", path + ".2", path + ".3"]:
        if os.path.isdir(d):
            shutil.rmtree(d, ignore_errors=True)


def wait_for(condition, timeout=30.0, interval=0.5):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if condition():
            return True
        time.sleep(interval)
    return False


def fresh_db(name):
    path = f"{BASE_PATH}/{name}"
    cleanup(path)
    return open_db(path)


# ── Test 1: Full merge (single-stage with concurrent reads) ────────────────

def test_full_merge():
    db = fresh_db("full_merge")

    mr = MapReduceJob(db, output_name="sorted_result")
    mr.set_partitioner(lambda data: [sorted(data[i::2]) for i in range(2)])
    mr.set_processor(lambda part: part)
    mr.set_merger(lambda a, b: sorted(a + b), "full")
    mr.run([5, 3, 8, 1, 9, 2, 7, 4, 6, 0])

    assert wait_tasks(timeout=60.0)
    result = mr.get()
    assert result == [0, 1, 2, 3, 4, 5, 6, 7, 8, 9], \
        f"Expected sorted list, got {result}"
    INFO("[PASS] test_full_merge")


# ── Test 2: Pre-partitioned (skip partition phase) ─────────────────────────

def test_pre_partitioned():
    db = fresh_db("pre_part")

    from test import write_data
    write_data(db, "shard_0", [1, 2, 3])
    write_data(db, "shard_1", [4, 5, 6])
    assert wait_tasks(timeout=30.0)

    mr = MapReduceJob(db, output_name="pre_part_result")
    mr.set_pre_partitioned(["shard_0", "shard_1"])
    mr.set_processor(lambda part: sum(part))
    mr.set_merger(lambda a, b: a + b, "summary")
    mr.run()

    assert wait_tasks(timeout=60.0)
    result = mr.get()
    assert result == 21, f"Expected 21 (1+2+3+4+5+6), got {result}"
    INFO("[PASS] test_pre_partitioned")


# ── Test 3: Keep intermediate data ────────────────────────────────────────

def test_keep_intermediate():
    db = fresh_db("keep_inter")

    mr = MapReduceJob(db, output_name="keep_result", keep_intermediate=True)
    mr.set_partitioner(lambda data: [data[i::2] for i in range(2)])
    mr.set_processor(lambda part: sum(part))
    mr.set_merger(lambda a, b: a + b, "summary")
    mr.run([10, 20, 30, 40])

    assert wait_tasks(timeout=60.0)
    result = mr.get()
    assert result == 100, f"Expected 100, got {result}"

    # Intermediate temp objects should still be readable
    read_count = 0
    for key in mr._processed_keys:
        try:
            db.read_object(key)
            read_count += 1
        except Exception:
            pass
    assert read_count > 0, \
        f"Intermediate data should be preserved (keep_intermediate=True), but 0/{len(mr._processed_keys)} readable"
    INFO(f"[PASS] test_keep_intermediate ({read_count} intermediates preserved)")


# ── Test 4: Multi-stage tree merge (>8 partitions) ────────────────────────

def test_multi_stage_merge():
    db = fresh_db("tree_merge")

    num_parts = 16
    data = list(range(1, 65))

    mr = MapReduceJob(db, output_name="tree_result")
    mr.set_partitioner(lambda d: [d[i::num_parts] for i in range(num_parts)])
    mr.set_processor(lambda part: sum(part))
    mr.set_merger(lambda a, b: a + b, "summary")
    mr.run(data)

    assert wait_tasks(timeout=60.0)
    result = mr.get()
    expected = sum(range(1, 65))
    assert result == expected, f"Expected {expected}, got {result}"
    INFO("[PASS] test_multi_stage_merge (16 partitions -> 2-stage tree)")


# ── Test 5: MR as downstream task dependency ───────────────────────────────

def test_downstream_dependency():
    db = fresh_db("downstream")

    mr = MapReduceJob(db, output_name="upstream_result")
    mr.set_partitioner(lambda data: [data[i::2] for i in range(2)])
    mr.set_processor(lambda part: sum(part))
    mr.set_merger(lambda a, b: a + b, "summary")
    mr.run([10, 20, 30, 40])

    from test import mr_downstream_read
    mr_downstream_read(db, mr, "downstream_output")

    assert wait_tasks(timeout=60.0)
    downstream = db.read_object("downstream_output")
    assert downstream == "downstream:100", \
        f"Expected 'downstream:100', got {downstream}"
    INFO("[PASS] test_downstream_dependency")


# ── Test 6: Error paths ───────────────────────────────────────────────────

def test_error_paths():
    db = fresh_db("errors")
    errors = []

    mr1 = MapReduceJob(db, output_name="err1")
    mr1.set_processor(lambda x: x)
    try:
        mr1.run([1, 2, 3])
        errors.append("Missing merger should raise")
    except ValueError as e:
        assert "Merger" in str(e)

    mr2 = MapReduceJob(db, output_name="err2")
    mr2.set_merger(lambda a, b: a + b)
    try:
        mr2.run([1, 2, 3])
        errors.append("Missing processor should raise")
    except ValueError as e:
        assert "Processor" in str(e)

    mr3 = MapReduceJob(db, output_name="err3")
    mr3.set_processor(lambda x: x)
    mr3.set_merger(lambda a, b: a + b)
    try:
        mr3.run([1, 2, 3])
        errors.append("Missing partitioner should raise")
    except ValueError as e:
        assert "Partitioner" in str(e)

    mr4 = MapReduceJob(db, output_name="err4")
    mr4.set_partitioner(lambda d: [d])
    mr4.set_processor(lambda x: x)
    mr4.set_merger(lambda a, b: a + b)
    try:
        mr4.run()
        errors.append("Missing input_data should raise")
    except ValueError as e:
        assert "input_data" in str(e)

    if errors:
        raise AssertionError(errors[0])

    INFO("[PASS] test_error_paths (4/4)")


# ── Run all ───────────────────────────────────────────────────────────────

get_config().set_int("fail_unscheduleable_tasks", 0)

from fly.runtime import get_agent
master = get_agent()

master.launch_local_workers([{}, {}, {}])
assert wait_for(lambda: master.worker_count >= 3, timeout=10.0), \
    f"3 workers should connect, got {master.worker_count}"

test_full_merge()
test_pre_partitioned()
test_multi_stage_merge()
test_downstream_dependency()
test_keep_intermediate()
test_error_paths()

master.stop()
INFO("\nAll MapReduce coverage tests passed!")
