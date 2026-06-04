from typing import Optional

try:
    from agent.agent import FlyAgent, Master, Worker
except ImportError:
    from agent.py.agent import FlyAgent, Master, Worker

_agent: Optional[FlyAgent] = None

_mode: str = "master"


def _config_is_worker_mode():
    from _fly_core import ex_core_get_process_info
    return ex_core_get_process_info().worker_mode()


def configure_worker():
    global _mode
    _mode = "worker"


def configure_master():
    global _mode
    _mode = "master"


def get_agent() -> FlyAgent:
    global _agent
    if _agent is None:
        _agent = _create_agent()
    return _agent


def _create_agent() -> FlyAgent:
    from _fly_log import DBG
    from _fly_core import ex_core_get_process_info

    proc = ex_core_get_process_info()

    if _mode == "worker":
        attrs_str = proc.worker_attributes()
        attributes = [a.strip() for a in attrs_str.split(",") if a.strip()] if attrs_str else []
        w = Worker(proc.worker_id(),
                    proc.master_host(),
                    proc.cli_master_port(),
                    attributes=attributes)
        w.start()
        DBG(f"Worker mode: id={proc.worker_id()}, master={proc.master_host()}:{proc.cli_master_port()}, attributes={attributes}")
        return w
    else:
        m = Master()
        DBG("Master mode: auto-initialized")
        return m


def reset():
    global _agent
    if _agent is not None:
        _agent.stop()
        _agent = None


__all__ = ["get_agent"]
