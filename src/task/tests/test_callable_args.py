"""callable task 参数的 cloudpickle 序列化往返测试。

worker 链式编排（如 dynamic solver 的 controller task 持有用户 update_rhs
回调）需要把函数对象作为 task 参数传到 worker。标准 pickle 无法序列化
脚本内闭包/lambda，新增 __fly_cfunc__ 标签走 cloudpickle 编解码。

验证 _serialize_args / deserialize_args 的对称性：
  - 闭包（捕获局部变量）、lambda、模块级函数三种 callable 均可往返且行为正确
  - 普通 pickle 参数路径不受影响
  - db 参数编码优先级不受影响（db 判定先于 callable 判定）
"""
import sys
import types

# stub task.py / executor.py 的 C++ 与包依赖（同 test_requires_parsing.py 模式）
_fly_storage_stub = types.ModuleType('_fly_storage')
_fly_storage_stub.ex_stg_compute_write_context_hash = lambda *a, **kw: ""
sys.modules['_fly_storage'] = _fly_storage_stub

_fly_log_stub = types.ModuleType('_fly_log')
_fly_log_stub.DBG = lambda *a, **kw: None
_fly_log_stub.INFO = lambda *a, **kw: None
_fly_log_stub.WARN = lambda *a, **kw: None
_fly_log_stub.ERR = lambda *a, **kw: None
sys.modules['_fly_log'] = _fly_log_stub

_fly_runtime_stub = types.ModuleType('fly.runtime')
_fly_runtime_stub.get_agent = lambda: None
_fly_pkg = types.ModuleType('fly')
_fly_pkg.__path__ = []
_fly_pkg.runtime = _fly_runtime_stub
sys.modules['fly'] = _fly_pkg
sys.modules['fly.runtime'] = _fly_runtime_stub

_monitor_stub = types.ModuleType('monitor')
_monitor_stub.set_current = lambda *a, **kw: None
_monitor_stub.take_result = lambda *a, **kw: None
_monitor_stub.add_drain_ms = lambda *a, **kw: None
sys.modules['monitor'] = _monitor_stub

_fly_agent_stub = types.ModuleType('_fly_agent')
_fly_agent_stub.EXTaskExecResult = object
_fly_agent_stub.EXTaskExecStatus = type('EXTaskExecStatus', (), {'OK': 0})
sys.modules['_fly_agent'] = _fly_agent_stub

_storage_stub = types.ModuleType('storage')
_storage_stub.Database = type('Database', (), {'_ROLE_REGISTRY': {}})
_storage_stub.DbMetaFile = type('DbMetaFile', (), {})
_storage_stub.get_registry = lambda: type('_R', (), {'register': lambda s, u, p: None})()
sys.modules['storage'] = _storage_stub

import importlib.util
import os

try:
    import cloudpickle  # noqa: F401
    _HAS_CP = True
except ImportError:
    _HAS_CP = False

_SRC_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..'))


def _load(name, rel):
    spec = importlib.util.spec_from_file_location(name, os.path.join(_SRC_ROOT, rel))
    mod = importlib.util.module_from_spec(spec)
    sys.modules[name] = mod
    spec.loader.exec_module(mod)
    return mod


# executor.py 顶部 `from task import ...`，故 task.py 必须以 'task' 名注册
task_mod = _load('task', 'src/task/py/task.py')
executor_mod = _load('executor_ser', 'src/agent/py/executor.py')

from task import _serialize_args
from executor_ser import deserialize_args


def _roundtrip(args):
    """序列化 + 反序列化往返（callable 参数不涉及 db，worker 传 None）。"""
    return deserialize_args(_serialize_args(args), None)


def test_closure_roundtrip():
    factor = 7

    def scale(x):
        return x * factor

    fn = _roundtrip([scale])[0]
    assert callable(fn)
    assert fn(6) == 42, f"closure capture broken: fn(6)={fn(6)}"


def test_lambda_roundtrip():
    fn = _roundtrip([lambda a, b: a + b])[0]
    assert fn(3, 4) == 7


def test_module_func_roundtrip():
    fn = _roundtrip([os.path.join])[0]
    assert fn("/a", "b") == "/a/b"


def test_callable_tag_prefix():
    def f():
        pass

    enc = _serialize_args([f])[0]
    assert isinstance(enc, str) and enc.startswith("__fly_cfunc__:"), \
        f"callable must be tagged, got: {enc[:40]}"


def test_plain_args_untouched():
    out = _roundtrip([42, "text", [1.5, 2.5]])
    assert out == [42, "text", [1.5, 2.5]]


def test_mixed_args():
    def double(x):
        return x * 2

    out = _roundtrip([double, 21, "ctx"])
    assert out[0](out[1]) == 42 and out[2] == "ctx"


def test_db_like_priority_over_callable():
    # 有 db 协议方法的对象优先走 db 编码（即使它 callable）
    class FakeDb:
        def get_db_path(self):
            return "/tmp/x"

        def get_full_name(self, n):
            return f"/tmp/x:{n}"

        def __call__(self):
            return "never"

        class _db:
            @staticmethod
            def get_db_path():
                return "/tmp/x"

            @staticmethod
            def get_data_path():
                return "/tmp/xd"

        _db = _db()

    enc = _serialize_args([FakeDb()])[0]
    assert enc.startswith("__fly_db__:"), f"db encoding must win, got {enc[:40]}"


def main():
    tests = [v for k, v in sorted(globals().items()) if k.startswith("test_")]
    ran = 0
    for t in tests:
        if not _HAS_CP and t.__name__ in (
                "test_closure_roundtrip", "test_lambda_roundtrip",
                "test_callable_tag_prefix", "test_mixed_args"):
            print(f"  [SKIP] {t.__name__} (cloudpickle unavailable outside bazel)")
            continue
        t()
        ran += 1
        print(f"  [PASS] {t.__name__}")
    print(f"callable args: {ran} tests passed")


if __name__ == "__main__":
    main()
