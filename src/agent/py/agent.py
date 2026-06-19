import os
import sys
import threading
import subprocess
from abc import ABC, abstractmethod

from _fly_agent import EXAgentMaster, EXAgentWorker, EXTaskExecutor, EXTaskExecStatus
from _fly_log import DBG, INFO, WARN, ERR 

from .executor import create_executor


class FlyAgent(ABC):

    @property
    @abstractmethod
    def mode(self) -> str:
        raise NotImplementedError

    @abstractmethod
    def submit(self, name: str, module: str, args: list,
               inputs: list = None,
               write_context_hash: str = "") -> int:
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

    def put_cache(self, key: str, value):
        """Store a Python object in the local agent cache.

        The cache lives for the lifetime of the agent process and is not
        shared across workers.  Use it to pass data between tasks on the
        same worker without network/disk I/O.

        Args:
            key: String key for the cached value.
            value: Any Python object.
        """
        self._cache[key] = value

    def get_cache(self, key: str, default=None):
        """Retrieve a cached Python object by key.

        Args:
            key: String key that was used with :meth:`put_cache`.
            default: Value to return if *key* is not found.

        Returns:
            The cached Python object, or *default* if not found.
        """
        return self._cache.get(key, default)

    def has_cache(self, key: str) -> bool:
        """Return ``True`` if *key* exists in the local agent cache."""
        return key in self._cache

    def remove_cache(self, key: str):
        """Remove a single entry from the local agent cache.

        Raises:
            KeyError: If *key* is not in the cache.
        """
        del self._cache[key]

    def clear_cache(self):
        """Remove all entries from the local agent cache."""
        self._cache.clear()


class Master(FlyAgent):

    @property
    def mode(self) -> str:
        return "master"

    def __init__(self, host: str = "127.0.0.1", port: int = 0):
        self._agent = EXAgentMaster(host, port)
        self._task_counter = 0
        self._lock = threading.Lock()
        self._worker_procs = []
        self._host = host
        self._port = port
        self._running = False
        self._next_worker_id = 1
        self._expected_workers = 0
        self._cache = {}
        self._shared_config_path = None

    def is_running(self) -> bool:
        return self._running

    def get_worker_pids(self) -> list:
        return [proc.pid for proc in self._worker_procs if proc.poll() is None]

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
               required_capabilities: list = None,
               write_context_hash: str = "") -> int:
        with self._lock:
            self._task_counter += 1
            task_id = self._task_counter

        if not self._running:
            self.start()

        self._agent.submit_task_with_requirements(
            task_id, name, module, args, inputs or [], [],
            required_capabilities or [], write_context_hash)
        DBG(f"Task submitted: id={task_id}, name={name}, requires={required_capabilities}")
        return task_id

    def launch_local_workers(self, worker_configs: list, port: int = None):
        if port is not None:
            self._port = port

        self.start()
        self._port = self._agent.get_port()

        num_workers = len(worker_configs)
        self._expected_workers += num_workers
        for i in range(num_workers):
            config = worker_configs[i]
            wid = self._next_worker_id
            self._next_worker_id += 1
            self._spawn_process_worker(wid, config)

        DBG(
            f"Master running on {self._host}:{self._port}, "
            f"{num_workers} workers launched")

    def stop(self):
        # First stop the C++ Master agent so it sends ShutdownMessage to Workers.
        # Workers need graceful exit to flush gcov coverage data.
        if self._running:
            self._agent.stop()
            self._running = False

        self._cache.clear()
        self._shared_config_path = None

        # Wait for Workers to exit gracefully (they received ShutdownMessage).
        # This ensures atexit/__gcov_exit runs in Worker processes.
        for proc in self._worker_procs:
            if proc.poll() is None:
                try:
                    proc.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    proc.terminate()
                    try:
                        proc.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        proc.kill()
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

    @property
    def worker_count(self) -> int:
        return self._agent.get_connection_count()

    def wait_for_workers(self, count: int = None, timeout: float = 30.0) -> bool:
        import time
        if count is None:
            count = self._expected_workers
        t0 = time.time()
        while time.time() - t0 < timeout:
            if self._agent.get_connection_count() >= count:
                return True
            time.sleep(0.1)
        return False

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

    def wait_for_all_workers(self, count: int = None, timeout: float = 30.0):
        import time
        if count is None:
            count = self._expected_workers
        if count <= 0:
            return
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

        # Phase 1: Master self-recovery — register db paths, no idx loading
        temp_db._db.set_db_id(meta.db_id)
        self._agent.register_database(meta.db_id, path, "")

        # Phase 2: Assign workers by hostname
        # Group WorkerInfo by hostname -> writer_ids
        hostname_to_writer_ids = defaultdict(list)
        for w in meta.workers:
            hostname_to_writer_ids[w.hostname].append(w.writer_id)

        # Check existing workers by hostname (worker_id, hostname)
        existing_by_hostname = defaultdict(list)
        for worker_id, hostname in self._agent.get_worker_hostnames():
            existing_by_hostname[hostname].append(worker_id)

        # Ensure at least one worker per hostname from meta
        spawned = 0
        for hostname in hostname_to_writer_ids:
            if not existing_by_hostname.get(hostname):
                # No worker on this hostname — spawn one with matching host
                self._spawn_process_worker(self._next_worker_id, {"host": hostname})
                self._next_worker_id += 1
                spawned += 1

        if spawned > 0:
            self._expected_workers += spawned
            self.wait_for_all_workers(timeout=30.0)

            # Refresh mapping after new workers connect
            existing_by_hostname = defaultdict(list)
            for worker_id, hostname in self._agent.get_worker_hostnames():
                existing_by_hostname[hostname].append(worker_id)

        # Phase 3: Send targeted idx load commands
        # Each worker receives ONLY the writer_ids belonging to its hostname
        for hostname, writer_ids in hostname_to_writer_ids.items():
            workers = existing_by_hostname.get(hostname, [])
            if not workers:
                WARN(f"load_db: no workers for hostname={hostname}, "
                     f"skipping {len(writer_ids)} writer_ids")
                continue
            # Use first available worker on this hostname
            worker_id = workers[0]
            self._agent.send_idx_load_to_worker(meta.db_id, path, writer_ids, worker_id)
            INFO(f"load_db: sent {len(writer_ids)} writer_ids to worker {worker_id} on host {hostname}")

        # Phase 4: Wait for all acks (on_idx_load_ack handles remote_idx rebuild)
        # Workers send IdxLoadAck after loading, master processes each ack
        # to rebuild remote_idx. Wait a reasonable time for async processing.
        import time
        time.sleep(1.0)

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

        cfg = ex_core_get_config()
        log_dir = cfg.get_str("log_dir")
        fly_bin = self._find_fly_binary()

        os.makedirs(log_dir, exist_ok=True)
        log_path = os.path.join(log_dir, f"worker{worker_id}.log")

        # Config is shared and immutable after worker startup — only write once.
        if not hasattr(self, '_shared_config_path') or not self._shared_config_path:
            self._shared_config_path = os.path.join(log_dir, ".fly_config")
            cfg.save_to_file(self._shared_config_path)
        config_path = self._shared_config_path

        attrs = config.get("attributes", []) if config and isinstance(config, dict) else []
        attrs_str = ",".join(attrs) if attrs else ""

        cmd = [
            fly_bin,
            "--worker",
            "--worker-id", str(worker_id),
            "--master-host", self._host,
            "--master-port", str(self._port),
            "--log-dir", log_dir,
            "--config-file", config_path,
        ]
        if config and isinstance(config, dict) and config.get("host"):
            cmd.extend(["--host", config["host"]])
        if attrs_str:
            cmd.extend(["--worker-attributes", attrs_str])

        env = os.environ.copy()
        gcov_prefix = os.environ.get("GCOV_PREFIX", "")
        if gcov_prefix:
            worker_cov_dir = gcov_prefix + f"/worker_{worker_id}"
            os.makedirs(worker_cov_dir, exist_ok=True)
            env["GCOV_PREFIX"] = worker_cov_dir

        # Python coverage: per-worker data file so parallel writes don't conflict
        if os.environ.get("FLY_PYCOVERAGE"):
            env["FLY_PYCOVERAGE"] = "1"
            env["FLY_PYCOVERAGE_DATA"] = f"/tmp/.coverage.fly.worker_{worker_id}"
            if os.environ.get("FLY_PYCOVERAGE_RCFILE"):
                env["FLY_PYCOVERAGE_RCFILE"] = os.environ["FLY_PYCOVERAGE_RCFILE"]

        log_file = open(log_path, "a")
        proc = subprocess.Popen(cmd, stdin=subprocess.DEVNULL,
                                stdout=log_file, stderr=log_file,
                                env=env)
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
        self._db_cache = {}
        self._db_path_pending = {}
        self._cache = {}
        self._master_host = master_host
        self._master_port = master_port
        self._worker_id = worker_id
        self._executor = None
        self._worker_procs = []

    def start(self):
        self._executor = EXTaskExecutor()
        self._executor.set_exec_func(create_executor(self))
        self._agent.set_executor(self._executor)
        self._agent.start()

    def submit(self, name: str, module: str, args: list,
               inputs: list = None,
               required_capabilities: list = None,
               write_context_hash: str = "") -> int:
        return self._agent.submit_task(name, module, args, inputs or [],
                                       required_capabilities or [],
                                       write_context_hash)

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
        self._cache.clear()

        if self._agent is not None:
            self._agent.stop()
            self._agent = None

    def is_running(self) -> bool:
        if self._agent is None:
            return False
        return self._agent.is_running()

    def poll_task(self) -> bool:
        if self._agent is None:
            return False
        return self._agent.poll_task()

    def poll_task_blocking(self, timeout_ms: int = 100) -> bool:
        if self._agent is None:
            return False
        return self._agent.poll_task_blocking(timeout_ms)

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
