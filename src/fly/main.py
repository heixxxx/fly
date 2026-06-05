import os
import sys
import code
import signal
from _fly_log import DBG, ERR, INFO, WARN

def init():
    from _fly_core import ex_core_get_process_info
    from fly.runtime import get_agent, configure_master, configure_worker
    
    proc = ex_core_get_process_info()
    if proc.worker_mode():
        configure_worker()
    else:
        configure_master()

    agent = get_agent()
    agent.start()
    
    INFO(f"Fly initialized: mode={agent.mode}")


def _cleanup():
    cov = globals().get('_fly_worker_cov')
    if cov is not None:
        try:
            cov.stop()
            cov.save()
        except Exception:
            pass

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
    gc.collect()


def _redirect_worker_io(worker_id, log_dir):
    os.makedirs(log_dir, exist_ok=True)
    log_path = os.path.join(log_dir, f"worker{worker_id}.log")
    log_file = open(log_path, "a")
    sys.stdout = log_file
    sys.stderr = log_file


def _save_worker_coverage():
    cov = globals().get('_fly_worker_cov')
    if cov is not None:
        try:
            cov.stop()
            cov.save()
        except Exception:
            pass


def _run_worker():
    import time
    import atexit
    from _fly_core import ex_core_get_process_info, ex_core_get_config
    from fly.runtime import get_agent

    proc = ex_core_get_process_info()
    cfg = ex_core_get_config()

    # Start Python coverage if FLY_PYCOVERAGE env var is set (passed from Master)
    global _fly_worker_cov
    _fly_worker_cov = None
    if os.environ.get("FLY_PYCOVERAGE"):
        try:
            import coverage
            data_file = os.environ.get("FLY_PYCOVERAGE_DATA",
                                       "/tmp/.coverage.fly.worker_" + str(proc.worker_id()))
            rcfile = os.environ.get("FLY_PYCOVERAGE_RCFILE")
            kwargs = dict(branch=True, data_file=data_file,
                          source=["fly", "agent", "storage", "task"])
            if rcfile and os.path.exists(rcfile):
                kwargs["config_file"] = rcfile
            _fly_worker_cov = coverage.Coverage(**kwargs)
            _fly_worker_cov.start()
            atexit.register(_save_worker_coverage)
        except Exception as e:
            with open("/tmp/fly_worker_cov_error.txt", "a") as f:
                f.write("coverage start failed: " + str(e) + "\n")

    _redirect_worker_io(proc.worker_id(), cfg.get_str("log_dir"))

    init()

    from fly.runtime import get_agent
    from _fly_log import INFO
    INFO("Worker process starting: id=" + str(proc.worker_id()))

    agent = get_agent()
    INFO("Worker agent created, starting poll loop")

    while agent.is_running():
        agent.poll_task()
        time.sleep(0.05)
    INFO("Worker poll loop exited, running cleanup")

    agent.stop()
    INFO("Worker agent stopped")

    # Save coverage on normal exit (atexit also saves as safety net)
    if _fly_worker_cov is not None:
        try:
            _fly_worker_cov.stop()
            _fly_worker_cov.save()
            INFO("Worker coverage data saved")
        except Exception as e:
            INFO("Worker coverage save failed: " + str(e))


def _run_master():
    import atexit
    from _fly_core import ex_core_get_process_info

    # Start Python coverage if FLY_PYCOVERAGE is set
    global _fly_worker_cov  # reuse same global name for cleanup
    _fly_worker_cov = None
    if os.environ.get("FLY_PYCOVERAGE"):
        try:
            import coverage
            data_file = os.environ.get("FLY_PYCOVERAGE_DATA",
                                       "/tmp/.coverage.fly.master." + str(os.getpid()))
            kwargs = dict(branch=True, data_file=data_file,
                          source=["fly", "agent", "storage", "task"])
            rcfile = os.environ.get("FLY_PYCOVERAGE_RCFILE")
            if rcfile and os.path.exists(rcfile):
                kwargs["config_file"] = rcfile
            _fly_worker_cov = coverage.Coverage(**kwargs)
            _fly_worker_cov.start()
            atexit.register(_save_worker_coverage)
        except Exception as e:
            with open("/tmp/fly_master_cov_error.txt", "a") as f:
                f.write("master coverage start failed: " + str(e) + "\n")

    init()

    proc = ex_core_get_process_info()
    script_path = proc.script_path()
    interactive = proc.interactive()

    if script_path:
        sys.argv = [script_path]
        sys._fly_script_path = script_path
        with open(script_path) as f:
            compiled = compile(f.read(), script_path, "exec")
            exec(compiled, {"__name__": "__main__", "__file__": script_path})

    if interactive or not script_path:
        code.interact(banner="Fly Shell", exitmsg="")


def run():
    def _sigterm_handler(sig, frame):
        raise SystemExit(0)

    signal.signal(signal.SIGTERM, _sigterm_handler)

    try:
        from fly.runtime import _config_is_worker_mode
        if _config_is_worker_mode():
            _run_worker()
        else:
            _run_master()
    except SystemExit:
        _cleanup()
        raise
    except KeyboardInterrupt:
        print("", file=sys.stderr)
    except Exception:
        import traceback
        traceback.print_exc(file=sys.stderr)
        _cleanup()
        return 1

    _cleanup()
    return 0


__all__ = ["init", "run"]
