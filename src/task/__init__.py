# Re-export task module members for Bazel runfiles compatibility.
try:
    from task.py.task import _task_registry, as_task, task_name, wait_obj
    from task.py.task import _USER_MODULE, _USER_FUNC_PREFIX
except ImportError:
    pass
