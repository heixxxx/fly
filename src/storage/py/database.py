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
        if hasattr(obj, "_write_to_db"):
            err = EXStgWriteErrorType(obj._write_to_db(self._db, name, type(obj).__name__, backup))
        else:
            data = pickle.dumps(obj)
            err = EXStgWriteErrorType(self._db._write_pickle_bytes(name, data, type(obj).__name__, backup))

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
        compressed = self._db._compress_pickle_bytes(data, py_name)
        self._db._put_temp_data(name, compressed)
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

    def get_obj_name(self, name: str) -> str:
        return self._db.get_obj_name(name)

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

    def reset(self):
        self._db.reset()

    def remove_object(self, name: str):
        self._db._remove_temp(name)
        self._db.remove_object(name)

    def __repr__(self):
        return f"Database(db_id={self.get_db_id()})"
