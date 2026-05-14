"""Layer 1 storage test: Database, StorageManager, IndexEntry, DbMeta, WorkerInfo integration
Tests verify the C++ storage modules work correctly when imported from Python
"""

import sys
import os
import pytest
import tempfile
import shutil

# Add bazel-bin output to path for extension module discovery
_bazel_bin = os.path.join(os.path.dirname(__file__), '..', 'bazel-bin', 'src', 'storage', 'export')
if os.path.exists(_bazel_bin):
    sys.path.insert(0, _bazel_bin)


@pytest.fixture
def temp_dir():
    d = tempfile.mkdtemp(prefix='fly_test_storage_')
    yield d
    shutil.rmtree(d, ignore_errors=True)


@pytest.fixture
def storage_mgr():
    from _fly_storage import get_storage_manager
    mgr = get_storage_manager()
    yield mgr


def test_index_entry_creation():
    from _fly_storage import IndexEntry
    entry = IndexEntry()
    assert entry.object_name == ""
    assert entry.offset == 0


def test_index_entry_attributes():
    from _fly_storage import IndexEntry
    entry = IndexEntry()
    assert hasattr(entry, 'object_name')
    assert hasattr(entry, 'file_name')
    assert hasattr(entry, 'offset')
    assert hasattr(entry, 'size')
    assert hasattr(entry, 'is_large')
    assert hasattr(entry, 'block_count')


def test_db_meta_creation():
    from _fly_storage import DbMeta
    meta = DbMeta()
    assert meta.db_id == ""
    assert meta.created_at == 0


def test_worker_info_creation():
    from _fly_storage import WorkerInfo
    info = WorkerInfo()
    assert info.worker_id == 0
    assert info.host == ""


def test_database_write_read(temp_dir):
    from _fly_storage import create_database
    db = create_database(temp_dir, "")
    db.write_object("test/key", "hello world")
    data = db.read_object("test/key")
    assert data == "hello world"
    db.reset()


def test_database_freeze(temp_dir):
    from _fly_storage import create_database
    db = create_database(temp_dir, "")
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
    from _fly_storage import get_storage_manager
    mgr1 = get_storage_manager()
    mgr2 = get_storage_manager()
    assert mgr1 is mgr2


def test_storage_manager_database(storage_mgr, temp_dir):
    db = storage_mgr.get_or_create_database(temp_dir)
    assert db is not None

    db2 = storage_mgr.get_or_create_database(temp_dir)
    assert db is db2


if __name__ == "__main__":
    pytest.main([__file__, "-v"])