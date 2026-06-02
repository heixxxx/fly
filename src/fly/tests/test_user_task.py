import sys
import os
import pickle

try:
    import cloudpickle
except ImportError:
    cloudpickle = None

try:
    from agent.executor import create_executor
except ImportError:
    from executor import create_executor


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


def _make_func_in_main_module(func, name=None):
    func.__module__ = "__main__"
    if name:
        func.__name__ = name
    return func


def _make_func_in_module(func, module, name=None):
    func.__module__ = module
    if name:
        func.__name__ = name
    return func


def _user_add(a, b):
    return a + b


def _user_greet():
    return "hello"


def _user_log_task(msg):
    return f"logged:{msg}"


def _user_fail():
    raise RuntimeError("intentional failure")


def _user_identity(x):
    return x


def _user_concat(a, b, c):
    return f"{a}-{b}-{c}"


def _user_named_task(db):
    db.write_object("k", "v")


def _user_my_task(db, key, value):
    db.write_object(key, value)


def _user_repo_task(db, key):
    db.write_object(key, "value")


def _user_task_reg(db):
    db.write_object("k", "v")


def _user_bad_task(db):
    pass


def test_as_task_user_script_serializes_func():
    from task.py.task import as_task

    _make_func_in_main_module(_user_my_task, "my_task")
    decorated = as_task()(_user_my_task)

    assert decorated._fly_original_func is _user_my_task
    assert decorated._fly_task_name == "my_task"
    print("  PASS: test_as_task_user_script_serializes_func")


def test_as_task_user_script_unpickleable_raises():
    from task.py.task import as_task

    class _Unpickleable:
        def __reduce__(self):
            raise TypeError("cannot pickle this")

    bad_obj = _Unpickleable()

    def _task_with_bad_closure(db):
        return bad_obj

    _task_with_bad_closure.__module__ = "__main__"

    try:
        as_task()(_task_with_bad_closure)
        assert False, "Should have raised ValueError"
    except (ValueError, TypeError):
        print("  PASS: test_as_task_user_script_unpickleable_raises")


def test_as_task_repo_module_registers_in_registry():
    from task.py.task import as_task, _task_registry

    _make_func_in_module(_user_repo_task, "my_repo_module", "repo_task")
    decorated = as_task()(_user_repo_task)

    assert ("my_repo_module", "repo_task") in _task_registry
    assert _task_registry[("my_repo_module", "repo_task")] is _user_repo_task
    assert decorated._fly_original_func is _user_repo_task
    assert decorated._fly_task_name == "repo_task"

    del _task_registry[("my_repo_module", "repo_task")]
    print("  PASS: test_as_task_repo_module_registers_in_registry")


def test_as_task_user_script_does_not_register():
    from task.py.task import as_task, _task_registry

    _make_func_in_main_module(_user_task_reg, "user_task")
    before_keys = set(_task_registry.keys())

    as_task()(_user_task_reg)

    after_keys = set(_task_registry.keys())
    new_keys = after_keys - before_keys
    assert len(new_keys) == 0, f"Unexpected registry entries: {new_keys}"
    print("  PASS: test_as_task_user_script_does_not_register")


def test_as_task_task_name_preserved():
    from task.py.task import as_task, task_name

    _make_func_in_main_module(_user_named_task, "original_name")
    inner = as_task()(_user_named_task)
    outer = task_name("custom_name")(inner)
    assert outer._fly_task_name == "custom_name"
    print("  PASS: test_as_task_task_name_preserved")


def test_executor_from_user_basic():
    worker = MockWorker()
    executor = create_executor(worker)

    payload = "__user_func__:" + cloudpickle.dumps(_user_add).hex()
    pickled_args = [pickle.dumps(3).hex(), pickle.dumps(4).hex()]

    result = executor(
        task_id=101,
        task_name=payload,
        task_module="from_user",
        args=pickled_args,
    )

    assert result['task_id'] == 101
    assert result['status'] == 0, f"Expected success, got error: {result['error']}"
    assert result['output'] == "7"
    print("  PASS: test_executor_from_user_basic")


def test_executor_from_user_no_args():
    worker = MockWorker()
    executor = create_executor(worker)

    payload = "__user_func__:" + cloudpickle.dumps(_user_greet).hex()

    result = executor(
        task_id=102,
        task_name=payload,
        task_module="from_user",
        args=[],
    )

    assert result['task_id'] == 102
    assert result['status'] == 0
    assert result['output'] == "hello"
    print("  PASS: test_executor_from_user_no_args")


def test_executor_from_user_with_side_effects():
    worker = MockWorker()
    executor = create_executor(worker)

    payload = "__user_func__:" + cloudpickle.dumps(_user_log_task).hex()
    pickled_args = [pickle.dumps("test_message").hex()]

    result = executor(
        task_id=103,
        task_name=payload,
        task_module="from_user",
        args=pickled_args,
    )

    assert result['status'] == 0
    assert result['output'] == "logged:test_message"
    print("  PASS: test_executor_from_user_with_side_effects")


def test_executor_from_user_raises_exception():
    worker = MockWorker()
    executor = create_executor(worker)

    payload = "__user_func__:" + cloudpickle.dumps(_user_fail).hex()

    result = executor(
        task_id=104,
        task_name=payload,
        task_module="from_user",
        args=[],
    )

    assert result['task_id'] == 104
    assert result['status'] == 1
    assert "intentional failure" in result['error']
    assert "RuntimeError" in result['error']
    print("  PASS: test_executor_from_user_raises_exception")


def test_executor_from_user_missing_prefix():
    worker = MockWorker()
    executor = create_executor(worker)

    result = executor(
        task_id=105,
        task_name="just_a_plain_name",
        task_module="from_user",
        args=[],
    )

    assert result['task_id'] == 105
    assert result['status'] == 1
    assert "lacks serialized payload" in result['error']
    print("  PASS: test_executor_from_user_missing_prefix")


def test_executor_from_user_corrupt_payload():
    worker = MockWorker()
    executor = create_executor(worker)

    result = executor(
        task_id=106,
        task_name="__user_func__:zzzz_not_hex",
        task_module="from_user",
        args=[],
    )

    assert result['task_id'] == 106
    assert result['status'] == 1
    print("  PASS: test_executor_from_user_corrupt_payload")


def test_executor_from_user_unpickleable_payload():
    worker = MockWorker()
    executor = create_executor(worker)

    bad_hex = b"not_a_pickle".hex()
    result = executor(
        task_id=107,
        task_name=f"__user_func__:{bad_hex}",
        task_module="from_user",
        args=[],
    )

    assert result['task_id'] == 107
    assert result['status'] == 1
    print("  PASS: test_executor_from_user_unpickleable_payload")


def test_executor_from_user_preserves_original_function():
    worker = MockWorker()
    executor = create_executor(worker)

    payload = "__user_func__:" + cloudpickle.dumps(_user_identity).hex()
    pickled_args = [pickle.dumps(42).hex()]

    result = executor(
        task_id=108,
        task_name=payload,
        task_module="from_user",
        args=pickled_args,
    )

    assert result['status'] == 0
    assert result['output'] == "42"
    print("  PASS: test_executor_from_user_preserves_original_function")


def test_executor_repo_module_still_works():
    worker = MockWorker()
    executor = create_executor(worker)

    result = executor(
        task_id=109,
        task_name="simple_task",
        task_module="test_executor_tasks",
        args=[],
    )

    assert result['task_id'] == 109
    assert result['status'] == 0
    print("  PASS: test_executor_repo_module_still_works")


def test_executor_from_user_with_mixed_args():
    worker = MockWorker()
    executor = create_executor(worker)

    payload = "__user_func__:" + cloudpickle.dumps(_user_concat).hex()
    pickled_args = [
        pickle.dumps("hello").hex(),
        pickle.dumps(42).hex(),
        pickle.dumps([1, 2]).hex(),
    ]

    result = executor(
        task_id=110,
        task_name=payload,
        task_module="from_user",
        args=pickled_args,
    )

    assert result['status'] == 0
    assert result['output'] == "hello-42-[1, 2]"
    print("  PASS: test_executor_from_user_with_mixed_args")


def _run_all():
    tests = [
        test_as_task_user_script_serializes_func,
        test_as_task_user_script_unpickleable_raises,
        test_as_task_repo_module_registers_in_registry,
        test_as_task_user_script_does_not_register,
        test_as_task_task_name_preserved,
        test_executor_from_user_basic,
        test_executor_from_user_no_args,
        test_executor_from_user_with_side_effects,
        test_executor_from_user_raises_exception,
        test_executor_from_user_missing_prefix,
        test_executor_from_user_corrupt_payload,
        test_executor_from_user_unpickleable_payload,
        test_executor_from_user_preserves_original_function,
        test_executor_repo_module_still_works,
        test_executor_from_user_with_mixed_args,
    ]

    passed = 0
    failed = 0
    for test in tests:
        try:
            test()
            passed += 1
        except Exception as e:
            failed += 1
            print(f"  FAIL: {test.__name__}: {e}")
            import traceback
            traceback.print_exc()

    print(f"\n{'='*60}")
    print(f"Results: {passed} passed, {failed} failed, {passed + failed} total")
    if failed:
        print("FAILED")
        sys.exit(1)
    print("ALL PASSED")


if __name__ == "__main__":
    _run_all()
