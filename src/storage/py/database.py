import pickle
import time
from _fly_storage import (
    ex_stg_get_data_service,
    EXStgWriteErrorType,
)

_MAX_RETRIES = 3
_RETRY_INTERVAL_SEC = 1.0


class _Database:

    def __init__(self, base_path: str, data_path: str = "", writer_id: int = 0):
        from fly.runtime import _mode
        if _mode == "master":
            from fly.runtime import get_agent
            agent = get_agent()
            self._db = agent._agent.get_or_create_database(base_path, data_path, writer_id)
        else:
            from _fly_storage import ex_stg_create_database
            self._db = ex_stg_create_database(base_path, data_path, writer_id)

    _WRITE_ERROR_MESSAGES = {
        EXStgWriteErrorType.FROZEN_DB: "Write to frozen database",
        EXStgWriteErrorType.REGISTRATION_FAILED: "Write registration failed",
        EXStgWriteErrorType.REGISTRATION_TIMEOUT: "Write registration timeout",
    }

    def write_object(self, name: str, obj, backup: bool = False, save_to_db: bool = True) -> str:
        if not save_to_db:
            return self._write_temp(name, obj)

        # write_object / _write_pickle_bytes return a WriteErrorType int (OK=success).
        # DUPLICATE_SKIPPED is benign (same object already written) — not raised.
        py_name = type(obj).__name__
        if hasattr(obj, "_write_to_db"):
            err = EXStgWriteErrorType(obj._write_to_db(self._db, name, py_name, backup))
        else:
            from _fly_storage import FlyStream, EXStgCompressionType
            from core import get_config as _gc
            _cfg = _gc()
            _cm = {"none": EXStgCompressionType.NONE, "lz4": EXStgCompressionType.LZ4,
                   "zlib": EXStgCompressionType.ZLIB, "zstd": EXStgCompressionType.ZSTD}
            stream = FlyStream(_cm.get(_cfg.get_str("compression_type"), EXStgCompressionType.LZ4),
                               _cfg.get_int("serialize_chunk_size"), py_name)
            pickle.dump(obj, stream)
            stream.flush()
            buf = stream.finish()
            err = EXStgWriteErrorType(self._db._commit_stream(name, buf, py_name, backup))

        if err != EXStgWriteErrorType.OK and err != EXStgWriteErrorType.DUPLICATE_SKIPPED:
            msg = self._WRITE_ERROR_MESSAGES.get(err, f"Write error (type={err})")
            raise RuntimeError(f"{msg}: {name}")
        return ""

    def _write_temp(self, name: str, obj) -> str:
        if hasattr(obj, "_write_to_db"):
            result = obj._write_to_db(self._db, name, type(obj).__name__, False)
            self._db._mark_temp(name)
            return result
        data = pickle.dumps(obj)
        py_name = type(obj).__name__
        # Compress + register + store in one C++ call — avoids
        # compress→Python bytes→CMString roundtrip.
        self._db._write_temp_pickle(name, data, py_name)
        return ""

    def read_object(self, name: str, backup: bool = False, cache: str = "low"):
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
            try:
                from storage.py.read_cache import get_read_cache
            except ImportError:
                from storage.read_cache import get_read_cache
            rc = get_read_cache()
            db_id = self.get_db_id()
            key = f"{db_id}:{name}"
            obj = rc.get(key, "high")
            if obj is not None:
                return obj
            # Zero-copy: use _read_decompressed to avoid intermediate copies
            data, _ = self._db._read_decompressed(name, backup)
            obj = pickle.loads(data)
            rc.put(key, "high", obj)
            return obj

        # pickle object, cache="low"/"none": C++ low tier handles byte caching.
        # Zero-copy: use _read_decompressed to avoid intermediate copies
        data, _ = self._db._read_decompressed(name, backup)
        return pickle.loads(data)

    def backup_object(self, name: str):
        self._db.backup_object(name)

    def write_object_raw(self, name: str, data: str, backup: bool = False) -> str:
        return self._db.write_object_raw(name, data, backup)

    def read_object_raw(self, name: str) -> str:
        return self._db.read_object_raw(name)

    def get_full_name(self, name: str) -> str:
        return self._db.get_full_name(name)

    def get_db_id(self) -> str:
        return self._db.get_db_id()

    def get_base_path(self) -> str:
        return self._db.get_base_path()

    def get_data_path(self) -> str:
        return self._db.get_data_path()

    def freeze(self):
        self._db.freeze()

    def is_frozen(self) -> bool:
        return self._db.is_frozen()

    def load_meta(self):
        return self._db.load_meta()

    @staticmethod
    def load_meta_from_path(base_path: str):
        """静态读 _DB_META，不构造 Database 实例（不触发 DataService register）。

        用于 merge_db 等场景：在已 open_db 的进程内读 meta 而不重复注册 base_path。
        """
        from _fly_storage import ex_stg_load_meta_from_path
        return ex_stg_load_meta_from_path(base_path)

    def reset(self):
        self._db.reset()

    def remove_object(self, name: str):
        self._db.remove_object(name)

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

    def __repr__(self):
        return f"Database(db_id={self.get_db_id()})"
