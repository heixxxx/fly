import pickle
import logging

try:
    import cloudpickle
except ImportError:
    cloudpickle = None

logger = logging.getLogger("fly")

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
        requires: list[str]，任务所需的 worker 能力标签。

    Usage::

        @as_task(inputs=lambda db: [])
        def my_task(db):
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
            serialized = _serialize_args(args)

            task_name = func_payload if func_payload is not None else name

            write_context_hash = ex_stg_compute_write_context_hash(
                task_name, module, serialized, task_inputs)

            agent.submit(task_name, module, serialized, task_inputs,
                         required_capabilities=task_requires,
                         write_context_hash=write_context_hash)
            logger.debug(
                f"Task submitted via {agent.mode}: "
                f"name={name}, module={module}, inputs={task_inputs}, requires={task_requires}")

        wrapper._fly_original_func = func
        wrapper._fly_task_name = name
        return wrapper

    return decorator


def wait_obj(inputs=None, poll_interval=0.1):
    """装饰器：等待依赖对象就绪后执行函数。

    与 @as_task 不同，@wait_obj 不会将函数提交给任务系统，
    而是在本地轮询 DataService 等待依赖对象可用后直接执行。

    Args:
        inputs: 与 @as_task 相同，callable(*args, **kwargs) -> list[str]，
                返回依赖对象的全名列表（需用 db.get_obj_name() 获取）。
        poll_interval: 轮询间隔（秒），默认 0.1 秒。

    Usage:
        @wait_obj(inputs=lambda db, key: [db.get_obj_name("dep1")])
        def process(db, key):
            return db.read_object(key)

        result = process(db, "my_key")  # 阻塞直到 dep1 就绪后执行
    """
    def decorator(func):
        def wrapper(*args, **kwargs):
            deps = inputs(*args, **kwargs) if inputs else []
            _wait_for_objects(deps, poll_interval)
            return func(*args, **kwargs)

        wrapper._fly_original_func = func
        return wrapper

    return decorator


def _wait_for_objects(deps, poll_interval):
    """阻塞等待所有依赖对象在 DataService 中可用。

    轮询策略：
    1. 快速检查 local_idx + remote_idx 缓存
    2. 若都不命中，触发 try_read_remote（Tier 3 → Master 查询）
       — Worker 端会缓存 remote_idx，下次轮询命中
    3. 若 Master 返回 can_still_produce=false（无 pending/running 任务），抛出 RuntimeError
    """
    if not deps:
        return

    import time
    from _fly_storage import ex_stg_get_data_service

    ds = ex_stg_get_data_service()

    pending = list(deps)
    last_probe = {dep: 0.0 for dep in deps}
    probe_interval = max(poll_interval * 5, 0.5)

    while pending:
        still_pending = []
        for dep in pending:
            if ds.has_local_object(dep) or ds.has_remote_location(dep):
                logger.debug("wait_obj: object '%s' is ready", dep)
                continue

            now = time.time()
            if now - last_probe[dep] >= probe_interval:
                last_probe[dep] = now
                found, _data, _py_name, can_still_produce = ds.try_read_remote(dep)
                if found:
                    logger.debug("wait_obj: object '%s' found via Tier 3 probe", dep)
                    continue
                if not can_still_produce:
                    raise RuntimeError(
                        f"wait_obj: object '{dep}' cannot be produced — "
                        f"no pending or running tasks on master")

            still_pending.append(dep)

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
