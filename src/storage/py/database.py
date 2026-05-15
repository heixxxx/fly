import pickle
from _fly_storage import EXStgDatabase as _CDatabase


class FlyDatabase(_CDatabase):
    """Python 侧 Database 包装，自动分派 C++ 导出类型 / Python 原生类型"""

    def write_object(self, name: str, obj) -> str:
        if hasattr(obj, "is_cpp"):
            return self._write_typed(name, obj.__getstate__(), type(obj).__name__)
        else:
            return self._write_typed(name, pickle.dumps(obj, -1), type(obj).__name__)

    def read_object(self, name: str):
        data, py_name = self._read_typed(name)
        import _fly_storage
        cls = getattr(_fly_storage, py_name, None)
        if cls is not None and hasattr(cls, "is_cpp"):
            obj = cls.__new__(cls)
            obj.__setstate__(data)
            return obj
        else:
            return pickle.loads(data)