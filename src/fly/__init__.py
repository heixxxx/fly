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


def open_db(path: str, data_path: str = "") -> '_Database':
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
    return get_agent().load_db(path)


def launch_workers(configs: list):
    get_agent().launch_local_workers(configs)


def wait_tasks(timeout: float = 30.0):
    return get_agent().wait_for_all_tasks(timeout=timeout)


def restart_failed_tasks(path: str):
    get_agent().restart_failed_tasks(path)


def get_task_error(task_id: int) -> str:
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
    'get_agent',
]
