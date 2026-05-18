import os
import sys
import logging


def _import_all_internal_modules():
    """Import all internal modules so Worker and Master share the same environment."""
    sys.setdlopenflags(os.RTLD_NOW | os.RTLD_GLOBAL)

    import _fly_core
    import _fly_log
    import _fly_storage
    import _fly_agent
    import _fly_test

    # Fly Python modules
    from fly import open_db, get_agent, get_config
    from fly import as_task, task_name
    from fly import Master, Worker, FlyAgent
    from fly.database import _Database
    from fly.runtime import get_agent as _ga, configure_master, configure_worker
    from fly.executor import create_executor

    # Test task modules (add src/test/py to sys.path for Worker processes)
    _test_py = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'test', 'py')
    _test_py = os.path.normpath(_test_py)
    if _test_py not in sys.path:
        sys.path.insert(0, _test_py)
    import test_tasks


def init(log_dir="fly_log", worker_mode=False, worker_id=0,
         master_host="127.0.0.1", master_port=0):
    logging.basicConfig(level=logging.DEBUG)

    _import_all_internal_modules()

    if worker_mode:
        from _fly_log import init_worker
        init_worker(worker_id, log_dir + "/")
        from .runtime import configure_worker
        configure_worker(worker_id, master_host, master_port, log_dir)
    else:
        from .log_setup import setup_log_dir
        log_dir = setup_log_dir(log_dir)
        from _fly_log import init_master
        init_master(log_dir + "/")
        from .runtime import configure_master
        configure_master(log_dir)

    from .runtime import get_agent
    agent = get_agent()
    print(f"Fly initialized: mode={agent.mode}", file=sys.stderr)


__all__ = ['init']
