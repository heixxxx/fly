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


def _stop_coverage():
    """Flush coverage data on exit.

    Coverage is now started at interpreter boot via sitecustomize.py (see
    docs/coverage-testing.md §12.1).  ``coverage.process_startup`` registers
    its own atexit saver, but C++ ``graceful_exit.cpp`` may call ``_exit()``,
    which skips Python atexit handlers.  This explicit stop/save covers that
    path and is harmless when no coverage is running.
    """
    try:
        import coverage
        cov = coverage.Coverage.current()
        if cov is not None:
            cov.stop()
            cov.save()
    except Exception:
        pass


def _cleanup():
    _stop_coverage()

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


def _run_worker():
    import time
    from _fly_core import ex_core_get_process_info, ex_core_get_config
    from fly.runtime import get_agent

    proc = ex_core_get_process_info()
    cfg = ex_core_get_config()

    # Coverage is now started at interpreter boot via sitecustomize.py
    # (see docs/coverage-testing.md §12.1).  No coverage.start() here — it
    # would miss the fly-package imports that already ran before _run_worker.

    _redirect_worker_io(proc.worker_id(), cfg.get_str("log_dir"))

    init()

    from fly.runtime import get_agent
    from _fly_log import INFO
    INFO("Worker process starting: id=" + str(proc.worker_id()))

    agent = get_agent()
    INFO("Worker agent created, starting poll loop")

    while agent.is_running():
        agent.poll_task_blocking(100)
    INFO("Worker poll loop exited, running cleanup")

    agent.stop()
    INFO("Worker agent stopped")
    # Coverage stop/save is handled centrally by _cleanup() -> _stop_coverage().


def _run_master():
    from _fly_core import ex_core_get_process_info
    from fly.bootstrap import get_script_namespace

    # Coverage is now started at interpreter boot via sitecustomize.py
    # (see docs/coverage-testing.md §12.1).  No coverage.start() here — it
    # would miss the fly-package imports that already ran before _run_master.

    init()

    proc = ex_core_get_process_info()
    script_path = proc.script_path()
    interactive = proc.interactive()

    # 用户脚本/交互 shell 的命名空间：由 bootstrap 预加载 fly + solver 并注入公共 API，
    # 使用户脚本零 import 即可直接 help() / SolverProject() / open_db()。
    # 脚本与 shell 共享同一份 ns，使脚本里定义的符号在随后进入 shell 时仍可用。
    script_ns = get_script_namespace()

    if script_path:
        sys.argv = [script_path]
        sys._fly_script_path = script_path
        script_dir = os.path.dirname(os.path.abspath(script_path))
        if script_dir not in sys.path:
            sys.path.insert(0, script_dir)
        script_ns["__file__"] = script_path
        with open(script_path) as f:
            compiled = compile(f.read(), script_path, "exec")
        exec(compiled, script_ns)

    if interactive:
        # -i flag: enter interactive shell after script (or directly).
        # stop() will be called by _cleanup when user exits.
        code.interact(banner="Fly Shell", local=script_ns, exitmsg="")
    elif script_path:
        # Script mode (non-interactive): auto-stop after script completes.
        from fly.runtime import get_agent
        agent = get_agent()
        if agent is not None and agent.is_running():
            agent.stop()


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
        _safe_cleanup()
        raise
    except KeyboardInterrupt:
        print("", file=sys.stderr)
    except Exception:
        import traceback
        traceback.print_exc(file=sys.stderr)
        _safe_cleanup()
        return 1

    _safe_cleanup()
    return 0


def _safe_cleanup():
    """_cleanup with full error logging — never lets cleanup exceptions
    escape silently (which would cause a non-zero exit code with no traceback,
    turning real failures into opaque timeouts/FAILED in runqa)."""
    try:
        _cleanup()
    except Exception:
        import traceback
        print("[fly] WARNING: _cleanup() raised an exception:", file=sys.stderr)
        traceback.print_exc(file=sys.stderr)
