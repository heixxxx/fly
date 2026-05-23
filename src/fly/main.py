import os
import sys
import code
from _fly_log import DBG, ERR, INFO, WARN

def init():
    from core import get_config
    from fly.runtime import get_agent, configure_master, configure_worker
    
    cfg = get_config()
    if cfg.get_int("worker_mode"):
        configure_worker()
    else:
        configure_master()

    agent = get_agent()
    
    INFO(f"Fly initialized: mode={agent.mode}")


def _cleanup():
    try:
        from fly.runtime import get_agent, reset
        agent = get_agent()
        if agent is not None:
            reset()
            del agent
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

    try:
        from _fly_storage import ex_stg_get_storage_manager
        sm = ex_stg_get_storage_manager()
        sm.close_all()
    except Exception:
        pass

    import gc
    for _ in range(3):
        gc.collect()


def _redirect_worker_io(worker_id, log_dir):
    os.makedirs(log_dir, exist_ok=True)
    log_path = os.path.join(log_dir, f"worker{worker_id}.log")
    log_file = open(log_path, "a")
    sys.stdout = log_file
    sys.stderr = log_file


def _run_worker():
    import time
    from core import get_config
    from fly.runtime import get_agent

    cfg = get_config()
    _redirect_worker_io(cfg.get_int("worker_id"), cfg.get_str("log_dir"))

    init()

    from fly.runtime import get_agent
    from _fly_log import INFO
    INFO("Worker process starting: id=" + str(cfg.get_int("worker_id")))

    agent = get_agent()
    INFO("Worker agent created, starting poll loop")

    while agent._agent.is_running():
        agent._agent.poll_task()
        time.sleep(0.05)
    while agent._agent.is_running():
        agent._agent.poll_task()
        time.sleep(0.05)


def _run_master():
    from core import get_config
    init()

    cfg = get_config()
    script_path = cfg.get_str("script_path")
    interactive = cfg.get_int("interactive")

    if script_path:
        sys.argv = [script_path]
        sys._fly_script_path = script_path
        with open(script_path) as f:
            compiled = compile(f.read(), script_path, "exec")
            exec(compiled, {"__name__": "__main__", "__file__": script_path})

    if interactive or not script_path:
        code.interact(banner="Fly Shell", exitmsg="")


def run():
    try:
        from fly.runtime import _config_is_worker_mode
        if _config_is_worker_mode():
            _run_worker()
        else:
            _run_master()
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


__all__ = ["init", "run"]
