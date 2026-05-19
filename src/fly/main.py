import os
import sys
import logging
import code
import argparse


def _import_all_internal_modules():
    import ctypes
    old_flags = sys.getdlopenflags()
    sys.setdlopenflags(old_flags | ctypes.RTLD_GLOBAL)

    import _fly_core
    import _fly_log
    import _fly_storage
    import _fly_agent
    import _fly_test

    sys.setdlopenflags(old_flags)

    from fly import open_db, get_agent, get_config
    from fly import as_task, task_name
    from fly import Master, Worker, FlyAgent
    from fly.database import _Database
    from fly.runtime import get_agent as _ga, configure_master, configure_worker
    from fly.executor import create_executor

    _test_py = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'test', 'py')
    _test_py = os.path.normpath(_test_py)
    if _test_py not in sys.path:
        sys.path.insert(0, _test_py)
    import test_tasks


def init(log_dir="fly_log", worker_mode=False, worker_id=0,
         master_host="127.0.0.1", master_port=0):

    _import_all_internal_modules()

    if worker_mode:
        from _fly_log import init_worker
        init_worker(worker_id, log_dir + "/")
        from .runtime import configure_worker
        configure_worker(worker_id, master_host, master_port, log_dir)
    else:
        from .log_setup import setup_log_dir
        log_dir = setup_log_dir(log_dir)

        master_log_path = os.path.join(log_dir, "master.log")
        logging.basicConfig(
            level=logging.DEBUG,
            format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
            handlers=[
                logging.FileHandler(master_log_path, mode="a"),
                logging.StreamHandler(sys.stderr),
            ],
        )

        from _fly_log import init_master
        init_master(log_dir + "/")
        from .runtime import configure_master
        configure_master(log_dir)

    from .runtime import get_agent
    agent = get_agent()
    print(f"Fly initialized: mode={agent.mode}", file=sys.stderr)


def _cleanup():
    try:
        from .runtime import get_agent, reset
        agent = get_agent()
        if agent is not None:
            reset()
    except Exception:
        pass

    try:
        from _fly_storage import ex_stg_get_data_service
        ds = ex_stg_get_data_service()
        ds.drain_write_back()
        ds.stop_write_back()
        ds.stop_transfer_server()
    except Exception:
        pass

    import gc
    gc.collect()


def _setup_worker_logging(worker_id, log_dir):
    """Redirect Python stdout/stderr and logging to worker log file."""
    worker_log_dir = os.path.join(log_dir, "workers")
    os.makedirs(worker_log_dir, exist_ok=True)
    log_path = os.path.join(worker_log_dir, f"worker_{worker_id}.log")

    log_file = open(log_path, "a")

    sys.stdout = log_file
    sys.stderr = log_file

    logging.basicConfig(
        level=logging.DEBUG,
        format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
        handlers=[
            logging.StreamHandler(log_file),
        ],
    )


def _run_worker(worker_id, master_host, master_port, log_dir):
    import time

    _setup_worker_logging(worker_id, log_dir)

    init(
        worker_mode=True,
        worker_id=worker_id,
        master_host=master_host,
        master_port=master_port,
        log_dir=log_dir,
    )

    from .runtime import get_agent
    agent = get_agent()
    while agent._agent.is_running():
        agent._agent.poll_task()
        time.sleep(0.05)


def _run_master(log_dir, script_path, interactive):
    init(log_dir=log_dir)

    if script_path:
        sys.argv = [script_path]
        sys._fly_script_path = script_path
        with open(script_path) as f:
            compiled = compile(f.read(), script_path, "exec")
            exec(compiled, {"__name__": "__main__", "__file__": script_path})

    if interactive or not script_path:
        code.interact(banner="Fly Shell", exitmsg="")


def run(argv=None):
    parser = argparse.ArgumentParser(prog="fly", add_help=True)
    parser.add_argument("--worker", action="store_true")
    parser.add_argument("--worker-id", type=int, default=0)
    parser.add_argument("--master-host", default="127.0.0.1")
    parser.add_argument("--master-port", type=int, default=0)
    parser.add_argument("--log-dir", default="fly_log")
    parser.add_argument("-i", action="store_true")
    parser.add_argument("script", nargs="?", default=None)
    args = parser.parse_args(argv[1:])

    try:
        if args.worker:
            _run_worker(
                worker_id=args.worker_id,
                master_host=args.master_host,
                master_port=args.master_port,
                log_dir=args.log_dir,
            )
        else:
            _run_master(
                log_dir=args.log_dir,
                script_path=args.script,
                interactive=args.i,
            )
    except SystemExit:
        raise
    except KeyboardInterrupt:
        print("", file=sys.stderr)
    except Exception as e:
        print(f"Fatal error: {e}", file=sys.stderr)
        _cleanup()
        return 1

    _cleanup()
    return 0


__all__ = ['init', 'run']
