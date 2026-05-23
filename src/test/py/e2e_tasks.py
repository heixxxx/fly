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


@as_task()
def write_and_remove(db, key, value):
    db.write_object(key, value)
    db.remove_object(key)


@as_task(inputs=lambda db, key, deps: list(deps))
def read_after_remove(db, key, deps):
    return db.read_object(key)


@as_task()
def write_remove_other(db, write_key, write_value, remove_key):
    db.write_object(write_key, write_value)
    db.remove_object(remove_key)


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
