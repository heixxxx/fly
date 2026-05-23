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


__all__ = ['as_task', 'task_name']
