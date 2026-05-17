import logging
from typing import Optional

from .agent import FlyAgent, Master, Worker

logger = logging.getLogger("fly")

_agent: Optional[FlyAgent] = None

_mode: str = "master"
_worker_id: int = 0
_master_host: str = "127.0.0.1"
_master_port: int = 0
_log_dir: str = "fly_log"


def configure_worker(worker_id: int, master_host: str, master_port: int,
                     log_dir: str = "fly_log"):
    global _mode, _worker_id, _master_host, _master_port, _log_dir
    _mode = "worker"
    _worker_id = worker_id
    _master_host = master_host
    _master_port = master_port
    _log_dir = log_dir


def configure_master(log_dir: str = "fly_log"):
    global _mode, _log_dir
    _mode = "master"
    _log_dir = log_dir


def get_agent() -> FlyAgent:
    global _agent
    if _agent is None:
        _agent = _create_agent()
    return _agent


def _create_agent() -> FlyAgent:
    if _mode == "worker":
        w = Worker(_worker_id, _master_host, _master_port)
        w.start()
        logger.debug(f"Worker mode: id={_worker_id}, master={_master_host}:{_master_port}")
        return w
    else:
        m = Master()
        logger.debug("Master mode: auto-initialized")
        return m


def reset():
    global _agent
    if _agent is not None:
        _agent.stop()
        _agent = None


__all__ = ['get_agent', 'reset', 'configure_worker', 'configure_master']
