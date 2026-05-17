"""Layer 1 storage test: Database, StorageManager, EXStgIndexEntry, EXStgDbMeta, EXStgWorkerInfo integration
Tests verify the C++ storage modules work correctly when imported from Python
"""

import sys
import os
import pytest
import tempfile
import shutil
import pickle

# Add bazel-bin output to path for extension module discovery
_bazel_bin = os.path.join(os.path.dirname(__file__), '..', 'bazel-bin', 'src')
for _subpath in ['storage/export', 'core/export', 'log/export', 'agent/export', 'network/export', 'task/export']:
    _full = os.path.join(_bazel_bin, _subpath)
    if os.path.exists(_full):
        sys.path.insert(0, _full)

# Add src directory to path for fly package discovery
_fly_src = os.path.join(os.path.dirname(__file__), '..', 'src')
if os.path.exists(_fly_src):
    sys.path.insert(0, _fly_src)

import fly.runtime as _rt
_rt._mode = "worker"


class PythonTaskData:
    def __init__(self, value=0, name="", tags=None):
        self.value = value
        self.name = name
        self.tags = tags or []


class SmallData:
    def __init__(self, x=0, y=0):
        self.x = x
        self.y = y


@pytest.fixture
def temp_dir():
    d = tempfile.mkdtemp(prefix='fly_test_storage_')
    yield d
    import gc
    gc.collect()
    shutil.rmtree(d, ignore_errors=True)


@pytest.fixture
def storage_mgr():
    from _fly_storage import ex_stg_get_storage_manager
    mgr = ex_stg_get_storage_manager()
    yield mgr


# ─── C++ class direct tests ───

def test_index_entry_creation():
    from _fly_storage import EXStgIndexEntry
    entry = EXStgIndexEntry()
    assert entry.object_name == ""
    assert entry.offset == 0


def test_index_entry_attributes():
    from _fly_storage import EXStgIndexEntry
    entry = EXStgIndexEntry()
    assert hasattr(entry, 'object_name')
    assert hasattr(entry, 'file_name')
    assert hasattr(entry, 'offset')
    assert hasattr(entry, 'size')
    assert hasattr(entry, 'is_large')
    assert hasattr(entry, 'block_count')


def test_db_meta_creation():
    from _fly_storage import EXStgDbMeta
    meta = EXStgDbMeta()
    assert meta.db_id == ""
    assert meta.created_at == 0


def test_worker_info_creation():
    from _fly_storage import EXStgWorkerInfo
    info = EXStgWorkerInfo()
    assert info.worker_id == 0
    assert info.host == ""


def test_database_write_read(temp_dir):
    from _fly_storage import ex_stg_create_database
    db = ex_stg_create_database(temp_dir, "", 0)
    db.write_object_raw("test/key", "hello world")
    data = db.read_object_raw("test/key")
    assert data == "hello world"
    db.reset()


def test_database_freeze(temp_dir):
    from _fly_storage import ex_stg_create_database
    db = ex_stg_create_database(temp_dir, "", 0)
    db.write_object_raw("test/key", "data")
    db.freeze()

    assert db.is_frozen() == True

    with pytest.raises(RuntimeError):
        db.write_object_raw("test/key2", "more data")

    db.reset()


def test_database_getters(temp_dir):
    from _fly_storage import ex_stg_create_database
    data_path = temp_dir + "/data"
    os.makedirs(data_path, exist_ok=True)
    db = ex_stg_create_database(temp_dir, data_path, 0)

    assert db.get_base_path() == temp_dir
    assert db.get_data_path() == data_path


def test_storage_manager_singleton(storage_mgr):
    from _fly_storage import ex_stg_get_storage_manager
    mgr1 = ex_stg_get_storage_manager()
    mgr2 = ex_stg_get_storage_manager()
    assert mgr1 is mgr2


def test_storage_manager_database(storage_mgr, temp_dir):
    db = storage_mgr.get_or_create_database(temp_dir)
    assert db is not None

    db2 = storage_mgr.get_or_create_database(temp_dir)
    assert db is db2

    storage_mgr.close_all()


# ─── FlyDatabase typed path tests ───

def test_fly_database_cpp_class_write_read(temp_dir):
    from fly import open_db
    from _fly_storage import EXStgIndexEntry

    db = open_db(temp_dir)

    entry = EXStgIndexEntry("test/entry", "data.dat", 100, 512, False, 0, 0)
    db.write_object("test/entry", entry)

    result = db.read_object("test/entry")
    assert isinstance(result, EXStgIndexEntry)
    assert result.object_name == "test/entry"
    assert result.file_name == "data.dat"
    assert result.offset == 100
    assert result.size == 512
    assert result.is_large == False
    assert result.block_count == 0

    db.reset()


def test_fly_database_cpp_dbmeta_write_read(temp_dir):
    from fly import open_db
    from _fly_storage import EXStgDbMeta, EXStgWorkerInfo

    db = open_db(temp_dir)

    meta = EXStgDbMeta("/test/db", "/test", 1000, 2000)

    db.write_object("test/meta", meta)

    result = db.read_object("test/meta")
    assert isinstance(result, EXStgDbMeta)
    assert result.db_id == "/test/db"
    assert result.created_at == 1000

    db.reset()


def test_fly_database_python_class_write_read(temp_dir):
    """Pure Python class -> pickle -> FlyDatabase roundtrip"""
    from fly import open_db

    db = open_db(temp_dir)

    obj = PythonTaskData(42, "test_task", ["tag1", "tag2"])
    db.write_object("task/result", obj)

    result = db.read_object("task/result")
    assert isinstance(result, PythonTaskData)
    assert result.value == 42
    assert result.name == "test_task"
    assert result.tags == ["tag1", "tag2"]

    db.reset()


def test_fly_database_mixed_cpp_python(temp_dir):
    from fly import open_db
    from _fly_storage import EXStgIndexEntry

    db = open_db(temp_dir)

    entry = EXStgIndexEntry("cpp/data", "", 99, 0, False, 0, 0)
    db.write_object("cpp/data", entry)

    py_obj = PythonTaskData(7, "py_data")
    db.write_object("py/data", py_obj)

    cpp_result = db.read_object("cpp/data")
    assert isinstance(cpp_result, EXStgIndexEntry)
    assert cpp_result.object_name == "cpp/data"
    assert cpp_result.offset == 99

    py_result = db.read_object("py/data")
    assert isinstance(py_result, PythonTaskData)
    assert py_result.value == 7
    assert py_result.name == "py_data"

    db.reset()


def test_fly_database_is_cpp_marker_present(temp_dir):
    from _fly_storage import EXStgIndexEntry, EXStgDbMeta, EXStgWorkerInfo

    for cls in [EXStgIndexEntry, EXStgDbMeta, EXStgWorkerInfo]:
        obj = cls()
        assert hasattr(obj, "is_cpp"), f"{cls.__name__} missing is_cpp"
        assert obj.is_cpp is True


def test_fly_database_python_class_missing_is_cpp(temp_dir):
    """Pure Python classes lack is_cpp marker"""
    assert not hasattr(PythonTaskData, "is_cpp")


def test_fly_database_cpp_getstate_setstate(temp_dir):
    from _fly_storage import EXStgIndexEntry

    entry = EXStgIndexEntry("test", "", 42, 0, False, 0, 0)

    data = entry.__getstate__()
    assert isinstance(data, bytes)
    assert len(data) > 0

    restored = EXStgIndexEntry.__new__(EXStgIndexEntry)
    restored.__setstate__(data)
    assert restored.object_name == "test"
    assert restored.offset == 42


def test_fly_database_multiple_cpp_types(temp_dir):
    from fly import open_db
    from _fly_storage import EXStgIndexEntry, EXStgWorkerInfo

    db = open_db(temp_dir)

    entry = EXStgIndexEntry("idx/1", "", 0, 0, False, 0, 0)
    db.write_object("idx/1", entry)

    wi = EXStgWorkerInfo(10, "worker-10", "", "", "", 0, "")
    db.write_object("worker/10", wi)

    r1 = db.read_object("idx/1")
    assert isinstance(r1, EXStgIndexEntry)

    r2 = db.read_object("worker/10")
    assert isinstance(r2, EXStgWorkerInfo)
    assert r2.worker_id == 10
    assert r2.host == "worker-10"

    db.reset()


def test_fly_database_pickle_roundtrip(temp_dir):
    from fly import open_db

    db = open_db(temp_dir)

    obj = SmallData(3, 4)
    db.write_object("math/pt", obj)

    result = db.read_object("math/pt")
    assert result.x == 3
    assert result.y == 4

    db.reset()


# ─── Cross-language tests: Python → C++ → Python ───

def test_cpp_writes_python_reads_typed_object(temp_dir):
    """Python creates db → passes to C++ → C++ writes EXStgIndexEntry → Python reads back via typed path"""
    from _fly_storage import ex_stg_create_database, ex_stg_cpp_write_index_entry, EXStgIndexEntry

    db = ex_stg_create_database(temp_dir, "", 0)

    # C++ function takes Database& and writes a typed EXStgIndexEntry
    ex_stg_cpp_write_index_entry(db, "cross/cpp_entry")

    # Python reads via typed path
    data, py_name = db._read_typed("cross/cpp_entry")
    assert py_name == "EXStgIndexEntry"
    assert isinstance(data, bytes)
    assert len(data) > 0

    # Python deserializes using C++ class's __setstate__
    result = EXStgIndexEntry.__new__(EXStgIndexEntry)
    result.__setstate__(data)
    assert result.object_name == "cross/cpp_entry"
    assert result.file_name == "cpp_generated.dat"
    assert result.offset == 12345
    assert result.size == 67890
    assert result.is_large == False
    assert result.block_count == 0

    db.reset()


def test_cpp_writes_python_reads_via_flydatabase(temp_dir):
    """Python creates FlyDatabase → passes to C++ → C++ writes → FlyDatabase.read_object reads back"""
    from fly import open_db
    from _fly_storage import ex_stg_cpp_write_index_entry, EXStgIndexEntry

    db = open_db(temp_dir)

    # C++ writes an EXStgIndexEntry into the Python-created database
    ex_stg_cpp_write_index_entry(db._db, "cross/fly_entry")

    # Python reads via FlyDatabase.read_object (typed dispatch)
    result = db.read_object("cross/fly_entry")
    assert isinstance(result, EXStgIndexEntry)
    assert result.object_name == "cross/fly_entry"
    assert result.file_name == "cpp_generated.dat"
    assert result.offset == 12345
    assert result.size == 67890

    db.reset()


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
