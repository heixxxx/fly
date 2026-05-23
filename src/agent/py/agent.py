import os
import sys
import threading
import subprocess
from abc import ABC, abstractmethod

from _fly_agent import EXAgentMaster, EXAgentWorker, EXTaskExecutor, EXTaskExecStatus
from _fly_storage import ex_stg_get_data_service
from _fly_log import DBG, INFO, WARN, ERR 

from .executor import create_executor


class FlyAgent(ABC):

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

    @abstractmethod
    def set_worker_property(self, prop):
        raise NotImplementedError

    @abstractmethod
    def remove_worker_property(self, prop):
        raise NotImplementedError

    @abstractmethod
    def get_worker_properties(self) -> list:
        raise NotImplementedError

    @abstractmethod
    def restart_failed_tasks(self, file_path: str):
        raise NotImplementedError

    @staticmethod
    def _ensure_list(prop):
        if isinstance(prop, str):
            return [prop]
        return list(prop)


class Master(FlyAgent):

    @property
    def mode(self) -> str:
        return "master"

    def __init__(self, host: str = "127.0.0.1", port: int = 0):
        self._agent = EXAgentMaster(host, port)
        self._agent.set_data_service(ex_stg_get_data_service())
        self._task_counter = 0
        self._lock = threading.Lock()
        self._workers = []
        self._worker_procs = []
        self._host = host
        self._port = port
        self._running = False
        self._next_worker_id = 1

    @property
    def port(self) -> int:
        if self._running:
            return self._agent.get_port()
        return self._port

    def start(self):
        if self._running:
            return
        self._agent.setup_write_context()
        self._agent.start()
        self._port = self._agent.get_port()
        self._running = True
        DBG(f"Master started on {self._host}:{self._port}")

    def submit(self, name: str, module: str, args: list,
               inputs: list = None,
               required_capabilities: list = None) -> int:
        with self._lock:
            self._task_counter += 1
            task_id = self._task_counter

        if not self._running:
            self.start()

        self._agent.submit_task_with_requirements(
            task_id, name, module, args, inputs or [], [],
            required_capabilities or [])
        DBG(f"Task submitted: id={task_id}, name={name}, requires={required_capabilities}")
        return task_id

    def launch_local_workers(self, worker_configs: list, port: int = None,
                              mode: str = "process"):
        if port is not None:
            self._port = port

        self.start()
        self._port = self._agent.get_port()

        num_workers = len(worker_configs)
        for i in range(num_workers):
            config = worker_configs[i]
            if mode == "process":
                self._spawn_process_worker(i + 1, config)
            else:
                attrs = config.get("attributes", []) if isinstance(config, dict) else []
                self._start_thread_worker(i + 1, attrs)

        DBG(
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
        self._workers.clear()
        self._worker_procs.clear()

    @property
    def pending_tasks(self):
        return self._agent.get_pending_tasks()

    @property
    def running_tasks(self):
        return self._agent.get_running_tasks()

    @property
    def completed_tasks(self):
        return self._agent.get_completed_tasks()

    @property
    def failed_tasks(self):
        return self._agent.get_failed_tasks()

    def get_task_error(self, task_id: int) -> str:
        return self._agent.get_task_error(task_id)

    def wait_for_all_tasks(self, expected: int = None, timeout: float = 30.0):
        import time
        t0 = time.time()
        if expected is not None:
            while time.time() - t0 < timeout:
                completed = self._agent.get_completed_tasks()
                if len(completed) >= expected:
                    return completed
                failed = self._agent.get_failed_tasks()
                if failed:
                    raise RuntimeError(f"Tasks failed: {failed}")
                time.sleep(0.5)
        else:
            while time.time() - t0 < timeout:
                pending = self._agent.get_pending_tasks()
                running = self._agent.get_running_tasks()
                if not pending and not running:
                    return self._agent.get_completed_tasks()
                failed = self._agent.get_failed_tasks()
                if failed:
                    raise RuntimeError(f"Tasks failed: {failed}")
                time.sleep(0.5)
        return self._agent.get_completed_tasks()

    def _start_thread_worker(self, worker_id: int, attributes: list = None):
        worker_agent = EXAgentWorker(worker_id, self._host, self._port,
                                      attributes or [])
        worker_agent.set_data_service(ex_stg_get_data_service())

        class _ThreadWorker:
            def __init__(self):
                self._db_cache = {}
                self._agent = worker_agent
                self._worker_id = worker_id

        tw = _ThreadWorker()
        executor = EXTaskExecutor()
        executor.set_exec_func(create_executor(tw))

        worker_agent.set_executor(executor)
        worker_agent.start()

        import time

        def _poll_loop():
            while worker_agent.is_running():
                worker_agent.poll_task()
                time.sleep(0.05)

        t = threading.Thread(target=_poll_loop, daemon=True)
        t.start()

        time.sleep(0.1)

        self._workers.append(worker_agent)

    def wait_for_all_workers(self, count: int, timeout: float = 30.0):
        import time
        t0 = time.time()
        registered = 0
        while time.time() - t0 < timeout:
            registered = len(self._agent.get_idle_workers())
            if registered >= count:
                return
            time.sleep(0.1)
        raise TimeoutError(f"Only {registered}/{count} workers registered after {timeout}s")

    def load_db(self, path: str):
        import os
        try:
            from storage.database import _Database
        except ImportError:
            from database import _Database
        from collections import defaultdict
        import socket

        if not os.path.isdir(path):
            raise RuntimeError(f"Path does not exist: {path}")
        if not os.path.isfile(os.path.join(path, "_DB_META")):
            raise RuntimeError(f"No _DB_META found at {path}")

        if not self._running:
            self.start()

        temp_db = _Database(path)
        meta = temp_db.load_meta()
        if not meta or not meta.db_id:
            raise RuntimeError(f"No valid _DB_META found at {path}")

        # Phase 2: Master self-recovery (no idx loading)
        temp_db._db.set_db_id(meta.db_id)
        self._agent.register_database(meta.db_id, path, "")

        # Phase 3: Assign workers by hostname
        # Group WorkerInfo by hostname -> writer_ids (includes Master's writer_id)
        hostname_to_writer_ids = defaultdict(list)
        for w in meta.workers:
            hostname_to_writer_ids[w.hostname].append(w.writer_id)

        master_hostname = socket.gethostname()

        # Check existing workers by hostname
        existing_by_hostname = defaultdict(list)
        for worker_id, hostname in self._agent.get_worker_hostnames():
            existing_by_hostname[hostname].append(worker_id)

        spawned = 0
        for hostname, writer_ids in hostname_to_writer_ids.items():
            if hostname == master_hostname:
                # Master's hostname - use existing or spawn local workers
                target_workers = existing_by_hostname.get(hostname, [])
                if not target_workers:
                    # Spawn one worker (will load all idx files)
                    self._spawn_process_worker(self._next_worker_id, {})
                    self._next_worker_id += 1
                    spawned += 1
            else:
                # Non-master hostname - need workers on that host
                target_workers = existing_by_hostname.get(hostname, [])
                if not target_workers:
                    # For now, only local process workers supported
                    WARN(f"load_db: no workers available for hostname={hostname}, "
                         f"skipping {len(writer_ids)} writer_ids")
                    continue

        if spawned > 0:
            self.wait_for_all_workers(spawned, timeout=30.0)

        # Send idx load commands to all connected workers (broadcast)
        # Only send if there are writer_ids to load
        all_writer_ids = []
        for hostname, writer_ids in hostname_to_writer_ids.items():
            all_writer_ids.extend(writer_ids)

        if all_writer_ids:
            self._agent.send_idx_load_commands(meta.db_id, path, all_writer_ids)

        # Phase 4: Rebuild remote index
        self._agent.rebuild_remote_idx(meta.db_id, path, meta.workers)

        return temp_db

    def set_worker_property(self, prop):
        WARN("set_worker_property called on Master, ignoring")

    def remove_worker_property(self, prop):
        WARN("remove_worker_property called on Master, ignoring")

    def get_worker_properties(self) -> list:
        WARN("get_worker_properties called on Master, returning empty")
        return []

    def restart_failed_tasks(self, file_path: str):
        self._agent.restart_failed_tasks(file_path)

    def _spawn_process_worker(self, worker_id: int, config: dict = None):
        import time
        from _fly_core import ex_core_get_config

        log_dir = ex_core_get_config().get_str("log_dir")
        fly_bin = self._find_fly_binary()

        os.makedirs(log_dir, exist_ok=True)
        log_path = os.path.join(log_dir, f"worker{worker_id}.log")

        attrs = config.get("attributes", []) if config and isinstance(config, dict) else []
        attrs_str = ",".join(attrs) if attrs else ""

        cmd = [
            fly_bin,
            "--worker",
            "--worker-id", str(worker_id),
            "--master-host", self._host,
            "--master-port", str(self._port),
            "--log-dir", log_dir,
        ]
        if attrs_str:
            cmd.extend(["--worker-attributes", attrs_str])

        log_file = open(log_path, "a")
        proc = subprocess.Popen(cmd, stdin=subprocess.DEVNULL,
                                stdout=log_file, stderr=log_file)
        self._worker_procs.append(proc)
        time.sleep(0.1)

        DBG(
            f"Spawned worker process: pid={proc.pid}, "
            f"worker_id={worker_id}, attributes={attrs}")

    @staticmethod
    def _find_fly_binary() -> str:
        import shutil
        fly_on_path = shutil.which("fly")
        if fly_on_path:
            return fly_on_path

        import _fly_agent
        agent_dir = os.path.dirname(os.path.abspath(_fly_agent.__file__))
        # agent_dir = build/python/agent/ or bazel-bin/src/agent/export/
        # fly binary = build/bin/fly or bazel-bin/src/main/cpp/fly
        build_dir = os.path.dirname(os.path.dirname(agent_dir))
        candidate = os.path.join(build_dir, "bin", "fly")
        if os.path.isfile(candidate) and os.access(candidate, os.X_OK):
            return candidate

        # Fallback: old bazel-bin layout
        candidate = os.path.join(build_dir, "src", "main", "cpp", "fly")
        if os.path.isfile(candidate) and os.access(candidate, os.X_OK):
            return candidate

        raise RuntimeError(
            f"Cannot find fly binary. Searched: PATH, {candidate}")


class Worker(FlyAgent):

    @property
    def mode(self) -> str:
        return "worker"

    def __init__(self, worker_id: int, master_host: str, master_port: int,
                 attributes: list = None):
        self._agent = EXAgentWorker(worker_id, master_host, master_port,
                                    attributes or [])
        self._agent.set_data_service(ex_stg_get_data_service())
        self._db_cache = {}
        self._db_path_pending = {}
        self._master_host = master_host
        self._master_port = master_port
        self._worker_id = worker_id
        self._executor = None

    def start(self):
        self._executor = EXTaskExecutor()
        self._executor.set_exec_func(create_executor(self))
        self._agent.set_executor(self._executor)
        self._agent.start()

    def submit(self, name: str, module: str, args: list,
               inputs: list = None,
               required_capabilities: list = None) -> int:
        return self._agent.submit_task(name, module, args, inputs or [],
                                       required_capabilities or [])

    def get_database(self, db_id: str):
        if db_id not in self._db_cache:
            raise RuntimeError(
                f"Unknown db_id: {db_id}, need master info (Phase 3)")
        return self._db_cache[db_id]

    def stop(self):
        if self._executor is not None:
            self._executor.clear_exec_func()
            self._executor = None
        self._db_cache.clear()
        self._agent.stop()
        self._agent = None

    def set_worker_property(self, prop):
        props = self._ensure_list(prop)
        if props:
            self._agent.set_worker_property(props)

    def remove_worker_property(self, prop):
        props = self._ensure_list(prop)
        if props:
            self._agent.remove_worker_property(props)

    def get_worker_properties(self) -> list:
        return list(self._agent.get_worker_properties())

    def restart_failed_tasks(self, file_path: str):
        WARN("restart_failed_tasks called on Worker, ignoring")


__all__ = ['FlyAgent', 'Master', 'Worker']
