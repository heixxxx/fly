import os
import sys
import socket
import threading
import subprocess
from abc import ABC, abstractmethod

from _fly_agent import EXAgentMaster, EXAgentWorker, EXTaskExecutor, EXTaskExecStatus
from _fly_log import DBG, INFO, WARN, ERR

from .executor import create_executor

# message 系统：业务代码必须用 fly.* 公开包装，禁止直接用 _fly_message 底层绑定。
# （见 docs/message-system.md §6.4「禁止直接使用底层接口」）
from fly import register_message_id, message

# 注册 storage domain 的流程性 message id（模块加载时注册）。
# STOR::0002: merge_db 完成；STOR::0003: load_db 恢复完成。
register_message_id("STOR::0002", "INFO")
register_message_id("STOR::0003", "INFO")


class FlyAgent(ABC):

    @property
    @abstractmethod
    def mode(self) -> str:
        raise NotImplementedError

    @abstractmethod
    def submit(self, name: str, module: str, args: list,
               inputs: list = None,
               write_context_hash: str = "",
               priority: int = 10) -> int:
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
               attribute_timeout: float = -1.0,
               write_context_hash: str = "",
               vars: list = None,
               priority: int = 10) -> int:
        with self._lock:
            self._task_counter += 1
            task_id = self._task_counter

        if not self._running:
            self.start()

        self._agent.submit_task_with_requirements(
            task_id, name, module, args, inputs or [], [],
            required_capabilities or [], attribute_timeout, write_context_hash,
            vars or [], priority)
        DBG(f"Task submitted: id={task_id}, name={name}, "
            f"requires={required_capabilities}, attr_timeout={attribute_timeout}, "
            f"vars={vars}")
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

        # 静态读 _DB_META（不构造 Database，避免与 register_database 建的权威 Database
        # 共享 DataService::db_paths_ 导致析构竞争 erase）。
        meta = _Database.load_meta_from_path(path)
        # db_path 废弃：_DB_META 的 db_path 字段可能过期（搬目录），不再用它作 db_path。
        # 用 created_at > 0 判断 _DB_META 是否有效（corrupt/空文件时 created_at == 0）。
        if not meta or meta.created_at <= 0:
            raise RuntimeError(f"No valid _DB_META found at {path}")

        # db_path 废弃：db_path == db_path（即 path）。不用 meta.db_path（旧 _DB_META 存的可能是
        # 搬目录前的旧 path）。用当前 path 作 db_path，确保与 Database 构造一致。
        db_path = path

        # Phase 1: Master self-recovery — register db paths, no idx loading.
        # register_database 内部构造权威 Database 插入 db_instances_（路径唯一权威源）。
        self._agent.register_database(path, "")

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
            self._agent.send_idx_load_to_worker(db_path, writer_ids, worker_id)
            INFO(f"load_db: sent {len(writer_ids)} writer_ids to worker {worker_id} on host {hostname}")

        # Phase 4: Wait for all acks (on_idx_load_ack handles remote_idx rebuild)
        # Workers send IdxLoadAck after loading, master processes each ack
        # to rebuild remote_idx. Wait a reasonable time for async processing.
        import time
        time.sleep(1.0)

        # 流程 message：load_db 恢复完成（系统就绪里程碑）。
        message("STOR::0003", 1, f"load_db done: path={path}")
        # 返回权威 Database 句柄：直接复用 db_instances_ 里的对象（register_database 已建），
        # 不再单独构造临时 Database（避免析构 unregister DataService::db_paths_ 的竞争）。
        db = _Database.__new__(_Database)
        db._db = self._agent.get_database(db_path)
        return db

    def merge_db(self, path: str, data_path: str = "", merge_db_path: str = "",
                 local_workers: int = 4, delete_source: bool = True):
        """Merge a frozen database's data onto the master host.

        把分散在各源 host 本地 data_path 的 .dat 数据通过网络集中到 master host，
        产出一个 data 自包含、索引沿用共享 db_path 的合并数据库。

        **阻塞调用**：本方法在返回前会完成全部 merge 工作（等待已有 task → 派发 merge task
        → 等待完成 → 删源 → 状态清理）。调用方（用户脚本）在 merge 完成前不会继续执行后续代码。

        **前置等待**：若调用时仍有 pending/running task，会先等待它们全部完成，保证 merge
        期间数据分布稳定。

        详见 docs/db-merge-design.md。

        Args:
            path: 源 db 的 db_path（共享存储，必须已 freeze）。
            data_path: 产物 data_path（master host 本地）。默认 path + ".merged_data"。
            db_path: 产物 db_path。默认空=复用源 path（idx/_DB_META 在共享盘，零搬迁）。
            local_workers: 仅当 master host **无**同 host worker 时拉起的 worker 数上限；
                已存在则不补齐，使用现有 worker 数作为并发度。
            delete_source: merge 全部成功后是否自动删源各 host 的原 .dat。

        Returns:
            合并后的 _Database 句柄。
        """
        import os
        import time
        from collections import defaultdict
        try:
            from storage.database import _Database
        except ImportError:
            from database import _Database

        # ── Phase 1: 校验 + 读源 meta ──────────────────────────────────
        if not os.path.isdir(path):
            raise RuntimeError(f"merge_db: path does not exist: {path}")
        if not os.path.isfile(os.path.join(path, "_FROZEN")):
            raise RuntimeError(
                f"merge_db: source db not frozen (no _FROZEN marker at {path}); "
                "call db.freeze() first")

        if not os.path.isfile(os.path.join(path, "_DB_META")):
            raise RuntimeError(f"merge_db: no _DB_META found at {path}")

        if not self._running:
            self.start()

        # 限制 1：merge 开始前，必须等待所有 pending/running task 完成。
        # 保证 merge 期间数据分布稳定（freeze 已禁止该 db 的写入，但其他 db 的 task
        # 可能仍在运行，其完成会改变 master/worker 状态）。merge 派发的 __merge_object
        # task 在此之后才提交，不会被本等待误拦。
        if self._agent.get_pending_tasks() or self._agent.get_running_tasks():
            INFO("merge_db: waiting for pending/running tasks to complete before merge")
            self.wait_for_all_tasks(timeout=3600)

        # 静态读 _DB_META（不构造 Database，避免在已 open_db 的进程内重复 register db_path）。
        meta = _Database.load_meta_from_path(path)
        if not meta:
            raise RuntimeError(f"merge_db: invalid _DB_META at {path}")
        # db_path == 源 path（db 唯一标识）。merge_db_path 是产物路径（用户可覆盖）。
        db_path = path

        merge_db_path = merge_db_path if merge_db_path else path
        merge_data_path = data_path if data_path else (path + ".merged_data")
        os.makedirs(merge_data_path, exist_ok=True)

        # 按源 hostname 分组 writer_ids（一个 writer 属于一个 host）。
        # _DB_META 的 WorkerInfo 是权威的 hostname 映射，但不一定覆盖全部 idx 文件
        # （master 进程自写 _DB_META header 时的 writer_id 等）。所以以磁盘 idx 文件为全集，
        # hostname 从 _DB_META 查，缺失的归到 source_hosts 第一个（避免漏删）。
        import glob
        writer_to_hostname = {}
        for w in meta.workers:
            writer_to_hostname[w.writer_id] = w.hostname

        hostname_to_writer_ids = defaultdict(list)
        # idx 文件在源 db_path（共享盘）。跨 path merge 时 merge_db_path 是产物新路径，
        # 但 idx 还在源 path（db_path == 源 path）。从源 path 读 idx。
        source_idx_path = db_path  # db_path == 源 db_path
        idx_files = glob.glob(os.path.join(source_idx_path, "*.idx"))
        for idx_file in idx_files:
            writer_id = os.path.basename(idx_file)[:-4]  # 去掉 .idx
            hostname = writer_to_hostname.get(writer_id)
            if hostname is None:
                # idx 文件不在 _DB_META 中：归到第一个已知 source host，或默认 host。
                hostname = (meta.workers[0].hostname if meta.workers else "unknown")
            hostname_to_writer_ids[hostname].append(writer_id)
        source_hosts = list(hostname_to_writer_ids.keys())
        INFO(f"merge_db: db_path={db_path}, source_hosts={source_hosts}, "
             f"target_data_path={merge_data_path}, "
             f"idx_files={len(idx_files)}, meta_workers={len(meta.workers)}")

        # ── Phase 2: 确保目标 worker 池（master host）+ 源 host worker 就位 ──
        existing_by_hostname = defaultdict(list)
        for worker_id, hostname in self._agent.get_worker_hostnames():
            existing_by_hostname[hostname].append(worker_id)

        # 确保每个源 host 有在线 worker（用于被跨机读 + 接收删源命令）。
        spawned_source = 0
        for hostname in source_hosts:
            if not existing_by_hostname.get(hostname):
                self._spawn_process_worker(self._next_worker_id, {"host": hostname})
                self._next_worker_id += 1
                spawned_source += 1
        if spawned_source > 0:
            self._expected_workers += spawned_source
            self.wait_for_all_workers(timeout=30.0)
            existing_by_hostname = defaultdict(list)
            for worker_id, hostname in self._agent.get_worker_hostnames():
                existing_by_hostname[hostname].append(worker_id)

        # 确保 master host 有 target worker（不传 host 的 local worker，与 master 同机）。
        master_hostname = socket.gethostname()
        master_host_workers = existing_by_hostname.get(master_hostname, [])
        spawned_local = 0
        if not master_host_workers:
            for _ in range(max(1, local_workers)):
                self._spawn_process_worker(self._next_worker_id, {})
                self._next_worker_id += 1
                spawned_local += 1
            self._expected_workers += spawned_local
            self.wait_for_all_workers(timeout=30.0)
            existing_by_hostname = defaultdict(list)
            for worker_id, hostname in self._agent.get_worker_hostnames():
                existing_by_hostname[hostname].append(worker_id)
            master_host_workers = existing_by_hostname.get(master_hostname, [])
        if not master_host_workers:
            raise RuntimeError(
                f"merge_db: no target workers on master host '{master_hostname}'")

        INFO(f"merge_db: target worker pool (master host) = {master_host_workers}")

        # ── Phase 3: master 从共享 db_path 读全部 idx（按 writer 分组对象清单）──
        # 用 read_idx_entries（轻量读，不灌 master local_idx / 不 mark_data_ready），
        # 避免"先污染再清理"绕路（restore_master_idx 会副作用地建立 master local 视图，
        # 但 master 不持 .dat，这些 entry 无效，需 cleanup 兜底清理）。
        writer_to_entries = {}
        for hostname, writer_ids in hostname_to_writer_ids.items():
            for writer_id in writer_ids:
                entries = self._agent.read_idx_entries(source_idx_path, writer_id)
                if entries:
                    writer_to_entries[writer_id] = (hostname, entries)

        # ── Phase 4: 派发 __merge_object tasks（按源 host 分配 target worker）──
        # 设计 §5.3：每源 host 固定派给 master host 一个 target worker（轮转分配）。
        host_to_target = {}
        for i, hostname in enumerate(source_hosts):
            host_to_target[hostname] = master_host_workers[i % len(master_host_workers)]

        all_task_ids = []
        task_count = 0
        for writer_id, (hostname, entries) in writer_to_entries.items():
            target_worker = host_to_target[hostname]
            for entry in entries:
                # entry.object_name 是 short_name（LocalIndex 不再存 db_path 前缀，阶段1 改造）
                short_name = entry.object_name
                # send_merge_task: source_db_path 拉源用，target_db_path 落盘/上报用。
                task_id = self._agent.send_merge_task(
                    target_worker, short_name, db_path, merge_db_path,
                    merge_data_path, hostname)
                all_task_ids.append(task_id)
                task_count += 1

        INFO(f"merge_db: dispatched {task_count} merge tasks across "
             f"{len(master_host_workers)} target workers")

        # 等待全部完成（"全部成功才删源"语义）。
        ok, completed, failed = self._agent.wait_merge_tasks_complete(
            all_task_ids, 3600)  # 1h timeout for large db
        if ok:
            INFO(f"merge_db: all {len(completed)} objects merged successfully")
        else:
            WARN(f"merge_db: {len(failed)} tasks failed (not deleting source). "
                 f"First failure: {failed[0] if failed else 'unknown'}")

        # ── Phase 5: 全部成功 → 统一删源 + 状态清理 ──────────────────────
        source_worker_ids = []
        if ok and delete_source:
            for hostname, writer_ids in hostname_to_writer_ids.items():
                host_workers = existing_by_hostname.get(hostname, [])
                if not host_workers:
                    WARN(f"merge_db: no worker on source host '{hostname}' to delete, "
                         f"skipping {len(writer_ids)} writer_ids")
                    continue
                source_worker = host_workers[0]
                source_worker_ids.append(source_worker)
                # data_path 传空 → C++ send_delete_data 从 db_registry 查源 data_path
                # （此时 cleanup 未执行，db_registry 仍是源的）。
                self._agent.send_delete_data(
                    source_worker, db_path, "", writer_ids)
                INFO(f"merge_db: sent DeleteData to worker {source_worker} on host "
                     f"'{hostname}' for {len(writer_ids)} writers")
            # 同步等待全部 DeleteDataAck（替换原先的 sleep 兜底，消除 flaky + 内存泄漏）。
            if source_worker_ids:
                del_ok, del_failed = self._agent.wait_delete_data_acks(
                    source_worker_ids, db_path, 60)
                if del_ok:
                    INFO(f"merge_db: all source deletes confirmed")
                else:
                    WARN(f"merge_db: {len(del_failed)} source deletes failed/timed out "
                         f"(workers={del_failed}), source .dat may remain")

        # 状态清理（无论是否删源，merge 已改变数据分布，旧索引都失效）：
        # 广播 MergeCleanup 让各 worker 清旧 local_idx/remote_idx + 按新路径重建 local_idx；
        # master 自身清旧索引 + 重建 remote_idx（指向 merge target）+ 更新 db_registry。
        # 删源 ack 已保证 worker 在线且响应过，紧随其后的广播时序确定（无需额外 sleep）。
        if ok:
            self._agent.cleanup_after_merge(
                db_path, completed, source_worker_ids, master_host_workers,
                merge_db_path, merge_data_path)
            INFO("merge_db: cleanup_after_merge done (broadcast + master state rebuilt)")

        # 产物 db 句柄：复用 cleanup_after_merge 在 db_instances_ 建好的权威 Database
        # （用源 db_path，保持 object_name = db_path:short 一致）。不再单独构造临时 Database，
        # 避免其析构 unregister DataService::db_paths_ 的竞争。
        # read_object 走 master remote_idx（merge task 已登记对象位置到 merge worker）。
        merged_db = _Database.__new__(_Database)
        merged_db._db = self._agent.get_database(db_path)
        INFO(f"merge_db: done, ok={ok}, merged_data at {merge_data_path}")
        # 流程 message：merge_db 完成（跨机数据集中里程碑）。
        message("STOR::0002", 1,
                f"merge_db done: db_path={db_path}, objects={len(completed)}, "
                f"data_path={merge_data_path}")
        return merged_db

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

        # C++ coverage (gcov) gcda relocation.
        #
        # Two modes, selected by whether GCOV_PREFIX_STRIP is set:
        #
        #  - New mode (GCOV_PREFIX_STRIP set, used by tools/measure_coverage.sh):
        #    The parent already pointed GCOV_PREFIX at the execroot with
        #    GCOV_PREFIX_STRIP=3 so gcda land where lcov scans, regardless of
        #    cwd.  Workers must inherit these EXACT values — overriding
        #    GCOV_PREFIX here would send worker gcda to execroot/worker_N/...
        #    and lcov would miss them again (the §12.2 flaw).  QA runs serially
        #    (-j 1) in this mode, so concurrent gcda writes are not a concern.
        #
        #  - Legacy mode (GCOV_PREFIX set but no STRIP): isolate each worker
        #    under GCOV_PREFIX/worker_N/ as before.
        gcov_prefix = os.environ.get("GCOV_PREFIX", "")
        gcov_strip = os.environ.get("GCOV_PREFIX_STRIP", "")
        if gcov_prefix and not gcov_strip:
            worker_cov_dir = gcov_prefix + f"/worker_{worker_id}"
            os.makedirs(worker_cov_dir, exist_ok=True)
            env["GCOV_PREFIX"] = worker_cov_dir

        # Python coverage: started at interpreter boot via sitecustomize.py
        # (see docs/coverage-testing.md §12.1). Workers inherit FLY_PYCOVERAGE
        # and start their own coverage automatically — no per-worker data file
        # needed; parallel mode in .coveragerc gives each process its own file.
        if os.environ.get("FLY_PYCOVERAGE"):
            env["FLY_PYCOVERAGE"] = "1"

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
               attribute_timeout: float = -1.0,
               write_context_hash: str = "",
               vars: list = None,
               priority: int = 10) -> int:
        return self._agent.submit_task(name, module, args, inputs or [],
                                       required_capabilities or [],
                                       attribute_timeout,
                                       write_context_hash,
                                       vars or [], priority)

    def get_database(self, db_path: str):
        if db_path not in self._db_cache:
            raise RuntimeError(
                f"Unknown db_path: {db_path}, need master info (Phase 3)")
        return self._db_cache[db_path]

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
