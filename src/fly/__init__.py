from .database import _Database
from .config import get_config
from .task import as_task, task_name
from .runtime import get_agent
from .agent import Master, Worker, FlyAgent


def open_db(path: str, data_path: str = "") -> _Database:
    return _Database(path, data_path)


def load_db(path: str) -> '_Database':
    from .runtime import get_agent
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