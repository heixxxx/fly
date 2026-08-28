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
                     cache: str = "low") -> str:
        """Write an object.

        Args:
            cache: 保存等级。``"low"``（默认）写入即填充 low-tier 压缩缓存
                （读请求不等后台落盘即可命中）；``"none"`` 仅落盘不进缓存
                （数据搬运/merge 等不希望中间对象挤占缓存的场景，读走索引+磁盘）。
        """
        if not save_to_db:
            # temp 路径独立计时（_write_temp 内部 record_write），不进本路径。
            return self._write_temp(name, obj)

        t0 = time.perf_counter()
        try:
            # write_object / _write_pickle_bytes return a WriteErrorType int (OK=success).
            # DUPLICATE_SKIPPED is benign (same object already written) — not raised.
            py_name = type(obj).__name__
            if hasattr(obj, "_write_to_db"):
                err = EXStgWriteErrorType(obj._write_to_db(self._db, name, py_name, backup))
            else:
                from core import get_config as _gc
                _cfg = _gc()
                if _cfg.get_int("streaming_write_threshold") > 0:
                    # L1 流式写（§9.1）：pickle.dump 流入 → 压缩块直写增量
                    # record（内存 R+常数而非 R+2C）。写前不知对象大小——
                    # 开关启用即统一走流式（小对象增量 API 等价，行为一致）。
                    stream = self._db.open_write_stream(name, py_name)
                    if stream is not None:
                        # 协议保持 DEFAULT（§9.5 尝试结论：pin 5 时 numpy 走
                        # PickleBuffer 与 FlyStream.write 的 bytes 参数不兼容；
                        # 且实测协议 4/5 in-band 内存特征一致——pin 无收益，
                        # 按"验证不通过即放弃"回退）。
                        pickle.dump(obj, stream)
                        err = EXStgWriteErrorType(
                            stream.finish_and_commit(backup, cache != "none"))
                    else:
                        raise RuntimeError(f"Database is frozen: {name}")
                else:
                    from _fly_storage import FlyStream, EXStgCompressionType
                    _cm = {"none": EXStgCompressionType.NONE, "lz4": EXStgCompressionType.LZ4,
                           "zlib": EXStgCompressionType.ZLIB, "zstd": EXStgCompressionType.ZSTD}
                    stream = FlyStream(_cm.get(_cfg.get_str("compression_type"), EXStgCompressionType.LZ4),
                                       _cfg.get_int("serialize_chunk_size"), py_name)
                    pickle.dump(obj, stream)
                    stream.flush()
                    buf = stream.finish()
                    err = EXStgWriteErrorType(self._db._commit_stream(
                        name, buf, py_name, backup, cache != "none"))

            if err != EXStgWriteErrorType.OK and err != EXStgWriteErrorType.DUPLICATE_SKIPPED:
                msg = self._WRITE_ERROR_MESSAGES.get(err, f"Write error (type={err})")
                raise RuntimeError(f"{msg}: {name}")
            # New value landed — drop any stale Python high-tier cache entry so a
            # subsequent read_object(cache="high") reflects the new value.
            self._invalidate_read_cache(name)
            return ""
        finally:
            # IO 归属计时（master 无 task 在跑时为空操作）；字节数由 C++
            # WriteRecord 汇总（TaskComplete.written_objects），此处仅计时。
            record_write(self.get_full_name(name), (time.perf_counter() - t0) * 1000.0)

    def _write_temp(self, name: str, obj) -> str:
        t0 = time.perf_counter()
        try:
            if hasattr(obj, "_write_to_db"):
                result = obj._write_to_db(self._db, name, type(obj).__name__, False)
                self._invalidate_read_cache(name)
                return result
            data = pickle.dumps(obj)
            py_name = type(obj).__name__
            # Compress + register + store in one C++ call — avoids
            # compress→Python bytes→CMString roundtrip.
            self._db._write_temp_pickle(name, data, py_name)
            self._invalidate_read_cache(name)
            return ""
        finally:
            record_write(self.get_full_name(name), (time.perf_counter() - t0) * 1000.0)

    def read_object(self, name: str, backup: bool = False, cache: str = "low"):
        # IO 归属计时：全分支包裹（cache 命中/C++ 对象/pickle 各路径）；
        # 字节数仅在 Python 拿到解压 data 的路径可得（C++ 对象路径记 0）。
        t0 = time.perf_counter()
        nbytes = 0
        try:
            # Caching tier dispatch:
            #   - nanobind (C++ exported) classes: _read_from_db → C++ ObjectCache
            #     Supports "low" (default), "high", "none" cache tiers.
            #   - pickle (Python) objects: "high" → Python ReadCache high tier;
            #     "low"/"none" → C++ ObjectCache low tier (transparent, via
            #     _read_streaming) + reconstruct every time.
            py_name = self._db._get_py_name(name)
            import _fly_storage
            cls = getattr(_fly_storage, py_name, None)
            is_cpp_obj = cls is not None and hasattr(cls, "_read_from_db")

            if is_cpp_obj:
                # nanobind class → C++ read_object<Cls> with specified cache tier.
                return cls._read_from_db(self._db, name, cache)

            if cache == "high":
                from storage import get_read_cache
                rc = get_read_cache()
                db_path = self.get_db_path()
                key = f"{db_path}:{name}"
                obj = rc.get(key, "high")
                if obj is not None:
                    return obj
                # Zero-copy: use _read_decompressed to avoid intermediate copies
                data, _ = self._db._read_decompressed(name, backup)
                # read_object_compressed 在所有 tier miss 时返回 nullptr，_read_decompressed
                # 翻译成空 bytes（storage_export.cpp）。不检查直接 pickle.loads(b'') 会抛
                # 误导性的 EOFError: Ran out of input，掩盖"对象不存在/尚未可见"的真相。
                # 这里前置检查，把语义还原成标准的"读不到"异常。
                if not data:
                    raise KeyError(
                        f"Object '{name}' not found (no data — not yet visible to master "
                        "or never written)")
                nbytes = len(data)
                obj = pickle.loads(data)
                rc.put(key, "high", obj)
                return obj

            # pickle object, cache="low"/"none": C++ low tier handles byte caching.
            # L3 流式（§8.1）：Unpickler(FlyStream) 增量消费——内存 R+常数而非
            # C+2R。streaming_read_threshold=0 可关闭（逃生口）；对象不可见
            #（NOT_FOUND/NOT_READY）由 export 层直接 KeyError（不回退——回退
            # 只会得到同样结果）。流中途异常/校验失败 → 回退整缓冲完整编排
            #（内含一次重取 + FATAL 语义，零容忍 §5）。
            from core import get_config as _gc
            if _gc().get_int("streaming_read_threshold") > 0:
                try:
                    import _fly_storage
                    stream = _fly_storage.ex_stg_open_read_stream(self._db, name, backup)
                except KeyError:
                    raise  # 对象不可见：语义与整缓冲路径一致
                try:
                    obj = pickle.Unpickler(stream).load()
                    corrupt = stream.checksum_failed()
                except (pickle.UnpicklingError, EOFError, AttributeError,
                        ImportError, IndexError):
                    corrupt = True  # 流截断/源坏——按损坏回退
                if not corrupt:
                    nbytes = stream.total_uncompressed  # property
                    return obj
                # 回退：完整 TIER2 编排 + 一次重取 + FATAL（_read_decompressed）。
                data, _ = self._db._read_decompressed(name, backup)
                if not data:
                    raise KeyError(
                        f"Object '{name}' not found (no data — not yet visible "
                        "to master or never written)")
                nbytes = len(data)
                return pickle.loads(data)

            # Zero-copy: use _read_decompressed to avoid intermediate copies
            data, _ = self._db._read_decompressed(name, backup)
            if not data:
                raise KeyError(
                    f"Object '{name}' not found (no data — not yet visible to master "
                    "or never written)")
            nbytes = len(data)
            return pickle.loads(data)
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

    def write_object_raw(self, name: str, data: str, backup: bool = False) -> str:
        t0 = time.perf_counter()
        try:
            ret = self._db.write_object_raw(name, data, backup)
            self._invalidate_read_cache(name)
            return ret
        finally:
            record_write(self.get_full_name(name), (time.perf_counter() - t0) * 1000.0)

    def read_object_raw(self, name: str) -> str:
        t0 = time.perf_counter()
        nbytes = 0
        try:
            ret = self._db.read_object_raw(name)
            if ret is not None:
                nbytes = len(ret)
            return ret
        finally:
            record_read(self.get_full_name(name), nbytes,
                        (time.perf_counter() - t0) * 1000.0)

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
    # All values flow through FlyBufferPtr (zero-copy in process):
    #   - C++ exported objects: __getstate_buffer__ returns a FlyBufferPtr stored
    #     directly; get_var reconstructs via __setstate_from_buffer__ (no Python
    #     bytes round-trip).
    #   - Python objects: pickle.dumps -> bytes -> wrapped into FlyBuffer at the
    #     C++ boundary; get_var unwraps and pickle.loads.
    def set_var(self, name: str, value):
        """Store a small object under `name`. Synchronous (waits for master).

        Var is immutable: a second set_var on an existing name is rejected.
        Serialized size > 1K logs a warning (use write_object instead).
        """
        type_name = type(value).__name__
        if hasattr(value, '__getstate_buffer__'):
            # C++ exported object: zero-copy. __getstate_buffer__ returns a
            # FlyBufferPtr (FLY_ENCODE_TO_BUFFER, non-streaming) that is stored
            # directly via shared ownership.
            buf = value.__getstate_buffer__()
            ok = self._db._set_var_buffer(name, buf, type_name)
        else:
            # Python object: pickle.dump writes directly into a FlyBuffer via the
            # file protocol — no intermediate Python bytes object.
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

        Deserialization dispatches by the stored type_name: C++ exported objects
        are reconstructed via __setstate_from_buffer__ (zero-copy from the shared
        FlyBufferPtr); Python objects via pickle.loads.
        """
        success, buf, type_name = self._db._get_var(name)
        if not success or buf is None:
            return None  # var does not exist
        import _fly_storage
        cls = getattr(_fly_storage, type_name, None)
        if cls is not None and hasattr(cls, '_read_from_db'):
            obj = cls.__new__(cls)
            obj.__setstate_from_buffer__(buf)  # zero-copy fill
            return obj
        # Python object: pickle.load reads from the FlyBuffer via the file
        # protocol (readinto/read/readline). pickle's C unpickler uses
        # readinto to fill its own working buffer directly — one
        # serialization-inherent copy, no intermediate Python bytes object.
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
