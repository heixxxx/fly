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
            cov.save()  # pragma: no cover（采集自反身路径）
    except Exception:  # pragma: no cover
        pass


def _cleanup():
    import time as _ct
    _ct0 = _ct.monotonic()
    def _clog(stage):
        try:
            from _fly_log import INFO as _INFO
            _INFO("_cleanup stage '{}' took {:.3f}s".format(stage, _ct.monotonic() - _ct0))
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
    _clog("agent_reset")

    try:
        from _fly_storage import ex_stg_get_data_service
        ds = ex_stg_get_data_service()
        ds.drain_write_back()
        ds.stop_write_back()
        ds.stop_transfer_server()
    except Exception:
        pass
    _clog("storage_drain")

    try:
        from _fly_storage import ex_stg_get_storage_manager
        sm = ex_stg_get_storage_manager()
        sm.close_all()
    except Exception:
        pass
    _clog("storage_close")

    import gc
    gc.collect()
    _clog("gc_collect")

    # coverage 停止必须在全部清理动作之后：先 stop 会把其后执行的清理
    # 代码（agent reset / storage drain / gc）从采集里剔除（覆盖率假缺
    # 口）。C++ graceful_exit 可能 _exit() 跳过 atexit 的路径仍由
    # _stop_coverage 的显式 stop+save 兜底——只是时机移到末尾。
    _stop_coverage()
    _clog("coverage_stop")  # pragma: no cover（tracer 已停，自身不可采）


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

    # 执行上提主循环：GIL 由本线程全程掌控（take_task 空等段在 C++ 侧
    # GIL 释放，不压制同进程 Python 线程；旧 poll_task_blocking 持 GIL
    # 阻塞曾是每 RPC 固定 100ms 延迟的根源）。
    while agent.is_running():
        agent.poll_loop(100)
    import time as _wt
    _wt0 = _wt.monotonic()
    INFO("Worker poll loop exited, running cleanup")

    # 退出码（graceful=0/abnormal=3）：stop 拆除 agent 前已在 Worker 内缓存，
    # 经 run() → sys.exit 透传到进程退出码（bsub/ssh 外部观测方判定用）。
    code = agent.exit_code()
    agent.stop()
    INFO("Worker agent stopped (agent.stop took {:.3f}s)".format(_wt.monotonic() - _wt0))
    # Coverage stop/save is handled centrally by _cleanup() -> _stop_coverage().
    return code


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
            sys.path.insert(0, script_dir)  # pragma: no cover（脚本目录注入 arc，QA 走模块形态）
        script_ns["__file__"] = script_path
        with open(script_path) as f:
            compiled = compile(f.read(), script_path, "exec")
        exec(compiled, script_ns)

    if interactive:
        # -i flag: enter interactive shell after script (or directly).
        # stop() will be called by _cleanup when user exits.
        code.interact(banner="Fly Shell", local=script_ns, exitmsg="")  # pragma: no cover（交互 REPL，无 tty）
    elif script_path:
        # Script mode (non-interactive): auto-stop after script completes.
        from fly.runtime import get_agent
        agent = get_agent()
        if agent is not None and agent.is_running():
            agent.stop()


def _dump_on_signal(sig, frame):
    """SIGUSR1 handler: dump all thread stacks (Python + native) to
    .fly.{pid}.stack then exit.

    Triggered by runqa (on timeout, before SIGKILL) or by Master.stop() (on
    slow worker exit). Captures the exact location of the hang/latency for
    both master and worker processes.

    Writes two sections per thread:
      - Python stack via sys._current_frames() (shows where each Python thread
        is in interpreted code).
      - Native kernel stack via /proc/self/task/<tid>/stack (shows where each
        OS thread is blocked — futex/epoll/nanosleep — essential for C++/GIL
        hangs the Python stack alone cannot reveal, e.g. the reactor thread
        blocked on schedule_mutex_).
    """
    import os, traceback, threading  # pragma: no cover（信号转储：os._exit 跳过 atexit，coverage 结构上不可采）
    from _fly_core import ex_core_get_config
    try:
        log_dir = ex_core_get_config().get_str("log_dir")
    except Exception:
        log_dir = "."
    pid = os.getpid()
    path = os.path.join(log_dir, f".fly.{pid}.stack")
    try:
        with open(path, "w") as f:
            f.write(f"=== SIGUSR1 stack dump (pid={pid}) sig={sig} ===\n")
            # Section 1: Python thread stacks (interpreted frames).
            f.write("\n########## Python thread stacks ##########\n")
            for tid, stack in sys._current_frames().items():
                tname = "?"
                for t in threading.enumerate():
                    if t.ident == tid:
                        tname = t.name
                        break
                f.write(f"\n--- Python Thread {tname} (tid={tid}) ---\n")
                traceback.print_stack(stack, file=f)
            # Section 2: native kernel stacks for ALL OS threads (incl. C++).
            # reactor / task-executor / data-server threads have no Python frame;
            # only the kernel stack reveals where they are blocked.
            f.write("\n########## Native kernel stacks (/proc/self/task) ##########\n")
            try:
                for t_ in os.listdir("/proc/self/task"):
                    comm = "?"
                    wchan = "?"
                    kstack = ""
                    try:
                        comm = open(f"/proc/self/task/{t_}/comm").read().strip()
                    except Exception:
                        pass
                    try:
                        wchan = open(f"/proc/self/task/{t_}/wchan").read().strip()
                    except Exception:
                        pass
                    try:
                        kstack = open(f"/proc/self/task/{t_}/stack").read().strip()
                    except Exception:
                        pass  # 权限不足时为空（非 root），可接受
                    f.write(f"\n--- OS Thread tid={t_} comm={comm} wchan={wchan} ---\n")
                    if kstack:
                        f.write(kstack + "\n")
                    else:
                        f.write("  (kernel stack unavailable)\n")
            except Exception as _e:
                f.write(f"(failed to read native stacks: {_e})\n")
            f.write(f"\n=== end dump ===\n")
    except Exception:
        pass
    # 直接退出，不跑 atexit（那个可能就是慢的原因）
    os._exit(42)


def run():
    def _sigterm_handler(sig, frame):
        # 先置 C++ 信号灯再退出：worker 的 C++ is_running() 轮询与 master 的
        # heartbeat drain 线程由此观察到 SIGTERM（Python handler 只在主线程
        # 字节码边界执行，若主线程阻塞在 C 调用中，仅剩信号灯通道生效）。
        try:
            from _fly_agent import ex_agent_set_graceful_shutdown
            ex_agent_set_graceful_shutdown()
        except Exception:
            pass
        raise SystemExit(0)

    signal.signal(signal.SIGTERM, _sigterm_handler)
    signal.signal(signal.SIGUSR1, _dump_on_signal)

    try:
        from fly.runtime import _config_is_worker_mode
        if _config_is_worker_mode():
            rc = _run_worker()
            if rc:
                # 异常退出（MASTER_LOST/REGISTRATION_REJECTED → 3）：退出码
                # 透传给 sys.exit，进程级可观测（用户裁定语义）。
                _safe_cleanup()
                return rc
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
