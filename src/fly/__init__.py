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

import logging
import os

logger = logging.getLogger("fly")

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
        logger.warning("open_db: path '%s' already contains a database, "
                       "creating new database at '%s'", path, actual_path)
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
]
