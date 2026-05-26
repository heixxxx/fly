import pickle
import logging

logger = logging.getLogger("fly")

_task_registry = {}


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
    def decorator(func):
        name = getattr(func, '_fly_task_name', None) or func.__name__
        module = func.__module__ or "__main__"

        if module == "__main__":
            import sys
            import os
            main_file = getattr(sys, '_fly_script_path', sys.argv[0] if sys.argv else "")
            if main_file:
                basename = os.path.splitext(os.path.basename(main_file))[0]
                module = basename

        task_requires = requires or []

        _task_registry[(module, name)] = func

        def wrapper(*args, **kwargs):
            from fly.runtime import get_agent
            agent = get_agent()

            task_inputs = inputs(*args, **kwargs) if inputs else []
            serialized = _serialize_args(args)

            agent.submit(name, module, serialized, task_inputs,
                         required_capabilities=task_requires)
            logger.debug(
                f"Task submitted via {agent.mode}: "
                f"name={name}, inputs={task_inputs}, requires={task_requires}")

        wrapper._fly_original_func = func
        wrapper._fly_task_name = name
        return wrapper

    return decorator


def wait_obj(inputs=None, timeout=30.0, poll_interval=0.1):
    """装饰器：等待依赖对象就绪后执行函数。

    与 @as_task 不同，@wait_obj 不会将函数提交给任务系统，
    而是在本地轮询 DataService 等待依赖对象可用后直接执行。

    Args:
        inputs: 与 @as_task 相同，callable(*args, **kwargs) -> list[str]，
                返回依赖对象的全名列表（需用 db.get_obj_name() 获取）。
        timeout: 超时时间（秒），默认 30 秒。
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
            _wait_for_objects(deps, timeout, poll_interval)
            return func(*args, **kwargs)

        wrapper._fly_original_func = func
        return wrapper

    return decorator


def _wait_for_objects(deps, timeout, poll_interval):
    """阻塞等待所有依赖对象在 DataService 中可用。

    轮询策略：
    1. 快速检查 local_idx + remote_idx 缓存
    2. 若都不命中，触发 try_read_remote（Tier 3 → Master 查询）
       — Worker 端会缓存 remote_idx，下次轮询命中
    3. 超时抛 TimeoutError
    """
    if not deps:
        return

    import time
    from _fly_storage import ex_stg_get_data_service

    ds = ex_stg_get_data_service()
    t0 = time.time()

    pending = list(deps)
    # Track last probe time per dep to avoid flooding Tier 3
    last_probe = {dep: 0.0 for dep in deps}
    probe_interval = max(poll_interval * 5, 0.5)  # probe every ~500ms

    while pending:
        still_pending = []
        for dep in pending:
            if ds.has_local_object(dep) or ds.has_remote_location(dep):
                logger.debug("wait_obj: object '%s' is ready", dep)
                continue

            now = time.time()
            if now - last_probe[dep] >= probe_interval:
                last_probe[dep] = now
                found, _data, _py_name = ds.try_read_remote(dep)
                if found:
                    logger.debug("wait_obj: object '%s' found via Tier 3 probe", dep)
                    continue

            still_pending.append(dep)

        pending = still_pending

        if not pending:
            return

        if time.time() - t0 >= timeout:
            raise TimeoutError(
                f"wait_obj: timed out after {timeout:.1f}s "
                f"waiting for objects: {pending}. "
                f"Check if upstream tasks are producing these objects.")

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
