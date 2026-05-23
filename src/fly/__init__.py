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
    from task.task import as_task, task_name
except ImportError:
    from task.py.task import as_task, task_name

try:
    from agent.agent import Master, Worker, FlyAgent
except ImportError:
    from agent.py.agent import Master, Worker, FlyAgent

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
    from fly.runtime import get_agent
    agent = get_agent()
    if not isinstance(agent, Master):
        raise RuntimeError("load_db can only be called from Master")
    return agent.load_db(path)


def __getattr__(name):
    if name == "agent":
        return get_agent()
    raise AttributeError(f"module 'fly' has no attribute {name}")


__all__ = [
    'open_db', 'load_db', 'agent', 'get_agent', 'get_config',
    'as_task', 'task_name',
    'Master', 'Worker', 'FlyAgent',
]
