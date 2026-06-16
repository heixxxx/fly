import pickle
import time
from _fly_storage import (
    ex_stg_get_data_service,
    ex_stg_get_last_error_type as _get_last_error_type_int,
    EXStgErrorType,
)

_MAX_RETRIES = 3
_RETRY_INTERVAL_SEC = 1.0


def _get_last_error_type():
    return EXStgErrorType(_get_last_error_type_int())


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
        EXStgErrorType.WRITE_TO_FROZEN_DB: "Write to frozen database",
        EXStgErrorType.WRITE_REGISTRATION_FAILED: "Write registration failed",
        EXStgErrorType.WRITE_REGISTRATION_TIMEOUT: "Write registration timeout",
        EXStgErrorType.WRITE_PROVENANCE_MISMATCH: "Write provenance mismatch",
    }

    def write_object(self, name: str, obj, backup: bool = False, save_to_db: bool = True) -> str:
        if not save_to_db:
            return self._write_temp(name, obj)

        if hasattr(obj, "_write_to_db"):
            result = obj._write_to_db(self._db, name, type(obj).__name__, backup)
        else:
            data = pickle.dumps(obj)
            result = self._db._write_pickle_bytes(name, data, type(obj).__name__, backup)

        if not result:
            error_type = _get_last_error_type()
            if error_type != EXStgErrorType.UNKNOWN and error_type != EXStgErrorType.WRITE_DUPLICATE_SKIPPED:
                msg = self._WRITE_ERROR_MESSAGES.get(error_type, f"Write error (type={error_type})")
                raise RuntimeError(f"{msg}: {name}")
        return result

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
        #     high tier (省反序列化). Both "low" and "high" cache modes use this.
        #   - pickle (Python) objects: "high" → Python ReadCache high tier;
        #     "low"/"none" → C++ ObjectCache low tier (transparent, via
        #     _read_streaming) + reconstruct every time.
        py_name = self._db._get_py_name(name)
        import _fly_storage
        cls = getattr(_fly_storage, py_name, None)
        is_cpp_obj = cls is not None and hasattr(cls, "_read_from_db")

        if is_cpp_obj:
            # nanobind class → C++ read_object<Cls> with high-tier cache.
            return cls._read_from_db(self._db, name)

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
            data, _ = self._db._read_streaming(name, backup)
            obj = self._reconstruct(data, py_name)
            rc.put(key, "high", obj)
            return obj

        # pickle object, cache="low"/"none": C++ low tier handles byte caching.
        data, _ = self._db._read_streaming(name, backup)
        return self._reconstruct(data, py_name)

    def backup_object(self, name: str):
        self._db.backup_object(name)

    def _reconstruct(self, data, py_name: str):
        import _fly_storage
        cls = getattr(_fly_storage, py_name, None)
        if cls is not None and hasattr(cls, "_write_to_db"):
            obj = cls.__new__(cls)
            obj.__setstate__(data)
            return obj
        raw = self._db._decompress_bytes(data)
        return pickle.loads(raw)

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
