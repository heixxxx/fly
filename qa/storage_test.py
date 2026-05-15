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
_bazel_bin = os.path.join(os.path.dirname(__file__), '..', 'bazel-bin', 'src', 'storage', 'export')
if os.path.exists(_bazel_bin):
    sys.path.insert(0, _bazel_bin)

# Add source path for FlyDatabase wrapper discovery
_storage_py = os.path.join(os.path.dirname(__file__), '..', 'src', 'storage', 'py')
if os.path.exists(_storage_py):
    sys.path.insert(0, _storage_py)


class PythonTaskData:
    """Pure Python class for FlyDatabase Python object write/read tests"""
    def __init__(self, value=0, name="", tags=None):
        self.value = value
        self.name = name
        self.tags = tags or []


@pytest.fixture
def temp_dir():
    d = tempfile.mkdtemp(prefix='fly_test_storage_')
    yield d
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
    db = ex_stg_create_database(temp_dir, "")
    db.write_object("test/key", "hello world")
    data = db.read_object("test/key")
    assert data == "hello world"
    db.reset()


def test_database_freeze(temp_dir):
    from _fly_storage import ex_stg_create_database
    db = ex_stg_create_database(temp_dir, "")
    db.write_object("test/key", "data")
    db.freeze()

    assert db.is_frozen() == True

    with pytest.raises(RuntimeError):
        db.write_object("test/key2", "more data")

    db.reset()


def test_database_getters(temp_dir):
    from _fly_storage import create_database
    db = create_database(temp_dir, "/tmp/data")

    assert db.get_base_path() == temp_dir
    assert db.get_data_path() == "/tmp/data"

    db.reset()


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


# ─── FlyDatabase typed path tests ───

def test_fly_database_cpp_class_write_read(temp_dir):
    """C++ exported class (EXStgIndexEntry) -> FlyDatabase -> bitsery -> storage roundtrip"""
    from database import FlyDatabase
    from _fly_storage import EXStgIndexEntry

    db = FlyDatabase(temp_dir)

    entry = EXStgIndexEntry()
    entry.object_name = "test/entry"
    entry.file_name = "data.dat"
    entry.offset = 100
    entry.size = 512
    entry.is_large = False
    entry.block_count = 0

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
    """EXStgDbMeta (with nested EXStgWorkerInfo vector) -> FlyDatabase roundtrip"""
    from database import FlyDatabase
    from _fly_storage import EXStgDbMeta, EXStgWorkerInfo

    db = FlyDatabase(temp_dir)

    meta = EXStgDbMeta()
    meta.db_id = "/test/db"
    meta.base_path = "/test"
    meta.created_at = 1000
    meta.frozen_at = 2000

    worker = EXStgWorkerInfo()
    worker.worker_id = 1
    worker.host = "node1"
    worker.role = "hybrid"
    worker.data_path = "/data"
    worker.idx_file = "w1.idx"
    worker.idx_entry_count = 100
    meta.workers.append(worker)

    db.write_object("test/meta", meta)

    result = db.read_object("test/meta")
    assert isinstance(result, EXStgDbMeta)
    assert result.db_id == "/test/db"
    assert result.created_at == 1000
    assert len(result.workers) == 1
    assert result.workers[0].worker_id == 1
    assert result.workers[0].host == "node1"

    db.reset()


def test_fly_database_python_class_write_read(temp_dir):
    """Pure Python class -> pickle -> FlyDatabase roundtrip"""
    from database import FlyDatabase

    db = FlyDatabase(temp_dir)

    obj = PythonTaskData(42, "test_task", ["tag1", "tag2"])
    db.write_object("task/result", obj)

    result = db.read_object("task/result")
    assert isinstance(result, PythonTaskData)
    assert result.value == 42
    assert result.name == "test_task"
    assert result.tags == ["tag1", "tag2"]

    db.reset()


def test_fly_database_mixed_cpp_python(temp_dir):
    """C++ exported class + Python class in same database"""
    from database import FlyDatabase
    from _fly_storage import EXStgIndexEntry

    db = FlyDatabase(temp_dir)

    # Write C++ type
    entry = EXStgIndexEntry()
    entry.object_name = "cpp/data"
    entry.offset = 99
    db.write_object("cpp/data", entry)

    # Write Python type
    py_obj = PythonTaskData(7, "py_data")
    db.write_object("py/data", py_obj)

    # Read C++ back
    cpp_result = db.read_object("cpp/data")
    assert isinstance(cpp_result, EXStgIndexEntry)
    assert cpp_result.object_name == "cpp/data"
    assert cpp_result.offset == 99

    # Read Python back
    py_result = db.read_object("py/data")
    assert isinstance(py_result, PythonTaskData)
    assert py_result.value == 7
    assert py_result.name == "py_data"

    db.reset()


def test_fly_database_is_cpp_marker_present(temp_dir):
    """C++ exported classes have is_cpp=True marker"""
    from _fly_storage import EXStgIndexEntry, EXStgDbMeta, EXStgWorkerInfo

    for cls in [EXStgIndexEntry, EXStgDbMeta, EXStgWorkerInfo]:
        assert hasattr(cls, "is_cpp"), f"{cls.__name__} missing is_cpp"
        assert cls.is_cpp is True


def test_fly_database_python_class_missing_is_cpp(temp_dir):
    """Pure Python classes lack is_cpp marker"""
    assert not hasattr(PythonTaskData, "is_cpp")


def test_fly_database_cpp_getstate_setstate(temp_dir):
    """C++ exported class __getstate__ / __setstate__ work from Python"""
    from _fly_storage import EXStgIndexEntry

    entry = EXStgIndexEntry()
    entry.object_name = "test"
    entry.offset = 42

    data = entry.__getstate__()
    assert isinstance(data, bytes)
    assert len(data) > 0

    restored = EXStgIndexEntry.__new__(EXStgIndexEntry)
    restored.__setstate__(data)
    assert restored.object_name == "test"
    assert restored.offset == 42


def test_fly_database_multiple_cpp_types(temp_dir):
    """Multiple C++ types (EXStgIndexEntry, EXStgDbMeta, EXStgWorkerInfo) write/read in one db"""
    from database import FlyDatabase
    from _fly_storage import EXStgIndexEntry, EXStgDbMeta, EXStgWorkerInfo

    db = FlyDatabase(temp_dir)

    # EXStgIndexEntry
    entry = EXStgIndexEntry()
    entry.object_name = "idx/1"
    db.write_object("idx/1", entry)

    # EXStgWorkerInfo
    wi = EXStgWorkerInfo()
    wi.worker_id = 10
    wi.host = "worker-10"
    db.write_object("worker/10", wi)

    # Read both
    r1 = db.read_object("idx/1")
    assert isinstance(r1, EXStgIndexEntry)

    r2 = db.read_object("worker/10")
    assert isinstance(r2, EXStgWorkerInfo)
    assert r2.worker_id == 10
    assert r2.host == "worker-10"

    db.reset()


def test_fly_database_pickle_roundtrip(temp_dir):
    """Pure Python pickled data roundtrip through FlyDatabase"""
    from database import FlyDatabase

    db = FlyDatabase(temp_dir)

    class SmallData:
        def __init__(self, x=0, y=0):
            self.x = x
            self.y = y

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

    db = ex_stg_create_database(temp_dir, "")

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
    from database import FlyDatabase
    from _fly_storage import ex_stg_cpp_write_index_entry, EXStgIndexEntry

    db = FlyDatabase(temp_dir)

    # C++ writes an EXStgIndexEntry into the Python-created database
    ex_stg_cpp_write_index_entry(db, "cross/fly_entry")

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
