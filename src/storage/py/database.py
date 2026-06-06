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
        else:
            data = pickle.dumps(obj)
            result = self._db._write_pickle_bytes(name, data, type(obj).__name__, False)
        self._db._mark_temp(name)
        return result

    def read_object(self, name: str, backup: bool = False, cache: str = "low"):
        if cache == "none":
            data, py_name = self._db._read_streaming(name, backup)
            return self._reconstruct(data, py_name)

        try:
            from storage.py.read_cache import get_read_cache
        except ImportError:
            from storage.read_cache import get_read_cache
        rc = get_read_cache()
        db_id = self.get_db_id()
        key = f"{db_id}:{name}"

        if cache == "high":
            obj = rc.get(key, "high")
            if obj is not None:
                return obj
            cached = rc.get(key, "low")
            if cached is not None:
                data, py_name = cached
                obj = self._reconstruct(data, py_name)
                rc.put(key, "high", obj)
                rc.remove(key, "low")
                return obj
        elif cache == "low":
            cached = rc.get(key, "low")
            if cached is not None:
                data, py_name = cached
                return self._reconstruct(data, py_name)

        data, py_name = self._db._read_streaming(name, backup)

        if cache == "high":
            obj = self._reconstruct(data, py_name)
            rc.put(key, "high", obj)
        else:
            rc.put(key, "low", (data, py_name))
            obj = self._reconstruct(data, py_name)

        return obj

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
