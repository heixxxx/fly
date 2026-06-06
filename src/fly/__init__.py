"""Fly — Distributed task execution framework.

Public API for creating databases, submitting tasks, and managing workers.

Example::

    import fly

    db = fly.open_db("./my_db")
    fly.launch_workers([{"role": "worker"}])

    @fly.as_task(inputs=lambda db: [])
    def my_task(db):
        ...

    fly.wait_tasks()
"""

import os

from _fly_log import WARN

try:
    from storage.database import _Database
except ImportError:
    from storage.py.database import _Database

try:
    from core import get_config
except ImportError:
    from core.py import get_config

try:
    from task.task import as_task, task_name, wait_obj
except ImportError:
    from task.py.task import as_task, task_name, wait_obj

from fly.runtime import get_agent
from fly.mapreduce import MapReduceJob


def open_db(path: str, data_path: str = "") -> '_Database':
    """Open a new database.

    If ``path`` already contains a database, auto-creates a numbered variant
    (``path.1``, ``path.2``, ...).

    Args:
        path: Directory path for the database.
        data_path: Optional separate path for data storage.

    Returns:
        A ``_Database`` instance.
    """
    actual_path = path
    n = 0
    while os.path.exists(os.path.join(actual_path, '_DB_META')):
        n += 1
        actual_path = f"{path}.{n}"
    if actual_path != path:
        WARN(f"open_db: path '{path}' already contains a database, "
             f"creating new database at '{actual_path}'")
    return _Database(actual_path, data_path)


def load_db(path: str) -> '_Database':
    """Load an existing database from a previous run.

    Must be called on the Master node.

    Args:
        path: Directory path of the existing database.

    Returns:
        A ``_Database`` instance.
    """
    return get_agent().load_db(path)


def launch_workers(configs: list):
    """Launch local worker processes.

    Each config dict supports a ``'role'`` key.

    Args:
        configs: List of config dicts, one per worker.
    """
    get_agent().launch_local_workers(configs)


def wait_tasks(timeout: float = 30.0):
    """Block until all submitted tasks complete or timeout expires.

    Args:
        timeout: Maximum seconds to wait. Defaults to 30.

    Returns:
        True if all tasks completed, False if timed out.
    """
    return get_agent().wait_for_all_tasks(timeout=timeout)


def restart_failed_tasks(path: str):
    """Re-submit previously failed tasks from a persisted file.

    Args:
        path: Path to the persisted task failure file.
    """
    get_agent().restart_failed_tasks(path)


def get_task_error(task_id: int) -> str:
    """Get the error message for a failed task.

    Args:
        task_id: The ID of the failed task.

    Returns:
        Error message string.
    """
    return get_agent().get_task_error(task_id)


def put_cache(key: str, value):
    """Store a Python object in the local agent cache.

    The cache lives for the lifetime of the agent process (Master or Worker)
    and is strictly local — not shared across workers.  Useful for passing
    data between tasks on the same worker without network/disk I/O.

    Args:
        key: String key for the cached value.
        value: Any Python object.
    """
    get_agent().put_cache(key, value)


def get_cache(key: str, default=None):
    """Retrieve a cached Python object by key.

    Args:
        key: String key that was used with :func:`put_cache`.
        default: Value to return if *key* is not found.

    Returns:
        The cached Python object, or *default* if not found.
    """
    return get_agent().get_cache(key, default)


def has_cache(key: str) -> bool:
    """Return ``True`` if *key* exists in the local agent cache."""
    return get_agent().has_cache(key)


def remove_cache(key: str):
    """Remove a single entry from the local agent cache.

    Raises:
        KeyError: If *key* is not in the cache.
    """
    get_agent().remove_cache(key)


def clear_cache():
    """Remove all entries from the local agent cache."""
    get_agent().clear_cache()


def __getattr__(name):
    if name == "completed_tasks":
        return get_agent().completed_tasks
    if name == "pending_tasks":
        return get_agent().pending_tasks
    if name == "running_tasks":
        return get_agent().running_tasks
    if name == "failed_tasks":
        return get_agent().failed_tasks
    if name == "port":
        return get_agent().port
    raise AttributeError(f"module 'fly' has no attribute {name}")


__all__ = [
    'open_db', 'load_db', 'get_config',
    'as_task', 'task_name', 'wait_obj',
    'launch_workers', 'wait_tasks',
    'restart_failed_tasks', 'get_task_error',
    'completed_tasks', 'pending_tasks', 'running_tasks', 'failed_tasks',
    'get_agent', 'MapReduceJob',
    'put_cache', 'get_cache', 'has_cache', 'remove_cache', 'clear_cache',
]
