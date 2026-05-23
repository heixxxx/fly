"""Unit tests for fly.executor module."""
import sys
import os
import pickle
import shutil
import tempfile
import uuid

_this_dir = os.path.dirname(os.path.abspath(__file__))
_project_root = os.path.normpath(os.path.join(_this_dir, '..', '..', '..'))
_bazel_bin = os.path.join(_project_root, 'bazel-bin', 'src')

for _subpath in ['agent/export', 'storage/export', 'log/export', 'core/export']:
    _full = os.path.join(_bazel_bin, _subpath)
    if os.path.exists(_full):
        sys.path.insert(0, _full)

sys.path.insert(0, os.path.join(_project_root, 'src'))

sys.path.insert(0, _this_dir)

try:
    from agent.executor import create_executor, _deserialize_args
except ImportError:
    from executor import create_executor, _deserialize_args
try:
    from storage.database import _Database
except ImportError:
    from database import _Database
import _fly_log as log
import _fly_storage as storage


def _unique_id():
    return uuid.uuid4().hex[:8]


class MockWorker:
    def __init__(self, worker_id=1):
        self._worker_id = worker_id
        self._db_cache = {}
        self._agent = MockAgent()


class MockAgent:
    def __init__(self):
        self.registered_dbs = []
    
    def register_database(self, db_id, db):
        self.registered_dbs.append(db_id)


def setup_module():
    if os.path.exists("test_executor_logs"):
        shutil.rmtree("test_executor_logs")
    log.init_log("test_executor_logs", 0)


def teardown_module():
    import gc
    gc.collect()
    try:
        sm = storage.ex_stg_get_storage_manager()
        sm.close_all()
    except Exception:
        pass
    log.shutdown_log()


def test_deserialize_pickle_args():
    worker = MockWorker()
    
    original = {"key": "value", "num": 42}
    pickled = pickle.dumps(original).hex()
    
    args = [pickled, pickle.dumps([1, 2, 3]).hex()]
    result = _deserialize_args(args, worker)
    
    assert result[0] == original
    assert result[1] == [1, 2, 3]


def test_deserialize_fly_db_marker():
    worker = MockWorker()
    
    db_marker = "__fly_db__:test_db_marker:/tmp/fly_test_marker_db:"
    args = [db_marker]
    
    result = _deserialize_args(args, worker)
    
    assert len(result) == 1
    assert isinstance(result[0], _Database)
    assert "test_db_marker" in worker._db_cache
    assert "test_db_marker" in worker._agent.registered_dbs
    
    result[0]._db.reset()
    del result[0]
    del worker._db_cache["test_db_marker"]


def test_deserialize_fly_db_with_data_path():
    worker = MockWorker()
    
    temp_dir = tempfile.mkdtemp(prefix="test_executor_base_")
    data_dir = tempfile.mkdtemp(prefix="test_executor_data_")
    db_id = f"my_db_{_unique_id()}"
    
    db_marker = f"__fly_db__:{db_id}:{temp_dir}:{data_dir}"
    args = [db_marker]
    
    result = _deserialize_args(args, worker)
    
    assert isinstance(result[0], _Database)
    assert db_id in worker._db_cache
    
    for db_id_key, db_obj in worker._db_cache.items():
        db_obj._db.reset()
    worker._db_cache.clear()


def test_deserialize_cached_db():
    worker = MockWorker()
    
    temp_dir = tempfile.mkdtemp(prefix="test_executor_cached_")
    db_id = f"cached_db_{_unique_id()}"
    
    db_marker = f"__fly_db__:{db_id}:{temp_dir}:"
    
    args1 = [db_marker]
    result1 = _deserialize_args(args1, worker)
    
    args2 = [db_marker]
    result2 = _deserialize_args(args2, worker)
    
    assert result1[0] is result2[0]
    assert len(worker._agent.registered_dbs) == 1
    
    for db_id_key, db_obj in worker._db_cache.items():
        db_obj._db.reset()
    worker._db_cache.clear()


def test_deserialize_mixed_args():
    worker = MockWorker()
    
    temp_dir = tempfile.mkdtemp(prefix="test_executor_mixed_")
    db_id = f"mixed_db_{_unique_id()}"
    
    pickle_arg = pickle.dumps({"data": "test"}).hex()
    db_marker = f"__fly_db__:{db_id}:{temp_dir}:"
    
    args = [pickle_arg, db_marker, pickle.dumps(123).hex()]
    result = _deserialize_args(args, worker)
    
    assert result[0] == {"data": "test"}
    assert isinstance(result[1], _Database)
    assert result[2] == 123
    
    for db_id_key, db_obj in worker._db_cache.items():
        db_obj._db.reset()
    worker._db_cache.clear()


def test_create_executor_returns_callable():
    worker = MockWorker()
    executor = create_executor(worker)
    
    assert callable(executor)


def test_executor_successful_execution():
    worker = MockWorker()
    executor = create_executor(worker)
    
    result = executor(
        task_id=1,
        task_name="simple_task",
        task_module="test_executor_tasks",
        args=[]
    )
    
    assert result['task_id'] == 1
    assert result['status'] == 0
    assert result['error'] == ''
    assert result['status'] == 0
    assert result['error'] == ''


def test_executor_module_import_failure():
    worker = MockWorker()
    executor = create_executor(worker)
    
    result = executor(
        task_id=2,
        task_name="some_func",
        task_module="nonexistent_module",
        args=[]
    )
    
    assert result['task_id'] == 2
    assert result['status'] == 1
    assert "nonexistent_module" in result['error']


def test_executor_function_not_found():
    worker = MockWorker()
    executor = create_executor(worker)
    
    result = executor(
        task_id=3,
        task_name="nonexistent_func",
        task_module="test_executor_tasks",
        args=[]
    )
    
    assert result['task_id'] == 3
    assert result['status'] == 1
    assert "nonexistent_func" in result['error']


def test_executor_with_args():
    worker = MockWorker()
    executor = create_executor(worker)
    
    pickled_args = [pickle.dumps([10, 20]).hex()]
    
    result = executor(
        task_id=4,
        task_name="add_numbers",
        task_module="test_executor_tasks",
        args=pickled_args
    )
    
    assert result['task_id'] == 4
    assert result['status'] == 0
    assert result['output'] == "30"


def test_executor_exception_handling():
    worker = MockWorker()
    executor = create_executor(worker)
    
    result = executor(
        task_id=5,
        task_name="raising_task",
        task_module="test_executor_tasks",
        args=[]
    )
    
    assert result['task_id'] == 5
    assert result['status'] == 1
    assert "RuntimeError" in result['error']
    assert "test_executor_tasks" in result['error']


def test_executor_freeze_detection():
    worker = MockWorker()
    executor = create_executor(worker)
    
    temp_dir = tempfile.mkdtemp(prefix="test_executor_freeze_")
    db_id = f"freeze_db_{_unique_id()}"
    
    db_marker = f"__fly_db__:{db_id}:{temp_dir}:"
    pickled_args = [db_marker]
    
    result = executor(
        task_id=6,
        task_name="freeze_task",
        task_module="test_executor_tasks",
        args=pickled_args
    )
    
    assert result['task_id'] == 6
    assert result['status'] == 0
    assert db_id in result['frozen_dbs']
    
    for db_id_key, db_obj in worker._db_cache.items():
        db_obj._db.reset()
    worker._db_cache.clear()


if __name__ == "__main__":
    setup_module()
    
    test_deserialize_pickle_args()
    test_deserialize_fly_db_marker()
    test_deserialize_fly_db_with_data_path()
    test_deserialize_cached_db()
    test_deserialize_mixed_args()
    
    test_create_executor_returns_callable()
    test_executor_successful_execution()
    test_executor_module_import_failure()
    test_executor_function_not_found()
    test_executor_with_args()
    test_executor_exception_handling()
    test_executor_freeze_detection()
    
    teardown_module()
    print("\nAll executor tests passed!")