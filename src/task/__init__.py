# Re-export task module members for Bazel runfiles compatibility.
# In Bazel runfiles, src/task/ becomes a package (auto-generated __init__.py).
# This file ensures _task_registry is importable via `from task import _task_registry`.
try:
    from task.py.task import _task_registry, as_task, task_name
except ImportError:
    pass
