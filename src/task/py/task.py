import pickle

try:
    import cloudpickle
except ImportError:
    cloudpickle = None

from _fly_log import DBG

_task_registry = {}

_USER_MODULE = "from_user"
_USER_FUNC_PREFIX = "__user_func__:"


def task_name(name: str):
    """装饰器：设置任务名称。

    必须在 @as_task 外层使用：
        @as_task(inputs=...)
        @task_name("my_task")
        def my_func(db, name):
            ...
    """
    def decorator(func):
        func._fly_task_name = name
        return func
    return decorator


def as_task(inputs=None, requires=None):
    """将函数注册为可分发任务。

    装饰器会拦截函数调用，将任务提交给 Agent（Master 或 Worker）执行。

    Args:
        inputs: callable(*args, **kwargs) -> list[str]，返回依赖对象名列表。
        requires: 任务所需的 worker 能力标签，支持以下形式：
            - list[str]: 能力标签列表，死等（必须满足才调度）
            - tuple(list[str], float): (能力标签列表, 属性依赖超时秒数)
                - timeout < 0: 死等（等价纯 list）
                - timeout == 0: 数据依赖满足后仅检查一次，无完整匹配立即降级
                  到匹配属性最多的 idle worker
                - timeout > 0: 数据依赖满足后限时等待；到期后降级调度
            - callable(*args, **kwargs): 返回上述任一形式，在提交时动态解析

    Usage::

        @as_task(inputs=lambda db: [])
        def my_task(db):
            ...

        @as_task(requires=["gpu"])
        def gpu_task(db):
            ...

        @as_task(requires=(["gpu"], 5.0))  # 5秒后降级
        def soft_gpu_task(db):
            ...
    """
    def decorator(func):
        name = getattr(func, '_fly_task_name', None) or func.__name__
        module = func.__module__ or "__main__"

        if module == "__main__":
            module = _USER_MODULE

        task_requires = requires or []

        if module == _USER_MODULE:
            try:
                serializer = cloudpickle if cloudpickle is not None else pickle
                func_payload = _USER_FUNC_PREFIX + serializer.dumps(func).hex()
            except Exception as exc:
                raise ValueError(
                    f"Failed to serialize user task function {name!r}. "
                    f"User-script tasks must be pickle-serializable. "
                    f"Original error: {exc}"
                ) from exc
        else:
            func_payload = None
            _task_registry[(module, name)] = func

        def wrapper(*args, **kwargs):
            from fly.runtime import get_agent
            from _fly_storage import ex_stg_compute_write_context_hash
            agent = get_agent()

            task_inputs = inputs(*args, **kwargs) if inputs else []
            if callable(task_requires):
                resolved = task_requires(*args, **kwargs)
            else:
                resolved = task_requires

            # 解析为 (caps, attribute_timeout)
            # tuple 形式：(list[str], float)；其他形式：纯 list，默认死等 (timeout<0)
            if isinstance(resolved, tuple) and len(resolved) == 2:
                caps, attr_timeout = resolved
            else:
                caps, attr_timeout = list(resolved), -1.0

            serialized = _serialize_args(args)

            task_name = func_payload if func_payload is not None else name

            write_context_hash = ex_stg_compute_write_context_hash(
                task_name, module, serialized, task_inputs)

            agent.submit(task_name, module, serialized, task_inputs,
                         required_capabilities=caps,
                         attribute_timeout=attr_timeout,
                         write_context_hash=write_context_hash)
            DBG(
                f"Task submitted via {agent.mode}: "
                f"name={name}, module={module}, inputs={task_inputs}, "
                f"requires={caps}, attr_timeout={attr_timeout}")

        wrapper._fly_original_func = func
        wrapper._fly_task_name = name
        return wrapper

    return decorator


def wait_obj(inputs=None, poll_interval=0.1, timeout=None):
    """装饰器：等待依赖对象就绪后执行函数。

    与 @as_task 不同，@wait_obj 不会将函数提交给任务系统，
    而是在本地轮询 DataService 等待依赖对象可用后直接执行。

    Args:
        inputs: 与 @as_task 相同，callable(*args, **kwargs) -> list[str]，
                返回依赖对象的全名列表（需用 db.get_obj_name() 获取）。
        poll_interval: 轮询间隔（秒），默认 0.1 秒。
        timeout: 超时秒数。None（默认）= 永远等待，直到数据可读或确认无法产出。

    Usage:
        @wait_obj(inputs=lambda db, key: [db.get_obj_name("dep1")])
        def process(db, key):
            return db.read_object(key)

        result = process(db, "my_key")  # 阻塞直到 dep1 就绪后执行
    """
    def decorator(func):
        import functools
        @functools.wraps(func)
        def wrapper(*args, **kwargs):
            deps = inputs(*args, **kwargs) if inputs else []
            _wait_for_objects(deps, poll_interval, timeout)
            return func(*args, **kwargs)

        wrapper._fly_original_func = func
        return wrapper

    return decorator


def _wait_for_objects(deps, poll_interval, timeout=None):
    """阻塞等待所有依赖对象在 DataService 中可用。

    轮询策略：
    1. 快速检查 local_idx + remote_idx 缓存
    2. 若都不命中，触发 try_read_remote（Tier 3 → Master 查询）
       — Worker 端会缓存 remote_idx，下次轮询命中
    3. 若 Master 返回 can_still_produce=false（无 pending/running 任务），
       连续确认多次后才判定失败（容忍任务链的竞态窗口）

    Args:
        deps: 待等待的对象全名列表
        poll_interval: 轮询间隔（秒）
        timeout: 超时秒数。None = 永远等待（直到数据可读或确认无法产出）
    """
    if not deps:
        return

    import time
    from _fly_storage import ex_stg_get_data_service

    ds = ex_stg_get_data_service()

    pending = list(deps)
    last_probe = {dep: 0.0 for dep in deps}
    probe_interval = max(poll_interval * 5, 0.5)
    # can_still_produce=false 的连续确认计数
    # 任务链中旧任务完成和新任务注册之间有竞态窗口，需要多次确认
    fail_confirm_count = {dep: 0 for dep in deps}
    fail_confirm_threshold = 3
    start_time = time.monotonic() if timeout is not None else None

    while pending:
        still_pending = []
        for dep in pending:
            if ds.has_local_object(dep) or ds.has_remote_location(dep):
                DBG(f"wait_obj: object '{dep}' is ready")
                continue

            now = time.time()
            if now - last_probe[dep] >= probe_interval:
                last_probe[dep] = now
                found, _data, _py_name, can_still_produce = ds.try_read_remote(dep)
                if found:
                    DBG(f"wait_obj: object '{dep}' found via Tier 3 probe")
                    continue
                if not can_still_produce:
                    fail_confirm_count[dep] += 1
                    if fail_confirm_count[dep] >= fail_confirm_threshold:
                        raise RuntimeError(
                            f"wait_obj: object '{dep}' cannot be produced — "
                            f"no pending or running tasks on master (confirmed {fail_confirm_threshold}x)")
                else:
                    fail_confirm_count[dep] = 0

            still_pending.append(dep)

        if timeout is not None and start_time is not None:
            if time.monotonic() - start_time >= timeout:
                raise TimeoutError(
                    f"wait_obj: timed out after {timeout}s waiting for {still_pending}")

        pending = still_pending

        if not pending:
            return

        time.sleep(poll_interval)


def _serialize_args(args):
    result = []
    for arg in args:
        if hasattr(arg, 'get_db_id') and hasattr(arg, 'get_obj_name'):
            base_path = arg._db.get_base_path()
            data_path = arg._db.get_data_path()
            result.append(f"__fly_db__:{arg.get_db_id()}:{base_path}:{data_path}")
        else:
            result.append(pickle.dumps(arg).hex())
    return result


__all__ = ['as_task', 'task_name', 'wait_obj']
