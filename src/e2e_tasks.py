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


@as_task(inputs=lambda db, key, value: [db.get_obj_name("phantom")])
def write_data_needs_phantom(db, key, value):
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


@as_task()
def add_gpu_property(db, key, value):
    from fly.runtime import get_agent
    get_agent().set_worker_property("gpu")
    db.write_object(key, value)


@as_task()
def remove_gpu_property(db, key):
    from fly.runtime import get_agent
    get_agent().remove_worker_property("gpu")
    db.write_object(key, "removed")


def _get_wid():
    from fly.runtime import get_agent
    return get_agent()._agent.get_worker_id()


@as_task(requires=["alpha"])
def alpha_write(db, key):
    db.write_object(key, _get_wid())


@as_task(requires=["beta"])
def beta_write(db, key):
    db.write_object(key, _get_wid())


@as_task(requires=["gamma"])
def gamma_write(db, key):
    db.write_object(key, _get_wid())


@as_task(requires=["shared"])
def shared_write(db, key):
    db.write_object(key, _get_wid())


@as_task(requires=["beta"])
def add_shared_on_beta(db, key):
    from fly.runtime import get_agent
    get_agent().set_worker_property("shared")
    db.write_object(key, _get_wid())


@as_task(requires=["gamma"])
def add_shared_on_gamma(db, key):
    from fly.runtime import get_agent
    get_agent().set_worker_property("shared")
    db.write_object(key, _get_wid())


@as_task(requires=["beta"])
def remove_shared_on_beta(db, key):
    from fly.runtime import get_agent
    get_agent().remove_worker_property("shared")
    db.write_object(key, _get_wid())
