from fly import as_task, wait_obj


@as_task()
def write_data(db, key, value):
    db.write_object(key, value)


@as_task()
def failing_task(db, key, error_msg):
    raise RuntimeError(error_msg)


@as_task()
def partial_write_then_fail(db, dirty_keys, clean_key, error_msg):
    """先写入若干对象（dirty_keys），再写入一个正常对象（clean_key），
    最后抛异常。用于测试异常清理：dirty_keys 和 clean_key 都应被撤销
    （属于同一个失败 task 的写入段）。"""
    for k in dirty_keys:
        db.write_object(k, f"dirty:{k}")
    db.write_object(clean_key, "clean_data")
    raise RuntimeError(error_msg)


@as_task()
def write_multiple(db, kv_pairs):
    """写入多个键值对（成功 task，用于对照测试）。"""
    for k, v in kv_pairs:
        db.write_object(k, v)


@as_task(inputs=lambda db, stage_id, n_stages, result_key:
         [db.get_full_name(f"stage{stage_id-1}/result")] if stage_id > 0 else [])
def chain_stage(db, stage_id, n_stages, result_key):
    """依赖链的一个阶段（扁平依赖，多 worker 并发）。

    时序设计（配合多 worker + sleep）验证"下游被调度后读不到文件而失败"：
      - 上游先写 result（依赖就绪），下游被调度执行
      - 下游读 result 前 sleep，给上游失败清理留时间窗口
      - 上游写 dirty 后失败 → result 被清理（文件删除）
      - 下游 sleep 结束读 result → 文件不存在 → 失败（连锁失败）

    stage_id == FLY_FAIL_AT：写 dirty 后抛异常。
    stage_id > FLY_FAIL_AT：读 result 前 sleep（等上游失败清理）。
    """
    import os
    import time

    fail_at = int(os.environ.get("FLY_FAIL_AT", "-1"))

    # 下游 stage（在失败点之后）读数据前 sleep，等上游失败清理
    if fail_at >= 0 and stage_id > fail_at:
        time.sleep(2.0)

    if stage_id > 0:
        upstream = db.read_object(f"stage{stage_id-1}/result")
    else:
        upstream = 0

    result = upstream + stage_id
    db.write_object(result_key, result)
    db.write_object(f"stage{stage_id}/intermediate", result * 2)

    if stage_id == fail_at:
        db.write_object(f"stage{stage_id}/dirty1", "dirty_data_1")
        db.write_object(f"stage{stage_id}/dirty2", "dirty_data_2")
        raise RuntimeError(f"chain failure at stage {stage_id}")


@as_task(inputs=lambda db, node_id, deps: [db.get_full_name(f"node{d}/result") for d in deps])
def dag_node(db, node_id, deps):
    """DAG 的一个节点（图状依赖，多输入汇聚）。

    deps 是上游 node_id 列表（汇聚节点有多个上游）。所有上游 result 就绪后被调度。
    读所有上游 → 求和写自己的 result。

    时序设计（多 worker + sleep）验证"下游被调度后读不到文件而失败"：
      - 失败节点写 dirty 后抛异常 → 其 result 被清理
      - 失败节点的下游（deps 含该节点）读 result 前 sleep，等清理后读不到 → 失败

    FLY_FAIL_NODES（逗号分隔）：哪些 node_id 会失败。
    FLY_DOWNSTREAM_SLEEP：下游节点读前 sleep 秒数（默认 0）。
    """
    import os
    import time

    fail_nodes = set(os.environ.get("FLY_FAIL_NODES", "").split(","))
    sleep_sec = float(os.environ.get("FLY_DOWNSTREAM_SLEEP", "0"))

    downstream_of_fail = bool(os.environ.get("FLY_FAIL_NODES")) and \
        any(str(d) in fail_nodes for d in deps)

    if downstream_of_fail and sleep_sec > 0:
        time.sleep(sleep_sec)

    total = 0
    for d in deps:
        total += db.read_object(f"node{d}/result")

    db.write_object(f"node{node_id}/result", total + node_id)
    db.write_object(f"node{node_id}/intermediate", total * 2)

    if str(node_id) in fail_nodes:
        db.write_object(f"node{node_id}/dirty1", f"dirty:{node_id}")
        db.write_object(f"node{node_id}/dirty2", f"dirty:{node_id}")
        raise RuntimeError(f"dag_node {node_id} intentional failure")


@as_task(inputs=lambda db, task_id, dep_key:
         [db.get_full_name(dep_key)] if dep_key else [])
def fail_write_task(db, task_id, dep_key=""):
    """读取依赖（如有）后写 dirty 对象，然后失败。

    FLY_FAIL_TASKS 环境变量（逗号分隔）指定哪些 task_id 会失败。
    未命中时不失败（restart 场景用）。dep_key 非空时声明数据依赖。
    """
    import os
    if dep_key:
        db.read_object(dep_key)   # 依赖上游（验证依赖恢复）

    db.write_object(f"task{task_id}/dirty1", f"dirty:{task_id}")
    db.write_object(f"task{task_id}/dirty2", f"dirty:{task_id}")

    fail_set = set(os.environ.get("FLY_FAIL_TASKS", "").split(","))
    if str(task_id) in fail_set:
        raise RuntimeError(f"fail_write_task {task_id} intentional failure")

    # 不失败时写入正常结果
    db.write_object(f"task{task_id}/result", f"ok:{task_id}")


@as_task(inputs=lambda db, key, value: [db.get_full_name("phantom")])
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


@as_task(inputs=lambda target_db, source_db, source_key, target_key: [source_db.get_full_name(source_key)])
def cross_db_copy(target_db, source_db, source_key, target_key):
    data = source_db.read_object(source_key)
    target_db.write_object(target_key, data)


@as_task(inputs=lambda target_db, db_a, db_b, key_a, key_b, target_key:
         [db_a.get_full_name(key_a), db_b.get_full_name(key_b)])
def cross_db_sum(target_db, db_a, db_b, key_a, key_b, target_key):
    a = db_a.read_object(key_a)
    b = db_b.read_object(key_b)
    target_db.write_object(target_key, a + b)


@as_task()
def add_alpha_property(db, key, value):
    from fly.runtime import get_agent
    get_agent().set_worker_property("alpha")
    db.write_object(key, value)


@as_task(inputs=lambda target_db, source_db, source_key, target_key: [source_db.get_full_name(source_key)],
         requires=["alpha"])
def alpha_cross_db_copy(target_db, source_db, source_key, target_key):
    data = source_db.read_object(source_key)
    target_db.write_object(target_key, data)


@as_task(inputs=lambda target_db, source_db, source_key, target_key: [source_db.get_full_name(source_key)],
         requires=["gpu"])
def gpu_cross_db_copy(target_db, source_db, source_key, target_key):
    data = source_db.read_object(source_key)
    target_db.write_object(target_key, data)


@as_task(inputs=lambda target_db, db_raw, db_feat, key_raw, key_feat, target_key:
         [db_raw.get_full_name(key_raw), db_feat.get_full_name(key_feat)])
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

    @wait_obj(inputs=lambda d, k: [d.get_full_name(k)])
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
@as_task(inputs=lambda db, source_key: [db.get_full_name(source_key)])
def cpu_read_large_remote(db, source_key):
    data = db.read_object(source_key)
    return len(data)


# ── Var service tasks ──

@as_task()
def set_var_task(db, name, value):
    """Set a var (small object)."""
    db.set_var(name, value)


@as_task()
def get_var_task(db, name):
    """Read a var and return it (or None)."""
    return db.get_var(name)


@as_task()
def remove_var_task(db, name):
    """Remove a var."""
    db.remove_var(name)


@as_task()
def set_var_and_write(db, var_name, var_value, obj_key, obj_value):
    """Set a var THEN write an object — establishes the implicit dependency:
    once obj_key's data dependency is satisfied, var_name is guaranteed
    retrievable on master (same-connection FIFO)."""
    db.set_var(var_name, var_value)
    db.write_object(obj_key, obj_value)


@as_task(inputs=lambda db, obj_key, var_name: [db.get_full_name(obj_key)],
         vars=lambda db, obj_key, var_name: [db.get_full_name(var_name)])
def read_obj_and_var(db, obj_key, var_name):
    """Task that depends on obj_key (data dep) AND declares var_name (inline var).
    The var is inlined by master into TaskAssignMessage, so get_var hits the local
    cache without a round-trip."""
    obj_val = db.read_object(obj_key)
    var_val = db.get_var(var_name)
    return (obj_val, var_val)
