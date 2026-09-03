import pickle
import time
from _fly_storage import (
    ex_stg_get_data_service,
    EXStgWriteErrorType,
)

from monitor import record_read, record_write

# db_meta 模块（同包相对导入）——_DB_META JSON 元信息读写
from .db_meta import (
    DbMetaFile, generate_uid, make_meta, make_edge,
    find_edge, append_edge, match_edge,
)
from .chain_registry import get_registry

_MAX_RETRIES = 3
_RETRY_INTERVAL_SEC = 1.0


class Database:

    # ── 子类机制：role + 自动注册 ──────────────────────────────
    # 基类 role=None（裸 db / 旧 db）。子类通过类属性声明 role：
    #   class MatrixDb(Database):
    #       role = "matrix"
    # 定义时自动注册到 _ROLE_REGISTRY，find_db 按 role 重建子类实例。
    role = None
    _ROLE_REGISTRY = {}

    def __init_subclass__(cls, **kwargs):
        super().__init_subclass__(**kwargs)
        if cls.role is not None:
            Database._ROLE_REGISTRY[cls.role] = cls

    def __init__(self, db_path: str, data_path: str = "", writer_id: int = 0):
        from fly.runtime import _mode
        if _mode == "master":
            from fly.runtime import get_agent
            agent = get_agent()
            self._db = agent._agent.get_or_create_database(db_path, data_path, writer_id)
        else:
            from _fly_storage import ex_stg_create_database
            self._db = ex_stg_create_database(db_path, data_path, writer_id)

        # meta 管理器（用于 _DB_META 文件读写）
        self._meta_file = DbMetaFile(db_path)

        # 从 _DB_META 恢复 uid/role（已存在的 db），否则后续由 _init_chain 初始化
        self._chain_uid = None
        self._chain_role = None
        self._chain_logical_name = None

    _WRITE_ERROR_MESSAGES = {
        EXStgWriteErrorType.FROZEN_DB: "Write to frozen database",
        EXStgWriteErrorType.REGISTRATION_FAILED: "Write registration failed",
        EXStgWriteErrorType.REGISTRATION_TIMEOUT: "Write registration timeout",
    }

    def write_object(self, name: str, obj, backup: bool = False, save_to_db: bool = True,
                     cache: str = "none") -> str:
        """Write an object.

        Args:
            cache: 缓存预热等级（2026-08-30 双池裁定）。``"none"``（默认）仅
                落盘；``"low"``/``"high"`` 写后把对象预热入缓存（正式对象主池，
                temp 对象恒 temp 池）。读路径的等级语义见 read_object。
        """
        if not save_to_db:
            # temp 路径独立计时（_write_temp 内部 record_write），不进本路径。
            return self._write_temp(name, obj, cache=cache)

        t0 = time.perf_counter()
        try:
            # write_object returns a WriteErrorType int (OK=success).
            # DUPLICATE_SKIPPED is benign (same object already written) — not raised.
            py_name = type(obj).__name__
            obj_size = 0
            if hasattr(obj, "_write_to_db"):
                err = EXStgWriteErrorType(obj._write_to_db(self._db, name, py_name, backup))
            else:
                # 写侧恒流式（T2c 2026-08-31：streaming_write_threshold 开关与
                # 非流式分支已删——open_write_stream → finish_and_commit 是唯一
                # 路径，内存 R+常数而非 R+2C；frozen 时 open 返回 None）。
                stream = self._db.open_write_stream(name, py_name)
                if stream is None:
                    raise RuntimeError(f"Database is frozen: {name}")
                # 协议保持 DEFAULT（§9.5 尝试结论：pin 5 时 numpy 走
                # PickleBuffer 与 FlyStream.write 的 bytes 参数不兼容；
                # 且实测协议 4/5 in-band 内存特征一致——pin 无收益，
                # 按"验证不通过即放弃"回退）。
                pickle.dump(obj, stream)
                err = EXStgWriteErrorType(
                    stream.finish_and_commit(backup, cache != "none"))
                obj_size = stream.total_uncompressed

            if err != EXStgWriteErrorType.OK and err != EXStgWriteErrorType.DUPLICATE_SKIPPED:
                msg = self._WRITE_ERROR_MESSAGES.get(err, f"Write error (type={err})")
                raise RuntimeError(f"{msg}: {name}")
            # New value landed — drop any stale cache entry so a subsequent
            # read reflects the new value; then optional write-through 预热
            #（cache 双池裁定 2026-08-30：正式对象 → 主池 low/high）。
            self._invalidate_read_cache(name)
            if cache != "none":
                from storage import get_read_cache
                get_read_cache().put(f"{self.get_db_path()}:{name}", cache, obj,
                                     size=obj_size if obj_size > 0 else 0)
            return ""
        finally:
            # IO 归属计时（master 无 task 在跑时为空操作）；字节数由 C++
            # WriteRecord 汇总（TaskComplete.written_objects），此处仅计时。
            record_write(self.get_full_name(name), (time.perf_counter() - t0) * 1000.0)

    def _write_temp(self, name: str, obj, cache: str = "none") -> str:
        t0 = time.perf_counter()
        try:
            if hasattr(obj, "_write_to_db"):
                result = obj._write_to_db(self._db, name, type(obj).__name__, False)
                self._invalidate_read_cache(name)
                return result
            # temp 写流式化（T2d 2026-08-31）：pickle.dump 直入 temp_writer_
            # 增量直写（R+常数，取代旧 _write_temp_pickle 的 R+2C 整对象
            # 缓冲）。盘写完成后 C++ commit 回调完成 INCOMPLETE→COMPLETE→
            # register 语义；frozen 时 open 返回 None。
            py_name = type(obj).__name__
            stream = self._db.open_write_stream(name, py_name, True)
            if stream is None:
                raise RuntimeError(f"Database is frozen: {name}")
            pickle.dump(obj, stream)
            err = EXStgWriteErrorType(stream.finish_and_commit(False, False))
            if err != EXStgWriteErrorType.OK and err != EXStgWriteErrorType.DUPLICATE_SKIPPED:
                msg = self._WRITE_ERROR_MESSAGES.get(err, f"Write error (type={err})")
                raise RuntimeError(f"{msg}: {name}")
            self._invalidate_read_cache(name)
            # 预热（temp 对象恒 temp 池——双池裁定）。
            if cache != "none":
                from storage import get_read_cache
                get_read_cache().put(f"{self.get_db_path()}:{name}", "temp", obj,
                                     size=stream.total_uncompressed)
            return ""
        finally:
            record_write(self.get_full_name(name), (time.perf_counter() - t0) * 1000.0)

    def read_object(self, name: str, backup: bool = False, cache: str = "low"):
        # IO 归属计时：全分支包裹（cache 命中/C++ 对象/pickle 各路径）；
        # 字节数仅在 Python 拿到解压 data 的路径可得（C++ 对象路径记 0）。
        # 缓存语义（2026-08-30 双池裁定）：默认 "low"——读到即按 low 等级
        # 入缓存（temp 对象路由 temp 池）；"high" 高优先级；"none" 显式零
        # 缓存。命中查询不分级（等级只影响淘汰优先级）。
        t0 = time.perf_counter()
        nbytes = 0
        try:
            import _fly_storage

            def _cpp_cls(py_name):
                cls = getattr(_fly_storage, py_name, None)
                return cls if cls is not None and hasattr(cls, "_read_from_db") else None

            rc = None
            key = None
            if cache != "none":
                from storage import get_read_cache
                rc = get_read_cache()
                key = f"{self.get_db_path()}:{name}"
                obj = rc.get(key)
                if obj is not None:
                    return obj

            # 三分层规范（§14.12）：none=会被修改的数据，每次全新反序列化。
            # 曾映射为 "low"（旧 low 池时代的刻意行为，读侧 populate 语义），
            # T4 单层化后语义漂移——C++ 侧 read_object 原生支持 none bypass，
            # 直传对齐两侧口径。
            cpp_cache = cache
            # populate 等级路由：读原语携带 temp 标记（本地 local_idx 判定 /
            # serve META 告知——跨进程读取方本进程查不到 temp 属性）。
            # populate 发生在流式消费成功点，按 stream.is_temp 路由。

            # 恒流式（2026-08-30 用户裁定：常规读统一流式，streaming_read_threshold
            # 逃生口不保留；仅非反序列化场景（backup 副本拉取等 C++ 侧）保留
            # 全量拉取）。Unpickler(FlyStream) 增量消费——内存 R+常数而非
            # C+2R。对象不可见（NOT_FOUND/NOT_READY）由 export 层直接 KeyError。
            # D4（§14.3）：消费中途失败 → 重新调 open（对象级重来）。
            detail = ""  # 失败分类（chunk_source 契约："io:"/"integrity:" 前缀）
            for _attempt in range(2):
                try:
                    stream = _fly_storage.ex_stg_open_read_stream(
                        self._db, name, backup)
                except KeyError:
                    raise  # 对象不可见：全源 miss（TIER3 已在前置轮换覆盖）
                cls = _cpp_cls(stream.py_name)
                if cls is not None:
                    # C++ 对象走权威重建路径（含 ObjectCache 命中快路径）；
                    # 弃置已打开的流（析构释放 source/连接资源）。
                    stream = None
                    return cls._read_from_db(self._db, name, cpp_cache)
                try:
                    obj = pickle.Unpickler(stream).load()
                    corrupt = stream.checksum_failed()
                    detail = stream.failure_detail() if corrupt else ""
                except (pickle.UnpicklingError, EOFError, AttributeError,
                        ImportError, IndexError):
                    corrupt = True  # 流截断/源坏——按损坏处理
                    detail = ""
                if not corrupt:
                    nbytes = stream.total_uncompressed  # property
                    if rc is not None:
                        rc.put(key, "temp" if stream.is_temp else cache, obj,
                               size=nbytes)
                    return obj
                stream = None  # 弃流重开（对象级重来）
            # 两轮流式消费均败（read_streaming 内已副本轮换+零容忍预算，
            # 消费端再重开一轮仍败）——#5 裁定禁止整缓冲回退，直接 FATAL
            #（task 失败通道，§5 零容忍语义）。失败分类（2026-09-04）：源侧
            # IO/网络失败（detail "io:" 前缀）不是数据损坏——独立
            # [FATAL-STREAM-IO] 文案，不再误报 corruption。
            if detail.startswith("io:"):
                raise RuntimeError(
                    f"[FATAL-STREAM-IO] streaming source I/O failed twice: "
                    f"{name} ({detail})")
            raise RuntimeError(
                f"[FATAL-DATA-CORRUPTION] streaming consume failed twice: "
                f"{name}" + (f" ({detail})" if detail else ""))
        finally:
            record_read(self.get_full_name(name), nbytes,
                        (time.perf_counter() - t0) * 1000.0)

    def _invalidate_read_cache(self, name: str):
        """Drop any cached Python (high-tier) entry for `name`.

        The C++ ObjectCache auto-invalidates on write/remove (object_cache.h
        contract), but the parallel Python ReadCache is a separate structure
        that read_object populates for pickle objects with cache="high".
        Without this invalidation, write(A)->read(A,"high")->write(A,new) leaves
        a stale object in the cache and the next read(A,"high") returns it.

        Harmless when no entry exists (ReadCache.remove handles misses) and
        when the object is a C++ one (never cached here). Called after every
        successful write/remove path below.
        """
        try:
            from storage import get_read_cache
            get_read_cache().remove(f"{self.get_db_path()}:{name}")
        except Exception:
            # Cache invalidation must never break a successful write/remove.
            pass

    def backup_object(self, name: str):
        self._db.backup_object(name)

    # write_object_raw / read_object_raw / 直写直读导出（_write_pickle_bytes /
    # _read_decompressed 等）已删除（2026-08-30 / T2b 2026-08-31 用户裁定，
    # 生产零使用）。写侧统一 open_write_stream → finish_and_commit 恒流式，
    # 读侧统一 ex_stg_open_read_stream 流式消费（read_object）。

    def get_full_name(self, name: str) -> str:
        return self._db.get_full_name(name)

    def get_db_path(self) -> str:
        return self._db.get_db_path()

    def get_data_path(self) -> str:
        return self._db.get_data_path()

    def freeze(self):
        self._db.freeze()

    def is_frozen(self) -> bool:
        return self._db.is_frozen()

    def load_meta(self):
        return self._meta_to_ex(self._meta_file.read())

    @staticmethod
    def load_meta_from_path(db_path: str):
        """静态读 _DB_META（JSON），不构造 Database 实例（不触发 DataService register）。

        用于 merge_db / load_db 等场景：在已 open_db 的进程内读 meta 而不
        重复注册 db_path。返回 EXStgDbMeta 兼容对象（created_at + workers），
        消费方与原 C++ 版本零改动；文件缺失/损坏 → created_at=0（有效性哨兵）。
        """
        return Database._meta_to_ex(DbMetaFile(db_path).read())

    @staticmethod
    def _meta_to_ex(meta_dict):
        """_DB_META JSON dict → EXStgDbMeta（QA/agent 消费兼容层）。"""
        from _fly_storage import EXStgDbMeta, EXStgWorkerInfo
        d = meta_dict or {}
        m = EXStgDbMeta(int(d.get("created_at") or 0))
        m.workers = [
            EXStgWorkerInfo(
                int(w.get("worker_id") or 0),
                w.get("writer_id", ""),
                w.get("hostname", ""),
                w.get("ip_address", ""),
                w.get("launch_command", ""))
            for w in d.get("workers", [])
        ]
        return m

    def reset(self):
        self._db.reset()

    def remove_object(self, name: str):
        self._db.remove_object(name)
        # Drop any cached Python high-tier entry so a subsequent read_object
        # sees "not found" rather than the removed object's stale reference.
        self._invalidate_read_cache(name)

    # ---- Var service: lightweight small-object KV ----
    # set_var/get_var/remove_var bypass write_object's compression / cache /
    # WriteBackQueue / dependency-graph machinery. Vars are immutable (a second
    # set on the same name is rejected) and are persisted at freeze time.
    #
    # var 是 Python 业务侧的轻量数据对象 API（用户裁定 2026-09-02）：值一律
    # pickle → FlyBuffer 存储。C++ 导出对象不进 var——需要 C++ 对象时由
    # Python get_var 后传入 C++ 导出函数。值经 FlyBufferPtr 全程零拷贝
    # （pickle.dump 直写 FlyBuffer；get_var 侧 unwrap + pickle.loads）。
    def set_var(self, name: str, value):
        """Store a small object under `name`. Synchronous (waits for master).

        Var is immutable: a second set_var on an existing name is rejected.
        Serialized size > 1K logs a warning (use write_object instead).
        """
        type_name = type(value).__name__
        from _fly_storage import FlyBuffer
        buf = FlyBuffer()
        pickle.dump(value, buf)
        ok = self._db._set_var_buffer(name, buf, type_name)
        if not ok:
            import _fly_log
            _fly_log.ERR(f"set_var rejected: '{name}' (frozen or already exists)")
            raise RuntimeError(f"set_var failed: '{name}' (frozen or already exists)")

    def get_var(self, name: str):
        """Retrieve a small object stored under `name`. Synchronous (queries
        master on local cache miss). Returns None if the var does not exist
        (distinct from a stored value, which is returned as-is).

        Values are Python-side lightweight objects (pickle protocol). C++
        exported objects do not go through var——Python get_var 后传入 C++
        导出函数（用户裁定 2026-09-02）。
        """
        success, buf, type_name = self._db._get_var(name)
        if not success or buf is None:
            return None  # var does not exist
        # pickle.load reads from the FlyBuffer via the file protocol
        # (readinto/read/readline). pickle's C unpickler uses readinto to fill
        # its own working buffer directly — one serialization-inherent copy,
        # no intermediate Python bytes object.
        # seek(0) first: the FlyBufferPtr is shared in the cache, and a prior
        # read may have advanced the cursor (Python GIL makes seek+load atomic).
        buf.seek(0)
        return pickle.load(buf)

    def remove_var(self, name: str):
        """Remove a var. Asynchronous (local cache cleared immediately,
        master notified without waiting for ack)."""
        self._db._remove_var(name)

    # ── DB Chain：构造与链管理 ─────────────────────────────────

    @classmethod
    def _wrap(cls, db_path: str, data_path: str = ""):
        """获取/复用 C++ Database 指针，包装成 cls 实例（不建库）。

        master: 走 MasterAgent::get_or_create_database（权威 map 复用同一 C++ 对象）。
        worker: 走 ex_stg_create_database。
        不重复建库——同 db_path 的 C++ Database 全进程唯一。

        与 __init__ 的区别：__init__ 调用 open_db 逻辑（建库+写 meta），
        _wrap 只获取已存在 db 的句柄（load 场景）。find_db 用 _wrap 构造前驱。
        """
        instance = cls.__new__(cls)   # 不调 __init__
        from fly.runtime import _mode
        if _mode == "master":
            from fly.runtime import get_agent
            instance._db = get_agent()._agent.get_or_create_database(db_path, data_path, 0)
        else:
            from _fly_storage import ex_stg_create_database
            instance._db = ex_stg_create_database(db_path, data_path, 0)

        instance._meta_file = DbMetaFile(db_path)
        instance._chain_uid = None
        instance._chain_role = None
        instance._chain_logical_name = None
        instance._load_chain_info()
        return instance

    def _init_chain(self, uid, role, logical_name, prev_edges=None, data_path=""):
        """新建 db 时写入 _DB_META（首次写入，持锁 RMW 保留已有 workers）。

        Args:
            uid: 逻辑身份。
            role: 角色。
            logical_name: 逻辑名。
            prev_edges: 前驱边列表 [{uid, role, logical_name, db_path}]。
            data_path: 正式数据目录（空 = 数据在 db 目录内自包含）。db 级
                属性存 meta，task 参数编码不再携带。
        """
        meta = make_meta(uid, role, logical_name, prev=prev_edges or [],
                         data_path=data_path)
        self._meta_file.write_new(meta)
        self._chain_uid = uid
        self._chain_role = role
        self._chain_logical_name = logical_name

        # 注册到进程级 uid↔path 映射
        get_registry().register(uid, self.get_db_path())

    def _load_chain_info(self):
        """从磁盘 _DB_META 恢复 uid/role/logical_name（load_db / _wrap 时调用）。"""
        meta = self._meta_file.read()
        if meta is not None:
            self._chain_uid = meta.get("uid")
            self._chain_role = meta.get("role")
            self._chain_logical_name = meta.get("logical_name")
            # 注册到进程级映射
            if self._chain_uid:
                get_registry().register(self._chain_uid, self.get_db_path())
        # 旧 db 无 _DB_META（JSON）→ uid/role 均为 None，视为叶子

    def get_uid(self):
        """db 的逻辑身份 uid（旧 db 无 _DB_CHAIN 时为 None）。"""
        return self._chain_uid

    def get_role(self):
        """db 的角色 role（旧 db 无 _DB_CHAIN 时为 None）。"""
        if self._chain_role is not None:
            return self._chain_role
        # 子类的 role 类属性优先
        return type(self).role


    def _get_chain_data(self):
        """读完整 _DB_META dict（并发安全 LOCK_SH）。无则返回 None。"""
        return self._meta_file.read()

    # ── DB Chain：查询 API ─────────────────────────────────────

    def find_db(self, role=None, logical_name=None, uid=None):
        """沿自身 DAG 向前（BFS），返回距离最近的一个匹配前驱。

        匹配条件：uid 精确相等，或 role/logical_name 任一非 None 即参与匹配（AND 组合）。
        多匹配：返回跳数最少者；同跳数按 prev 声明顺序取第一个。
        找不到返回 None。

        返回对应 role 子类实例（按 _DB_CHAIN 记录的 role 查 _ROLE_REGISTRY 重建）。
        """
        results = self._bfs_search(role, logical_name, uid, find_all=False)
        return results[0] if results else None

    def find_all_dbs(self, role=None, logical_name=None):
        """返回 DAG 中所有匹配前驱列表（按 BFS 距离排序）。"""
        return self._bfs_search(role, logical_name, None, find_all=True)

    def prevs(self):
        """返回直接前驱列表（仅一层，不递归）。"""
        chain = self._get_chain_data()
        if chain is None:
            return []
        return [self._reconstruct(edge) for edge in chain.get("prev", [])]

    def nexts(self):
        """返回直接后继列表（仅一层，不递归）。"""
        chain = self._get_chain_data()
        if chain is None:
            return []
        return [self._reconstruct(edge) for edge in chain.get("next", [])]

    def _bfs_search(self, role, logical_name, uid, find_all):
        """BFS 遍历 DAG 前驱，收集匹配的 db 实例。

        Returns:
            list of _Database 实例（按 BFS 距离排序）。find_all=False 时最多返回 1 个。
        """

        chain = self._get_chain_data()
        if chain is None:
            return []

        registry = get_registry()
        queue = []  # [(edge, depth)]
        visited = set()
        results = []

        # 初始层：自己的直接前驱
        for edge in chain.get("prev", []):
            edge_uid = edge.get("uid")
            if edge_uid is None or edge_uid in visited:
                continue
            visited.add(edge_uid)
            if match_edge(edge, role, logical_name, uid):
                results.append((edge, 0))
                if not find_all:
                    return [self._reconstruct(results[0][0])]
            queue.append((edge, 0))

        # BFS 展开
        while queue:
            cur_edge, depth = queue.pop(0)
            cur_path = cur_edge.get("db_path")
            if not cur_path:
                continue

            # 读前驱的 _DB_META 继续展开（DbMetaFile 顶层已 import）
            prev_cf = DbMetaFile(cur_path)
            prev_chain = prev_cf.read()
            if prev_chain is None:
                continue

            for next_edge in prev_chain.get("prev", []):
                next_uid = next_edge.get("uid")
                if next_uid is None or next_uid in visited:
                    continue
                visited.add(next_uid)
                if match_edge(next_edge, role, logical_name, uid):
                    results.append((next_edge, depth + 1))
                    if not find_all:
                        return [self._reconstruct(results[0][0])]
                queue.append((next_edge, depth + 1))

        return [self._reconstruct(edge) for edge, _ in results]

    def _reconstruct(self, edge):
        """根据边节点重建 db 子类实例。

        先查 master uid→path 映射拿最新 path（merge 后可能变了），
        再按 role 查 _ROLE_REGISTRY 选子类，用 _wrap 构造。
        """
        uid = edge.get("uid")
        edge_path = edge.get("db_path")

        # 优先查注册表拿最新 path（merge 后更新过）
        actual_path = edge_path
        if uid:
            resolved = get_registry().resolve_uid(uid)
            if resolved:
                actual_path = resolved

        if not actual_path:
            return None

        # 按 role 选子类
        role = edge.get("role")
        cls = Database._ROLE_REGISTRY.get(role, Database) if role else Database
        return cls._wrap(actual_path)

    # ── DB Chain：建链辅助（供 _create_db / open_db 调用）─────

    def _add_next_to_chain(self, edge):
        """向后继列表追加一条边（建链时回填上游的 next）。"""
        def add_next(d):
            d["next"], _ = append_edge(d.get("next", []), edge)
            return d
        self._meta_file.update(add_next)

    # ── DB Chain：next 自愈（load 时校验补齐）─────────────────

    def _heal_next_edges(self):
        """校验并补齐缺失的 next 边（建链 crash 自愈）。

        遍历自身的 prev[]，检查每个前驱的 next[] 是否包含自己。
        若缺失则回填（prev 是权威边，next 是可重建的缓存）。

        在 load_db / Project.load 时调用。
        """
        chain = self._get_chain_data()
        if chain is None:
            return

        self_uid = chain.get("uid")
        self_role = chain.get("role")
        self_lname = chain.get("logical_name")
        self_path = self.get_db_path()
        if not self_uid:
            return

        for prev_edge in chain.get("prev", []):
            prev_path = prev_edge.get("db_path")
            prev_uid = prev_edge.get("uid")
            if not prev_path or not prev_uid:
                continue

            # 读前驱的 _DB_META，检查 next 是否含自己（符号顶层已 import）
            prev_cf = DbMetaFile(prev_path)
            prev_chain = prev_cf.read()
            if prev_chain is None:
                continue

            existing = find_edge(prev_chain.get("next", []), self_uid)
            if existing is None:
                # 缺失 → 回填
                self_edge = make_edge(self_uid, self_role, self_lname, self_path)

                def heal_upstream(d, edge=self_edge):
                    d["next"], _ = append_edge(d.get("next", []), edge)
                    return d

                prev_cf.update(heal_upstream)

    def __repr__(self):
        return f"Database(db_path={self.get_db_path()})"
