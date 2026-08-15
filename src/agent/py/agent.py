import os
import sys
import socket
import threading
import subprocess
from abc import ABC, abstractmethod

from _fly_agent import EXAgentMaster, EXAgentWorker, EXTaskExecutor, EXTaskExecStatus
from _fly_log import DBG, INFO, WARN, ERR

from storage import Database, DbChainFile, make_chain
from storage import get_registry

from .executor import create_executor

# message 系统：业务代码必须用 fly.* 公开包装，禁止直接用 _fly_message 底层绑定。
# （见 docs/message-system.md §6.4「禁止直接使用底层接口」）
from fly import register_message_id, message, wait_obj as _wait_obj

# 注册 storage domain 的流程性 message id（模块加载时注册）。
# STOR::0002: merge_db 完成；STOR::0003: load_db 恢复完成。
register_message_id("STOR::0002", "INFO")
register_message_id("STOR::0003", "INFO")
# STOR::0004: merge_db 删源失败（重试后）——提醒用户手动删除残留源 .dat。
register_message_id("STOR::0004", "ERROR")


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
        import time as _t
        import sys as _sys
        import signal as _sig
        _t0 = _t.monotonic()
        def _log(msg):
            _sys.stderr.write(f"[Master.stop] +{_t.monotonic()-_t0:.3f}s {msg}\n"); _sys.stderr.flush()

        # First stop the C++ Master agent so it sends ShutdownMessage to Workers.
        # Workers need graceful exit to flush gcov coverage data.
        if self._running:
            self._agent.stop()
            self._running = False
            _log("C++ stop done")

        self._cache.clear()
        self._shared_config_path = None

        # Wait for Workers to exit gracefully (they received ShutdownMessage).
        # 并行等待所有 worker（共享一个 5s deadline，避免串行累积）。
        import time as _time
        deadline = _time.monotonic() + 5.0
        pending = [p for p in self._worker_procs if p.poll() is None]
        while pending and _time.monotonic() < deadline:
            pending = [p for p in pending if p.poll() is None]
            if pending:
                _time.sleep(0.05)
        # 超时的 worker: 发 SIGUSR1 触发 stack dump 然后快速退出
        for p in pending:
            if p.poll() is None:
                _log(f"worker pid={p.pid} timeout, sending SIGUSR1 (dump+exit)")
                try:
                    p.send_signal(_sig.SIGUSR1)
                except Exception:
                    p.terminate()
        # 等 SIGUSR1 处理完（1s 足够写文件 + os._exit）
        deadline2 = _time.monotonic() + 1.0
        pending2 = [p for p in pending if p.poll() is None]
        while pending2 and _time.monotonic() < deadline2:
            pending2 = [p for p in pending2 if p.poll() is None]
            if pending2:
                _time.sleep(0.05)
        for p in pending2:
            if p.poll() is None:
                _log(f"worker pid={p.pid} still alive after SIGUSR1, killing")
                p.kill()
        _log(f"all workers done (timed_out={len(pending)} killed={len(pending2)})")
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

    def wait_workers_registered(self, timeout: float = None) -> bool:
        """等所有尝试唤起的 worker（占位符）完成注册。

        bsub（LSF）等慢调度场景：唤起请求发出后 worker 可能分钟级才真正启动，
        不假设任何注册时限。timeout=None 时取 config 'worker_register_timeout'
        （默认 0 = 无限等待，等待期间每 30s 打 INFO 进度）。
        """
        import time
        from _fly_core import ex_core_get_config
        if timeout is None:
            cfg_timeout = ex_core_get_config().get_int("worker_register_timeout") or 0
            timeout = float(cfg_timeout) if cfg_timeout > 0 else float("inf")
        t0 = time.time()
        last_report = t0
        while True:
            if self._agent.all_workers_registered():
                return True
            now = time.time()
            if now - t0 >= timeout:
                return False
            if now - last_report >= 30.0:
                INFO(f"wait_workers_registered: still waiting for "
                     f"{self._agent.get_expected_worker_count()} worker(s) to "
                     f"register ({now - t0:.0f}s elapsed)")
                last_report = now
            time.sleep(0.1)

    def expect_workers(self, worker_ids):
        """手动登记唤起占位符（外部唤起场景，如 bsub/LSF 调度脚本）。

        launch_local_workers 会自动登记；此 API 供用户用自己的 launcher 起
        `fly --worker` 时登记，使 wait_workers_registered 能等待它们注册。
        """
        for wid in worker_ids:
            self._agent.expect_worker(int(wid))

    def _wait_spawned_workers(self):
        """补 spawn worker 后的统一等待：先等注册（不假设时限，config 控制），
        再等 IDLE（原有语义，timeout 放宽到 max(30, config)）。
        注册等待超时（仅 config>0 时可能）抛 TimeoutError，与原行为一致。"""
        from _fly_core import ex_core_get_config
        cfg_timeout = ex_core_get_config().get_int("worker_register_timeout") or 0
        if not self.wait_workers_registered():
            pending = self._agent.get_expected_worker_count()
            raise TimeoutError(
                f"{pending} worker(s) failed to register within {cfg_timeout}s")
        idle_timeout = max(30.0, float(cfg_timeout)) if cfg_timeout > 0 else 30.0
        self.wait_for_all_workers(timeout=idle_timeout)

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
        """等 worker 注册并 IDLE。注：按可调度（idle）worker 计数——storage_only
        角色的 worker 不在调度候选（role 语义），不计入本计数。"""
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
        from storage import Database
        from collections import defaultdict

        if not os.path.isdir(path):
            raise RuntimeError(f"Path does not exist: {path}")
        if not os.path.isfile(os.path.join(path, "_DB_META")):
            raise RuntimeError(f"No _DB_META found at {path}")

        if not self._running:
            self.start()

        # 静态读 _DB_META（不构造 Database，避免与 register_database 建的权威 Database
        # 共享 DataService::db_paths_ 导致析构竞争 erase）。
        meta = Database.load_meta_from_path(path)
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
            self._wait_spawned_workers()

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
        db = Database.__new__(Database)
        db._db = self._agent.get_database(db_path)
        # 恢复 _DB_CHAIN 链信息（uid/role/logical_name）+ 注册 uid→path 映射
        db._chain_file = DbChainFile(db_path)
        db._chain_uid = None
        db._chain_role = None
        db._chain_logical_name = None
        db._load_chain_info()
        # next 自愈：检查并补齐缺失的 next 边（建链 crash 自愈）
        db._heal_next_edges()
        return db

    def merge_db(self, path: str, data_path: str = "", merge_db_path: str = "",
                 task_timeout: float = 3600.0,
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
            合并后的 Database 句柄。
        """
        import os
        import time
        from collections import defaultdict
        from storage import Database

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
        meta = Database.load_meta_from_path(path)
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
            self._wait_spawned_workers()
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
            self._wait_spawned_workers()
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

        # 空 idx 防误删源：清单全空有两种成因，必须区分——
        #   真空 db（目录下无 .idx 文件）：允许空合并（删源无损失）；
        #   idx 存在但读出 0 条目（idx 损坏/读取失败被误当真空）：拒绝 merge——
        #   否则 0 个 task 全部"成功"走"全成功删源"，源 .dat 全删而产物为空
        #   = 数据丢失（plan 评审确认纳入的数据丢失级风险）。
        if not writer_to_entries:
            idx_files = ([f for f in os.listdir(source_idx_path)
                          if f.endswith(".idx")]
                         if os.path.isdir(source_idx_path) else [])
            if idx_files:
                raise RuntimeError(
                    f"merge_db aborted: {len(idx_files)} idx file(s) exist under "
                    f"{source_idx_path} but no entries were read (possible idx "
                    f"corruption) — refusing to merge and delete source. db={db_path}")
            INFO("merge_db: empty db (no idx files, no entries) — trivial empty merge")

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

        # 等待全部完成（"全部成功才删源"语义）。task_timeout 可调（QA 注错入口）。
        ok, completed, failed = self._agent.wait_merge_tasks_complete(
            all_task_ids, int(task_timeout))
        if ok:
            INFO(f"merge_db: all {len(completed)} objects merged successfully")
        else:
            WARN(f"merge_db: {len(failed)} tasks failed (not deleting source). "
                 f"First failure: {failed[0] if failed else 'unknown'}")
            # 失败清理：源数据全保留（支撑重 merge）；按 db 精确清 master merge task
            # 状态 + 广播 purge（merge target worker 删自己写的产物 .dat/.idx）。
            self._agent.cleanup_failed_merge(db_path, merge_db_path, merge_data_path)
            raise RuntimeError(
                f"merge_db failed: {len(failed)} object(s) failed to merge "
                f"(source data preserved for re-merge). First failure: "
                f"{failed[0] if failed else 'unknown'}")

        # ── Phase 5: 全部成功 → 统一删源 + 状态清理 ──────────────────────
        source_worker_ids = []
        worker_to_writers = {}  # 重试用：失败 worker → 其 writer_ids
        if ok and delete_source:
            for hostname, writer_ids in hostname_to_writer_ids.items():
                host_workers = existing_by_hostname.get(hostname, [])
                if not host_workers:
                    WARN(f"merge_db: no worker on source host '{hostname}' to delete, "
                         f"skipping {len(writer_ids)} writer_ids")
                    continue
                source_worker = host_workers[0]
                source_worker_ids.append(source_worker)
                worker_to_writers[source_worker] = writer_ids
                # data_path 传空 → C++ send_delete_data 从 db_registry 查源 data_path
                # （此时 cleanup 未执行，db_registry 仍是源的）。
                self._agent.send_delete_data(
                    source_worker, db_path, "", writer_ids)
                INFO(f"merge_db: sent DeleteData to worker {source_worker} on host "
                     f"'{hostname}' for {len(writer_ids)} writers")
            # 同步等待全部 DeleteDataAck；失败自动重试一轮（瞬时故障），仍失败发
            # 流程 message 提醒用户手动删除（残留清单完整暴露）。
            if source_worker_ids:
                del_ok, del_failed = self._agent.wait_delete_data_acks(
                    source_worker_ids, db_path, 60)
                if not del_ok:
                    INFO(f"merge_db: retrying source delete for workers={del_failed}")
                    for w in del_failed:
                        writers = worker_to_writers.get(w, [])
                        if writers:
                            self._agent.send_delete_data(w, db_path, "", writers)
                    del_ok, del_failed = self._agent.wait_delete_data_acks(
                        del_failed, db_path, 60)
                if del_ok:
                    INFO("merge_db: all source deletes confirmed")
                else:
                    WARN(f"merge_db: {len(del_failed)} source deletes failed after retry "
                         f"(workers={del_failed}) — manual cleanup required")
                    message("STOR::0004", 2,
                            f"merge_db source delete failed (manual cleanup required): "
                            f"db={db_path}, residual workers={list(del_failed)}")

        # 状态清理（无论是否删源，merge 已改变数据分布，旧索引都失效）：
        # 广播 MergeCleanup 让各 worker 清旧 local_idx/remote_idx + 按新路径重建 local_idx；
        # master 自身清旧索引 + 重建 remote_idx（指向 merge target）+ 更新 db_registry。
        # 删源 ack 已保证 worker 在线且响应过，紧随其后的广播时序确定（无需额外 sleep）。
        if ok:
            self._agent.cleanup_after_merge(
                db_path, completed, source_worker_ids, master_host_workers,
                merge_db_path, merge_data_path)
            INFO("merge_db: cleanup_after_merge done (broadcast + master state rebuilt)")

        # ── db chain 更新（取代 _MIGRATED_TO 机制）──
        # target 继承 source 身份（uid 不变）+ absorbed_from 记录旧 path +
        # 更新直接邻居的 prev/next 中的 db_path + 彻底删源目录。
        if ok:
            self._update_chain_on_merge(db_path, merge_db_path, merge_data_path)

        # 产物 db 句柄：复用 cleanup_after_merge 在 db_instances_ 建好的权威 Database
        # （用源 db_path，保持 object_name = db_path:short 一致）。不再单独构造临时 Database，
        # 避免其析构 unregister DataService::db_paths_ 的竞争。
        # read_object 走 master remote_idx（merge task 已登记对象位置到 merge worker）。
        merged_db = Database.__new__(Database)
        merged_db._db = self._agent.get_database(db_path)
        # 恢复 _DB_CHAIN 链信息
        merged_db._chain_file = DbChainFile(merge_db_path)
        merged_db._chain_uid = None
        merged_db._chain_role = None
        merged_db._chain_logical_name = None
        merged_db._load_chain_info()
        INFO(f"merge_db: done, ok={ok}, merged_data at {merge_data_path}")
        # 流程 message：merge_db 完成（跨机数据集中里程碑）。
        message("STOR::0002", 1,
                f"merge_db done: db_path={db_path}, objects={len(completed)}, "
                f"data_path={merge_data_path}")
        return merged_db

    def _update_chain_on_merge(self, source_path, target_path, target_data_path):
        """merge 后更新 db chain：target 继承 source 身份 + 更新邻居 + 彻底删源。

        按 docs/db-chain-design.md §7.3：
        5a. 读 source._DB_CHAIN（拿 uid, role, prev, next）
        5b. target._DB_CHAIN 继承 source 身份 + absorbed_from 追加 source_path
        5c. master uid_to_path_ 更新
        5d. 靠 source.next[] 更新下游 S.prev[uid].db_path = target_path
        5e. 靠 source.prev[] 更新上游 P.next[uid].db_path = target_path
        5g. 彻底删除 source_path 目录
        """
        import os
        import shutil
        source_cf = DbChainFile(source_path)
        source_chain = source_cf.read()

        if source_chain is None:
            # 旧 db 无 _DB_CHAIN → 无链更新，但仍删源目录（如果有 _MIGRATED_TO 兼容）
            INFO(f"_update_chain_on_merge: source has no _DB_CHAIN at {source_path}, "
                 "skipping chain update")
            return

        uid = source_chain.get("uid")
        role = source_chain.get("role")
        logical_name = source_chain.get("logical_name")
        prev_edges = source_chain.get("prev", [])
        next_edges = source_chain.get("next", [])
        absorbed = source_chain.get("absorbed_from", [])

        # 5b. target 继承 source 身份 + absorbed_from 追加 source_path
        target_cf = DbChainFile(target_path)
        new_absorbed = list(absorbed) + [source_path]
        target_chain = make_chain(uid, role, logical_name,
                                  prev=prev_edges, next_=next_edges,
                                  absorbed_from=new_absorbed)
        target_cf.write_new(target_chain)
        INFO(f"_update_chain_on_merge: target _DB_CHAIN written at {target_path}, "
             f"uid={uid}, role={role}, absorbed_from={new_absorbed}")

        # 5c. master uid_to_path_ 更新
        registry = get_registry()
        registry.update_path(uid, target_path)

        # 5d. 靠 source.next[] 更新下游 S.prev[uid].db_path = target_path
        for edge in next_edges:
            downstream_path = edge.get("db_path")
            if not downstream_path or not os.path.isdir(downstream_path):
                continue
            downstream_cf = DbChainFile(downstream_path)
            downstream_cf.update_neighbor_path(uid, target_path, is_next=False)
            INFO(f"_update_chain_on_merge: updated downstream {downstream_path} "
                 f"prev[{uid[:8]}].db_path -> {target_path}")

        # 5e. 靠 source.prev[] 更新上游 P.next[uid].db_path = target_path
        for edge in prev_edges:
            upstream_path = edge.get("db_path")
            if not upstream_path or not os.path.isdir(upstream_path):
                continue
            upstream_cf = DbChainFile(upstream_path)
            upstream_cf.update_neighbor_path(uid, target_path, is_next=True)
            INFO(f"_update_chain_on_merge: updated upstream {upstream_path} "
                 f"next[{uid[:8]}].db_path -> {target_path}")

        # 5g. 彻底删除 source_path 目录（含 _DB_META/_FROZEN/_DB_CHAIN/.idx，全部）
        #     必须在邻居更新之后。
        #     同 path merge（source_path == target_path）：不删源——源就是产物本身。
        #     跨 path merge（source_path != target_path）：删源，产物在 target。
        if os.path.abspath(source_path) != os.path.abspath(target_path):
            try:
                shutil.rmtree(source_path, ignore_errors=False)
                INFO(f"_update_chain_on_merge: deleted source directory {source_path}")
            except Exception as e:
                WARN(f"_update_chain_on_merge: failed to delete source {source_path}: {e}")
        else:
            INFO(f"_update_chain_on_merge: same-path merge, source=target={source_path}, "
                 "no deletion needed")

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

        # 先登记占位符再 spawn：若 spawn 后才 expect，worker 注册可能先到达，
        # 转正 erase 落空 → 占位符永久泄漏，wait_workers_registered 永不返回。
        self._agent.expect_worker(worker_id)

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
        # role：静态身份（hybrid 默认 / storage_only，不可变更）。此前被静默忽略
        #（F3 遗留，文档承诺未实现）；非法值 WARN 回退 hybrid。
        if config and isinstance(config, dict) and config.get("role"):
            role = str(config["role"])
            if role not in ("hybrid", "storage_only"):
                WARN(f"launch_workers: unknown role '{role}' for worker {worker_id} "
                     f"(expected hybrid|storage_only), falling back to hybrid")
                role = "hybrid"
            cmd.extend(["--worker-role", role])

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
                 attributes: list = None, role: str = "hybrid"):
        # role：静态身份（hybrid 默认 / storage_only），注册时上报、不可变更；
        # 独立于 attributes（可变、参与调度匹配）。storage_only 不参与计算调度
        #（仍可执行 internal 数据 task 与作为数据副本持有者）。
        self._agent = EXAgentWorker(worker_id, master_host, master_port,
                                    attributes or [], role)
        self._db_cache = {}
        self._db_path_pending = {}
        self._cache = {}
        self._master_host = master_host
        self._master_port = master_port
        self._worker_id = worker_id
        self._executor = None
        self._worker_procs = []
        self._role = role

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

    # ── 业务 RPC（PeerChannelGroup 底层透传）──────────────────────
    # payload 全程用 bytes（nanobind 零拷贝），无 latin-1 编解码。
    def start_peer_rpc_listen(self, host="", port=0):
        return self._agent.start_peer_rpc_listen(host, port)
    def peer_rpc_connect(self, host, port, retries=2, retry_interval_ms=500):
        return self._agent.peer_rpc_connect(host, port, retries, retry_interval_ms)
    def peer_rpc_call(self, conn_id, payload, timeout_ms=30000):
        return self._agent.peer_rpc_call(conn_id, payload, timeout_ms)
    def peer_rpc_respond(self, conn_id, rpc_id, payload):
        return self._agent.peer_rpc_respond(conn_id, rpc_id, payload)
    def peer_rpc_respond_failure(self, conn_id, rpc_id, reason):
        return self._agent.peer_rpc_respond_failure(conn_id, rpc_id, reason)
    def peer_rpc_recv_request(self, timeout_ms=30000):
        return self._agent.peer_rpc_recv_request(timeout_ms)
    def peer_rpc_notify_failure(self, conn_id, reason):
        return self._agent.peer_rpc_notify_failure(conn_id, reason)
    def peer_rpc_close(self, conn_id):
        self._agent.peer_rpc_close(conn_id)
    def stop_peer_rpc(self):
        self._agent.stop_peer_rpc()
    def peer_rpc_port(self):
        return self._agent.peer_rpc_port()


# ============================================================
# PeerChannelGroup — 可传递的业务 RPC channel 工厂
# ============================================================

import uuid as _uuid
import time as _time

def serialize_array(arr):
    """numpy array → bytes（含 dtype/shape，零拷贝 buffer 协议）。"""
    import numpy as _np
    a = _np.asarray(arr)
    dt = a.dtype.str.encode()  # 如 b'<f8'
    return bytes([len(dt)]) + dt + a.shape[0].to_bytes(4, 'little') + a.tobytes()

def deserialize_array(data):
    """bytes → numpy array（serialize_array 的逆）。"""
    import numpy as _np
    dt_len = data[0]
    dtype = _np.dtype(data[1:1+dt_len].decode())
    shape0 = int.from_bytes(data[1+dt_len:5+dt_len], 'little')
    return _np.frombuffer(data[5+dt_len:], dtype=dtype).reshape(shape0).copy()


class PeerRpcStatus:
    """PeerRpc 状态码（与 C++ PeerRpcStatus 枚举对应）。
    peer_rpc_call / chan.rpc 的返回 status 取这些值。"""
    PENDING = 0    # 未完成（内部用，不会作为返回值）
    OK = 1         # 正常响应
    ERROR = 2      # 对端主动 notify_failure（resp 为 reason）
    FAILED = 3     # 超时 / 连接断开 / send 失败（resp 为原因描述）


class PeerChannel:
    """客户端侧 channel（compute worker 用）。连接到 check 的业务端口。

    生命周期：正常结束时手动调 close()（发 BYE 握手 + 关闭）；异常退出时
    __del__ 兜底自动 close。故障由断连事件驱动，rpc 无默认超时。"""
    def __init__(self, agent, conn_id):
        self._agent = agent
        self._conn_id = conn_id
        self._closed = False

    def rpc(self, payload, timeout=None):
        """请求-响应（同步）。payload: bytes。返回 (status, response_bytes)。
        timeout=None（默认）：无限等待，故障由断连事件驱动（check 崩溃 → FAILED）。
        status 见 PeerRpcStatus：OK=正常, ERROR=对端 notify_failure/respond_failure,
        FAILED=断连/send 失败。"""
        if not isinstance(payload, (bytes, bytearray)):
            raise TypeError(f"payload must be bytes, got {type(payload).__name__}")
        timeout_ms = int(timeout * 1000) if timeout is not None else 0
        return self._agent.peer_rpc_call(self._conn_id, bytes(payload), timeout_ms)

    def notify_failure(self, reason):
        """主动告知对端失败退出。reason: bytes。"""
        if not isinstance(reason, (bytes, bytearray)):
            raise TypeError(f"reason must be bytes, got {type(reason).__name__}")
        self._agent.peer_rpc_notify_failure(self._conn_id, bytes(reason))

    def close(self):
        """优雅关闭：发 BYE 握手 + 关闭。幂等。"""
        if not self._closed:
            self._closed = True
            self._agent.peer_rpc_close(self._conn_id)

    def __del__(self):
        """GC 回收时自动关闭（异常路径兜底）。"""
        try:
            self.close()
        except Exception:
            pass


class PeerListener:
    """服务端侧 listener（check worker 用）。accept compute 连接。"""
    def __init__(self, agent, port):
        self._agent = agent
        self._port = port

    def accept_one(self, timeout=None):
        """阻塞等下一个请求。返回 (conn_id, rpc_id, src_worker_id, payload_bytes)。
        timeout=None（默认）：无限等待，只由请求到达或错误断连唤醒。
        错误断连（compute 崩溃/网络断，无 BYE）时抛 RuntimeError。
        超时（仅 timeout>0 时可能）返回 (0, 0, 0, b'')。"""
        timeout_ms = int(timeout * 1000) if timeout is not None else 0
        conn_id, rpc_id, src, payload = self._agent.peer_rpc_recv_request(timeout_ms)
        return conn_id, rpc_id, src, payload

    def respond(self, conn_id, rpc_id, payload):
        """回正常响应给请求方（status=OK）。payload: bytes。"""
        if not isinstance(payload, (bytes, bytearray)):
            raise TypeError(f"payload must be bytes, got {type(payload).__name__}")
        self._agent.peer_rpc_respond(conn_id, rpc_id, bytes(payload))

    def respond_failure(self, conn_id, rpc_id, reason):
        """对单个请求回失败（status=ERROR）。只让这一个请求的调用方收到失败，
        不影响同连接上其他 pending 请求。reason: bytes。"""
        if not isinstance(reason, (bytes, bytearray)):
            raise TypeError(f"reason must be bytes, got {type(reason).__name__}")
        self._agent.peer_rpc_respond_failure(conn_id, rpc_id, bytes(reason))

    def notify_failure(self, conn_id, reason):
        """主动告知对端失败（status=全局通知, rpc_id=0）。reason: bytes。"""
        if not isinstance(reason, (bytes, bytearray)):
            raise TypeError(f"reason must be bytes, got {type(reason).__name__}")
        self._agent.peer_rpc_notify_failure(conn_id, bytes(reason))

    def close(self):
        self._agent.stop_peer_rpc()

    @property
    def port(self):
        return self._port


class PeerChannelGroup:
    """可 pickle 的 channel 工厂。仅含唯一 group_id，随 task 参数传递。

    Usage（check 侧）:
        listener = group.listen(db)
        conn_id, rpc_id, _, payload = listener.accept_one()
        listener.respond(conn_id, rpc_id, response_bytes)

    Usage（compute 侧）:
        chan = group.connect(db)
        status, resp = chan.rpc(request_bytes)
    """
    def __init__(self, group_id=None):
        self.group_id = group_id or str(_uuid.uuid4())

    def __reduce__(self):
        # 仅 pickle group_id（轻量，随 task 参数传递）
        return (PeerChannelGroup, (self.group_id,))

    def _temp_name(self):
        return f"__fly_chan_{self.group_id}"

    def listen(self, db):
        """服务端：绑定业务端口 + 发布地址到 DB（跨 worker 可见）。返回 PeerListener。"""
        from fly.runtime import get_agent
        agent = get_agent()
        # 用 127.0.0.1（单机 worker 场景）。跨机时由 ProcessInfo --host 覆盖。
        host = "127.0.0.1"
        port = agent.start_peer_rpc_listen(host, 0)
        if port <= 0:
            raise RuntimeError("PeerChannelGroup.listen: failed to bind port")
        # 发布地址到 DB（save_to_db=True 默认，注册 master 使 compute 可经 TIER2/TIER3 读）
        db.write_object(self._temp_name(), {"host": host, "port": port})
        print(f"[PeerChannelGroup] listen on {host}:{port} (group={self.group_id})", flush=True)
        return PeerListener(agent, port)

    def connect(self, db, timeout=60):
        """客户端：等 check 发布的地址就绪 + connect_peer。返回 PeerChannel。

        地址就绪等待复用 @wait_obj（三级查询：local_idx → remote_idx → master probe），
        替代原来的手写 while+sleep 轮询。"""
        from fly.runtime import get_agent
        agent = get_agent()
        info = _read_peer_address(self, db, timeout=timeout)
        print(f"[PeerChannelGroup] connecting to {info['host']}:{info['port']}", flush=True)
        conn_id = agent.peer_rpc_connect(info["host"], info["port"])
        if conn_id == 0:
            raise ConnectionError(f"PeerChannelGroup.connect: failed to connect {info}")
        print(f"[PeerChannelGroup] connected conn_id={conn_id}", flush=True)
        return PeerChannel(agent, conn_id)


@_wait_obj(inputs=lambda group, db, **kw: [db.get_full_name(group._temp_name())])
def _read_peer_address(group, db, timeout=60):
    """等 check 发布的地址对象就绪后读取（@wait_obj 阻塞直到 master 可见）。

    group 是 PeerChannelGroup 实例（需访问 _temp_name）；db 是 Database。
    timeout 透传给 @wait_obj（None=永远等）。"""
    return db.read_object(group._temp_name())


