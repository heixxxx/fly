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
# 仓库根的 bazel-bin（qa/unit → qa → 仓库根；修复历史少一级导致的注入失效）
_bazel_bin = os.path.join(os.path.dirname(__file__), '..', '..', 'bazel-bin', 'src')
for _subpath in ['storage/export', 'core/export', 'log/export', 'agent/export',
                 'network/export', 'task/export', 'message/export']:
    _full = os.path.join(_bazel_bin, _subpath)
    if os.path.exists(_full):
        sys.path.insert(0, _full)

# Add src directory to path for fly package discovery
_fly_src = os.path.join(os.path.dirname(__file__), '..', '..', 'src')
if os.path.exists(_fly_src):
    sys.path.insert(0, _fly_src)

import fly.runtime as _rt
_rt._mode = "worker"


def _drain():
    """排空异步写回队列（裸 pytest 进程无 fly task 收尾的统一 drain——真实
    环境由 executor postprocess 排空）。不排空则写后立读 TIER1 盘 miss，
    TIER2 又无 streaming handler（无 agent），误报 corruption/not-found。"""
    from _fly_storage import ex_stg_get_data_service
    ex_stg_get_data_service().drain_write_back()


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
    # 测试间单例复位（issue：全文件跑 corruption 排查，2026-09-04）：
    # ① gc 收网析构测试局部 Database（drain + unregister）；② StorageManager
    # 容器清空（其 Database 析构同样走 drain/unregister）；③ DataService
    # 键控状态全清（local_idx_/migrated_db_paths_ 每测试泄僵尸条目的载体；
    # WBQ 停止后由下次写入惰性重启）。
    gc.collect()
    from _fly_storage import ex_stg_get_storage_manager, ex_stg_get_data_service
    ex_stg_get_storage_manager().reset()
    ex_stg_get_data_service().reset_state()
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
    assert meta.created_at == 0  # default DbMeta
    assert meta.created_at == 0


def test_database_write_read(temp_dir):
    from _fly_storage import ex_stg_create_database, ex_stg_open_read_stream
    db = ex_stg_create_database(temp_dir, "", 0)
    # 恒流式写读（T2b 2026-08-31：_write_pickle_bytes/_read_decompressed 已删，
    # 与生产 write_object/read_object 同路径）
    stream = db.open_write_stream("test/key", "str")
    pickle.dump("hello world", stream)
    assert int(stream.finish_and_commit(False, False)) == 0
    rstream = ex_stg_open_read_stream(db, "test/key", False)
    assert pickle.Unpickler(rstream).load() == "hello world"
    db.reset()


def test_database_freeze(temp_dir):
    from _fly_storage import ex_stg_create_database, EXStgWriteErrorType
    db = ex_stg_create_database(temp_dir, "", 0)
    # 恒流式写（T2b 2026-08-31：_write_pickle_bytes 已删）
    stream = db.open_write_stream("test/key", "str")
    pickle.dump("data", stream)
    assert int(stream.finish_and_commit(False, False)) == 0
    db.freeze()

    assert db.is_frozen() == True

    # After freeze, open_write_stream returns None (production path raises
    # "Database is frozen"); finish path therefore unreachable — assert None.
    assert db.open_write_stream("test/key2", "str") is None, \
        "open_write_stream on frozen db should return None"

    db.reset()


def test_database_getters(temp_dir):
    from _fly_storage import ex_stg_create_database
    data_path = temp_dir + "/data"
    os.makedirs(data_path, exist_ok=True)
    db = ex_stg_create_database(temp_dir, data_path, 0)

    assert db.get_db_path() == temp_dir
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
    from _fly_storage import EXStgIndexEntry, ex_stg_get_data_service

    db = open_db(temp_dir)

    entry = EXStgIndexEntry("test/entry", "data.dat", 100, 512, False, 0)
    db.write_object("test/entry", entry)
    _drain()

    result = db.read_object("test/entry")
    assert isinstance(result, EXStgIndexEntry)
    assert result.object_name == "test/entry"
    assert result.file_name == "data.dat"
    assert result.offset == 100
    assert result.size == 512
    assert result.is_large == False
    assert result.block_count == 0

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

    entry = EXStgIndexEntry("cpp/data", "", 99, 0, False, 0)
    db.write_object("cpp/data", entry)

    py_obj = PythonTaskData(7, "py_data")
    db.write_object("py/data", py_obj)
    _drain()

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

    entry = EXStgIndexEntry("test", "", 42, 0, False, 0)

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

    entry = EXStgIndexEntry("idx/1", "", 0, 0, False, 0)
    db.write_object("idx/1", entry)

    wi = EXStgWorkerInfo(10, "writer-10", "worker-10", "", "")
    db.write_object("worker/10", wi)
    _drain()

    r1 = db.read_object("idx/1")
    assert isinstance(r1, EXStgIndexEntry)

    r2 = db.read_object("worker/10")
    assert isinstance(r2, EXStgWorkerInfo)
    assert r2.worker_id == 10
    assert r2.hostname == "worker-10"

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
    """Python creates db → writes EXStgIndexEntry via streaming → reads back"""
    from _fly_storage import ex_stg_create_database, EXStgIndexEntry

    db = ex_stg_create_database(temp_dir, "", 0)

    entry = EXStgIndexEntry("cross/cpp_entry", "cpp_generated.dat", 12345, 67890, False, 0)
    entry._write_to_db(db, "cross/cpp_entry", "EXStgIndexEntry", False)

    # Python reads via the authoritative typed path（_read_streaming 已删，
    # T2b 2026-08-31——read_object 对 C++ 对象走 _read_from_db 重建）
    result = EXStgIndexEntry._read_from_db(db, "cross/cpp_entry", "none")
    assert result.object_name == "cross/cpp_entry"
    assert result.file_name == "cpp_generated.dat"
    assert result.offset == 12345
    assert result.size == 67890
    assert result.is_large == False
    assert result.block_count == 0

    db.reset()


def test_cpp_writes_python_reads_via_flydatabase(temp_dir):
    """Python creates FlyDatabase → writes via streaming → FlyDatabase.read_object reads back"""
    from fly import open_db
    from _fly_storage import EXStgIndexEntry

    db = open_db(temp_dir)

    entry = EXStgIndexEntry("cross/fly_entry", "cpp_generated.dat", 12345, 67890, False, 0)
    entry._write_to_db(db._db, "cross/fly_entry", "EXStgIndexEntry", False)
    _drain()

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
