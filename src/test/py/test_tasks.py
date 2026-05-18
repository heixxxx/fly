import sys
import os

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '../../../bazel-bin/src/agent/export'))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '../../../bazel-bin/src/log/export'))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '../../../bazel-bin/src/storage/export'))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '../../../bazel-bin/src/core/export'))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '../../../bazel-bin/src/test/export'))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))

from fly import as_task
from _fly_test import EXTestObject, ex_test_parallel_read


@as_task()
def write_data_task(db, idx):
    obj = EXTestObject(idx, "test_obj_" + str(idx))
    db.write_object("test." + str(idx), obj)


@as_task(inputs=lambda db, names, out_key: [db.get_obj_name(n) for n in names])
def concurrent_read_task(db, names, out_key):
    short_names = [n.split(":")[-1] if ":" in n else n for n in names]
    total, local_count, remote_count = ex_test_parallel_read(db, short_names)
    db.write_object(out_key, (total, local_count, remote_count))


@as_task(inputs=lambda db, dep_keys: list(dep_keys))
def freeze_db_task(db, dep_keys):
    db.freeze()


@as_task()
def entry_task(db):
    # Phase 1: fan-out writes
    for i in range(10):
        write_data_task(db, i)

    # Phase 2: 3 concurrent read tasks with overlapping object dependencies
    # chunks: [0..3], [3..6], [6..9] — each triggers cross-worker parallel reads
    chunk_keys = [
        (1, ["test.0", "test.1", "test.2", "test.3"]),
        (2, ["test.3", "test.4", "test.5", "test.6"]),
        (3, ["test.6", "test.7", "test.8", "test.9"]),
    ]
    for idx, keys in chunk_keys:
        concurrent_read_task(db, keys, f"summary.{idx}")

    # Phase 3: freeze depends on all 3 summaries
    summary_keys = ["summary.1", "summary.2", "summary.3"]
    freeze_db_task(db, [db.get_obj_name(k) for k in summary_keys])
