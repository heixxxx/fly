import pickle
import time
import logging
from _fly_storage import ex_stg_get_data_service

logger = logging.getLogger("fly")

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

    def write_object(self, name: str, obj, backup: bool = False) -> str:
        from _fly_storage import FlyBuffer
        if hasattr(obj, "is_cpp") and hasattr(obj, "__getstate_buffer__"):
            buf = obj.__getstate_buffer__()
        elif hasattr(obj, "is_cpp"):
            buf = FlyBuffer()
            buf.write(obj.__getstate__())
        else:
            import pickle
            data = pickle.dumps(obj)
            return self._db._write_pickle_bytes(name, data, type(obj).__name__, backup)
        return self._db._write_buffer(name, buf, type(obj).__name__, backup)

    def read_object(self, name: str, backup: bool = False):
        data, py_name = self._db.read_raw(name, backup)
        return self._reconstruct(data, py_name)

    def backup_object(self, name: str):
        self._db.backup_object(name)

    def _reconstruct(self, data, py_name: str):
        import _fly_storage
        cls = getattr(_fly_storage, py_name, None)
        if cls is not None and hasattr(cls, "is_cpp"):
            obj = cls.__new__(cls)
            obj.__setstate__(data)
            return obj
        return pickle.loads(data)

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
        self._db.remove_object(name)

    def __repr__(self):
        return f"Database(db_id={self.get_db_id()})"
