import os
import sys
import logging


def init(log_dir="fly_log", worker_mode=False, worker_id=0,
         master_host="127.0.0.1", master_port=0):
    logging.basicConfig(level=logging.DEBUG)

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
