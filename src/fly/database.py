import pickle
import time
import logging
from _fly_storage import ex_stg_create_database, ex_stg_get_data_service

logger = logging.getLogger("fly")

_MAX_RETRIES = 3
_RETRY_INTERVAL_SEC = 1.0


class _Database:

    def __init__(self, base_path: str, data_path: str = "", writer_id: int = 0):
        from .runtime import _mode
        if _mode == "master":
            from .runtime import get_agent
            agent = get_agent()
            self._db = agent._agent.get_or_create_database(base_path, data_path, writer_id)
        else:
            self._db = ex_stg_create_database(base_path, data_path, writer_id)

    def write_object(self, name: str, obj) -> str:
        if hasattr(obj, "is_cpp"):
            data = obj.__getstate__()
        else:
            data = pickle.dumps(obj, -1)
        return self._db._write_typed(name, data, type(obj).__name__)

    def read_object(self, name: str):
        ds = ex_stg_get_data_service()

        # 1. Try local via DataService
        found, data, py_name = ds.try_read_local(name)
        if found:
            return self._reconstruct(data, py_name)

        # 2. Try remote_idx cache -> direct worker-to-worker read
        has_loc, worker_id, host, port = ds.lookup_remote_idx(name)
        if has_loc and host:
            try:
                data, py_name = self._read_from_worker(host, port, name)
                return self._reconstruct(data, py_name)
            except Exception as e:
                logger.debug(f"Remote idx read failed for '{name}': {e}, falling back to full remote")

        # 3. Full remote with retries
        last_error = None
        for attempt in range(_MAX_RETRIES):
            try:
                data, py_name = self._read_remote(name)
                return self._reconstruct(data, py_name)
            except Exception as e:
                last_error = e
                if attempt < _MAX_RETRIES - 1:
                    logger.debug(f"Remote read attempt {attempt + 1} failed for '{name}': {e}")
                    time.sleep(_RETRY_INTERVAL_SEC)

        raise RuntimeError(
            f"Failed to read object '{name}' after {_MAX_RETRIES} attempts: {last_error}")

    def _reconstruct(self, data, py_name: str):
        import _fly_storage
        cls = getattr(_fly_storage, py_name, None)
        if cls is not None and hasattr(cls, "is_cpp"):
            obj = cls.__new__(cls)
            obj.__setstate__(data)
            return obj
        return pickle.loads(data)

    def _read_remote(self, name: str):
        from .runtime import get_agent
        agent = get_agent()
        data, py_name = agent._agent.request_remote_data(name)
        return data, py_name

    def _read_from_worker(self, host: str, port: int, name: str):
        from .runtime import get_agent
        agent = get_agent()
        data, py_name = agent._agent.request_data_from_worker(host, port, name)
        return data, py_name

    def write_object_raw(self, name: str, data: str) -> str:
        return self._db.write_object_raw(name, data)

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

    def __repr__(self):
        return f"Database(db_id={self.get_db_id()})"
