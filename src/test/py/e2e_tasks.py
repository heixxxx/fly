from fly import as_task, wait_obj


@as_task()
def write_data(db, key, value):
    db.write_object(key, value)


@as_task()
def failing_task(db, key, error_msg):
    raise RuntimeError(error_msg)


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
    try:
        db.write_object(key, value)
    except RuntimeError:
        pass


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


@as_task()
def write_and_remove(db, key, value):
    db.remove_object(key)
    db.write_object(key, value)
    db.remove_object(key)


@as_task(inputs=lambda db, key, deps: list(deps))
def read_after_remove(db, key, deps):
    return db.read_object(key)


@as_task(inputs=lambda db, read_key, write_key, deps: list(deps))
def increment(db, read_key, write_key, deps=[]):
    val = db.read_object(read_key)
    db.write_object(write_key, val + 1)


@as_task()
def write_remove_other(db, write_key, write_value, remove_key):
    db.write_object(write_key, write_value)


@as_task()
def compute_sum(db, read_key_a, read_key_b, result_key):
    a = db.read_object(read_key_a)
    b = db.read_object(read_key_b)
    db.write_object(result_key, a + b)


@as_task(inputs=lambda target_db, source_db, source_key, target_key: [source_db.get_obj_name(source_key)])
def cross_db_copy(target_db, source_db, source_key, target_key):
    data = source_db.read_object(source_key)
    target_db.write_object(target_key, data)


@as_task(inputs=lambda target_db, db_a, db_b, key_a, key_b, target_key:
         [db_a.get_obj_name(key_a), db_b.get_obj_name(key_b)])
def cross_db_sum(target_db, db_a, db_b, key_a, key_b, target_key):
    a = db_a.read_object(key_a)
    b = db_b.read_object(key_b)
    target_db.write_object(target_key, a + b)


@as_task()
def add_alpha_property(db, key, value):
    from fly.runtime import get_agent
    get_agent().set_worker_property("alpha")
    db.write_object(key, value)


@as_task(inputs=lambda target_db, source_db, source_key, target_key: [source_db.get_obj_name(source_key)],
         requires=["alpha"])
def alpha_cross_db_copy(target_db, source_db, source_key, target_key):
    data = source_db.read_object(source_key)
    target_db.write_object(target_key, data)


@as_task(inputs=lambda target_db, source_db, source_key, target_key: [source_db.get_obj_name(source_key)],
         requires=["gpu"])
def gpu_cross_db_copy(target_db, source_db, source_key, target_key):
    data = source_db.read_object(source_key)
    target_db.write_object(target_key, data)


@as_task(inputs=lambda target_db, db_raw, db_feat, key_raw, key_feat, target_key:
         [db_raw.get_obj_name(key_raw), db_feat.get_obj_name(key_feat)])
def triple_db_sum(target_db, db_raw, db_feat, key_raw, key_feat, target_key):
    raw_val = db_raw.read_object(key_raw)
    feat_val = db_feat.read_object(key_feat)
    target_db.write_object(target_key, raw_val + feat_val)


# ── wait_obj usage: task that internally waits for another task's output ──

@as_task(inputs=lambda db, dep_key, result_key: [])
def wait_obj_then_process(db, dep_key, result_key):
    """Task that uses @wait_obj to wait for upstream data, then processes it.

    The dependency is checked AT CALL TIME by @wait_obj (not via task system).
    This tests the Worker-side @wait_obj scenario.
    """

    @wait_obj(inputs=lambda d, k: [d.get_obj_name(k)])
    def wait_for_data(d, k):
        return d.read_object(k)

    data = wait_for_data(db, dep_key)
    # After waiting, process and write result
    processed = f"processed:{data}"
    db.write_object(result_key, processed)


@as_task()
def write_data_backup(db, key, value):
    db.write_object(key, value, backup=True)


@as_task(inputs=lambda db, key, deps: list(deps))
def read_data_backup(db, key, deps):
    return db.read_object(key, backup=True)


@as_task()
def write_temp(db, key, value):
    db.write_object(key, value, save_to_db=False)


@as_task()
def write_temp_large(db, key, size):
    data = list(range(size))
    db.write_object(key, data, save_to_db=False)


@as_task(inputs=lambda db, mr, output_key: [mr.get_output_name()])
def mr_downstream_read(db, mr, output_key):
    data = mr.get(db)
    db.write_object(output_key, f"downstream:{data}")


# ── Large object remote transfer profiling tasks ──

# Task 1: runs on GPU worker, writes a large temp object (~10MB when size=1_250_000).
@as_task(requires=["gpu"])
def gpu_write_large_temp(db, key, size):
    data = list(range(size))  # ~10MB when size = 1_250_000 (each int ~8 bytes)
    db.write_object(key, data, save_to_db=False)


# Task 2: runs on CPU worker, depends on task 1's output, reads it remotely.
@as_task(inputs=lambda db, source_key: [db.get_obj_name(source_key)])
def cpu_read_large_remote(db, source_key):
    data = db.read_object(source_key)
    return len(data)
