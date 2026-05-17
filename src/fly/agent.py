import os
import sys
import threading
import subprocess
import logging
from abc import ABC, abstractmethod

from _fly_agent import EXAgentMaster, EXAgentWorker, EXTaskExecutor, EXTaskExecStatus

from .executor import create_executor

logger = logging.getLogger("fly")


class FlyAgent(ABC):
    """Agent 基类 — Master 和 Worker 的公共接口"""

    @property
    @abstractmethod
    def mode(self) -> str:
        raise NotImplementedError

    @abstractmethod
    def submit(self, name: str, module: str, args: list,
               inputs: list = None) -> int:
        raise NotImplementedError

    @abstractmethod
    def start(self):
        raise NotImplementedError

    @abstractmethod
    def stop(self):
        raise NotImplementedError


class Master(FlyAgent):
    """Master Agent — 主进程模式

    管理任务调度、Worker 生命周期、依赖图。
    """

    @property
    def mode(self) -> str:
        return "master"

    def __init__(self, host: str = "127.0.0.1", port: int = 0):
        self._agent = EXAgentMaster(host, port)
        self._task_counter = 0
        self._lock = threading.Lock()
        self._workers = []
        self._worker_procs = []
        self._host = host
        self._port = port
        self._running = False

    @property
    def port(self) -> int:
        if self._running:
            return self._agent.get_port()
        return self._port

    def start(self):
        if self._running:
            return
        self._agent.start()
        self._port = self._agent.get_port()
        self._running = True
        logger.debug(f"Master started on {self._host}:{self._port}")

    def submit(self, name: str, module: str, args: list,
                inputs: list = None) -> int:
        with self._lock:
            self._task_counter += 1
            task_id = self._task_counter

        if not self._running:
            self.start()

        self._agent.submit_task_with_deps(
            task_id, name, module, args, inputs or [], [])
        logger.debug(f"Task submitted: id={task_id}, name={name}")
        return task_id

    def launch_local_workers(self, worker_configs: list, port: int = None,
                              mode: str = "thread"):
        if port is not None:
            self._port = port

        self.start()
        self._port = self._agent.get_port()

        num_workers = len(worker_configs)
        for i in range(num_workers):
            if mode == "process":
                self._spawn_process_worker(i + 1)
            else:
                self._start_thread_worker(i + 1)

        logger.debug(
            f"Master running on {self._host}:{self._port}, "
            f"{num_workers} workers launched (mode={mode})")

    def stop(self):
        for w in self._workers:
            w.stop()
        for proc in self._worker_procs:
            if proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    proc.kill()
        if self._running:
            self._agent.stop()
            self._running = False

    @property
    def pending_tasks(self):
        return self._agent.get_pending_tasks()

    @property
    def running_tasks(self):
        return self._agent.get_running_tasks()

    @property
    def completed_tasks(self):
        return self._agent.get_completed_tasks()

    def _start_thread_worker(self, worker_id: int):
        """Phase 2: 线程内 Worker"""
        from _fly_log import init_worker
        import time

        init_worker(worker_id, "fly_worker_logs")

        executor = EXTaskExecutor()
        executor.set_exec_func(
            lambda tid, tname, tmod, targs:
            self._default_executor(tid, tname, tmod, targs))

        worker = EXAgentWorker(worker_id, self._host, self._port)
        worker.set_executor(executor)
        worker.start()
        time.sleep(0.1)

        self._workers.append(worker)

    def _spawn_process_worker(self, worker_id: int):
        """Phase 3: 子进程 Worker — 启动 fly binary"""
        import time
        from .runtime import _log_dir

        fly_bin = self._find_fly_binary()

        cmd = [
            fly_bin,
            "--worker",
            "--worker-id", str(worker_id),
            "--master-host", self._host,
            "--master-port", str(self._port),
            "--log-dir", _log_dir,
        ]

        proc = subprocess.Popen(cmd, stdin=subprocess.DEVNULL)
        self._worker_procs.append(proc)
        time.sleep(0.1)

        logger.debug(
            f"Spawned worker process: pid={proc.pid}, "
            f"worker_id={worker_id}")

    @staticmethod
    def _find_fly_binary() -> str:
        """查找 fly 可执行文件路径"""
        # 1. 开发环境: 通过 bazel-bin 路径
        import shutil
        fly_on_path = shutil.which("fly")
        if fly_on_path:
            return fly_on_path

        # 2. 相对于 Python 包路径查找
        #    .so files are in bazel-bin/src/.../export/
        #    fly binary is in bazel-bin/src/main/cpp/fly
        import _fly_agent
        agent_dir = os.path.dirname(os.path.abspath(_fly_agent.__file__))
        # agent_dir = bazel-bin/src/agent/export
        bazel_bin = os.path.dirname(os.path.dirname(os.path.dirname(agent_dir)))
        candidate = os.path.join(bazel_bin, "src", "main", "cpp", "fly")
        if os.path.isfile(candidate) and os.access(candidate, os.X_OK):
            return candidate

        raise RuntimeError(
            f"Cannot find fly binary. Searched: PATH, {candidate}")

    @staticmethod
    def _default_executor(task_id, task_name, task_module, args):
        """Phase 2 默认 executor — 仅记录。Phase 3 替换为真实执行。"""
        from _fly_agent import EXTaskExecResult, EXTaskExecStatus as Status
        ret = EXTaskExecResult()
        ret.task_id = task_id
        ret.status = Status.SUCCESS
        ret.output = ""
        ret.error = ""
        ret.outputs = []
        logger.debug(
            f"Worker executed task: id={task_id}, name={task_name}")
        return ret


class Worker(FlyAgent):
    """Worker Agent — 工作进程模式

    Phase 2: stub — submit 抛出 NotImplementedError。
    Phase 3: 完整实现，包括自动任务执行和递归提交。
    """

    @property
    def mode(self) -> str:
        return "worker"

    def __init__(self, worker_id: int, master_host: str, master_port: int):
        self._agent = EXAgentWorker(worker_id, master_host, master_port)
        self._db_cache = {}
        self._db_path_pending = {}
        self._master_host = master_host
        self._master_port = master_port
        self._worker_id = worker_id

    def start(self):
        self._executor = EXTaskExecutor()
        self._executor.set_exec_func(create_executor(self))
        self._agent.set_executor(self._executor)
        self._agent.start()
        logger.debug(
            f"Worker {self._worker_id} started, "
            f"connected to {self._master_host}:{self._master_port}")

    def submit(self, name: str, module: str, args: list,
                inputs: list = None) -> int:
        return self._agent.submit_task(name, module, args, inputs or [])

    def get_database(self, db_id: str):
        if db_id not in self._db_cache:
            raise RuntimeError(
                f"Unknown db_id: {db_id}, need master info (Phase 3)")
        return self._db_cache[db_id]

    def stop(self):
        self._agent.stop()


__all__ = ['FlyAgent', 'Master', 'Worker']
