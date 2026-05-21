import sys
import os

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '../bazel-bin/src/agent/export'))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '../bazel-bin/src/log/export'))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '../bazel-bin/src/storage/export'))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '../bazel-bin/src/core/export'))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from fly import as_task


@as_task()
def write_data(db, key, value):
    db.write_object(key, value)


@as_task(inputs=lambda db, dep_keys: list(dep_keys))
def freeze_db(db, dep_keys):
    db.write_object("finish", 1)
    db.freeze()


@as_task(inputs=lambda db, key, deps: list(deps))
def read_data(db, key, deps):
    return db.read_object(key)


@as_task()
def write_after_freeze(db, key, value):
    db.write_object(key, value)


@as_task()
def fanout_write(db, keys, values):
    for k, v in zip(keys, values):
        write_data(db, k, v)


@as_task(requires=["gpu"])
def gpu_write(db, key, value):
    db.write_object(key, value)


@as_task()
def cpu_write(db, key, value):
    db.write_object(key, value)
