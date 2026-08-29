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

from agent import create_executor, deserialize_args
from storage import Database
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
    
    def register_database(self, db_path, db):
        self.registered_dbs.append(db_path)


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
    result = deserialize_args(args, worker)
    
    assert result[0] == original
    assert result[1] == [1, 2, 3]


def test_deserialize_fly_db_marker():
    worker = MockWorker()

    marker_dir = tempfile.mkdtemp(prefix="test_executor_marker_")
    db_marker = f"__fly_db__:{marker_dir}:"
    args = [db_marker]

    result = deserialize_args(args, worker)

    assert len(result) == 1
    assert isinstance(result[0], Database)
    # cache key == db_path（db_path == db_path，不再单独传）
    assert marker_dir in worker._db_cache
    assert marker_dir in worker._agent.registered_dbs

    result[0]._db.reset()
    del result[0]
    del worker._db_cache[marker_dir]
    shutil.rmtree(marker_dir, ignore_errors=True)


def test_deserialize_fly_db_with_data_path():
    worker = MockWorker()

    temp_dir = tempfile.mkdtemp(prefix="test_executor_base_")
    data_dir = tempfile.mkdtemp(prefix="test_executor_data_")

    db_marker = f"__fly_db__:{temp_dir}:{data_dir}"
    args = [db_marker]

    result = deserialize_args(args, worker)

    assert isinstance(result[0], Database)
    # cache key == db_path（temp_dir）
    assert temp_dir in worker._db_cache

    for db_path_key, db_obj in worker._db_cache.items():
        db_obj._db.reset()
    worker._db_cache.clear()


def test_deserialize_fly_db2_reads_data_path_from_meta():
    """v2 编码 __fly_db2__:{uid}:{db_path}：data_path 从 _DB_META 读取。

    db 级属性（data_path）不再随参数携带——worker 端从 meta 获取（同一次
    读盘取 role，零新增 IO）。
    """
    worker = MockWorker()

    temp_dir = tempfile.mkdtemp(prefix="test_executor_v2_")
    data_dir = tempfile.mkdtemp(prefix="test_executor_v2data_")

    from storage import DbMetaFile, make_meta
    DbMetaFile(temp_dir).write_new(
        make_meta("uid_v2test", "test", "test", data_path=data_dir))

    db_marker = f"__fly_db2__:uid_v2test:{temp_dir}"
    result = deserialize_args([db_marker], worker)

    assert isinstance(result[0], Database)
    # uid 与 db_path 双 key 注册
    assert "uid_v2test" in worker._db_cache
    assert temp_dir in worker._db_cache
    # data_path 取自 _DB_META（参数未携带）
    assert result[0].get_data_path() == data_dir

    for db_path_key, db_obj in worker._db_cache.items():
        db_obj._db.reset()
    worker._db_cache.clear()


def test_deserialize_fly_db2_meta_missing_falls_back_empty():
    """v2 编码但 _DB_META 缺失（异常场景）：data_path 退化空（自包含）。"""
    worker = MockWorker()

    temp_dir = tempfile.mkdtemp(prefix="test_executor_v2nometa_")

    db_marker = f"__fly_db2__:uid_nometa:{temp_dir}"
    result = deserialize_args([db_marker], worker)

    assert isinstance(result[0], Database)
    # 空 data_path = 自包含（正式数据落 db_path，由 DataWriter 写层解释；
    # getter 返回空串而非 db_path）。
    assert result[0].get_data_path() == ""

    for db_path_key, db_obj in worker._db_cache.items():
        db_obj._db.reset()
    worker._db_cache.clear()


def test_deserialize_cached_db():
    worker = MockWorker()
    
    temp_dir = tempfile.mkdtemp(prefix="test_executor_cached_")
    
    db_marker = f"__fly_db__:{temp_dir}:"
    
    args1 = [db_marker]
    result1 = deserialize_args(args1, worker)
    
    args2 = [db_marker]
    result2 = deserialize_args(args2, worker)
    
    assert result1[0] is result2[0]
    assert len(worker._agent.registered_dbs) == 1
    
    for db_path_key, db_obj in worker._db_cache.items():
        db_obj._db.reset()
    worker._db_cache.clear()


def test_deserialize_mixed_args():
    worker = MockWorker()
    
    temp_dir = tempfile.mkdtemp(prefix="test_executor_mixed_")
    
    pickle_arg = pickle.dumps({"data": "test"}).hex()
    db_marker = f"__fly_db__:{temp_dir}:"
    
    args = [pickle_arg, db_marker, pickle.dumps(123).hex()]
    result = deserialize_args(args, worker)
    
    assert result[0] == {"data": "test"}
    assert isinstance(result[1], Database)
    assert result[2] == 123
    
    for db_path_key, db_obj in worker._db_cache.items():
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

    db_marker = f"__fly_db__:{temp_dir}:"
    pickled_args = [db_marker]

    result = executor(
        task_id=6,
        task_name="freeze_task",
        task_module="test_executor_tasks",
        args=pickled_args
    )

    assert result['task_id'] == 6
    assert result['status'] == 0
    # freeze 是 task 内主动行为：db.freeze() 已执行，db 对象本地 is_frozen。
    # executor 不再用差集推断 frozen_dbs（freeze 通知由 Database::freeze() 即时发 master）。
    db_obj = worker._db_cache[temp_dir]
    assert db_obj.is_frozen()

    for did_key, db_obj in worker._db_cache.items():
        db_obj._db.reset()
    worker._db_cache.clear()


def test_executor_from_user_deserialization():
    worker = MockWorker()
    executor = create_executor(worker)

    def _inline_add(a, b):
        return a + b

    payload = "__user_func__:" + pickle.dumps(_inline_add).hex()
    pickled_args = [pickle.dumps((3, 4)).hex()]

    result = executor(
        task_id=7,
        task_name=payload,
        task_module="from_user",
        args=pickled_args,
    )

    assert result['task_id'] == 7
    assert result['status'] == 0
    assert result['output'] == "7"


if __name__ == "__main__":
    setup_module()
    
    test_deserialize_pickle_args()
    test_deserialize_fly_db_marker()
    test_deserialize_fly_db_with_data_path()
    test_deserialize_fly_db2_reads_data_path_from_meta()
    test_deserialize_fly_db2_meta_missing_falls_back_empty()
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