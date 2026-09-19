"""as_task kwargs 序列化修复测试（2026-09-19 缺陷修复）。

缺陷：as_task wrapper 仅序列化位置参数（_serialize_args(args)），调用方
传的 **kwargs 被静默丢弃——worker 端按 (module, name) 取回原函数后仅以
位置参数调用。两种失败形态：
  - 被丢参数无默认值 → worker 端 TypeError（timing 批次实证形态）
  - 被丢参数有默认值 → 提交成功、worker 正常执行、静默拿默认值产出
    错误数据，无任何报错（更危险形态）

修复（不做版本兼容，用户裁定 2026-09-19）：
  - _serialize_args 产出 (args, kwargs) 二元组（kwargs 为编码 dict）
  - 线格式在位置编码元素后恒定追加 __fly_kwargs__ 尾段（空 kwargs 也
    追加——尾段存在性即新旧载体判别依据，无需版本号）
  - worker 端尾段缺失（旧格式载体）→ 显式报错，该任务失败透出原因
  - worker 执行改 func(*args, **kwargs)

同 test_callable_args.py 模式：stub 包依赖 + 按路径加载 task.py/executor.py；
kwargs 测试值用标量/callable（db 参数编码已由 test_callable_args 覆盖，
且元素编码同源复用）。
"""
import json
import sys
import types


# ── 包依赖 stub（同 test_callable_args.py）────────────────────────────

class _FakeDataService:
    def has_local_object(self, name):
        return False

    def has_remote_location(self, name):
        return False

    def try_read_remote(self, name):
        return (False, None, "", False)


_log_stub = types.ModuleType('log')
_log_stub.DBG = lambda *a, **kw: None
_log_stub.INFO = lambda *a, **kw: None
_log_stub.WARN = lambda *a, **kw: None
_log_stub.ERR = lambda *a, **kw: None
sys.modules['log'] = _log_stub


class _FakeAgent:
    """as_task wrapper 提交路径用假 agent（记录 submit 全部参数）。"""
    mode = "master"

    def __init__(self):
        self.submitted = []

    def submit(self, name, module, args, inputs, **kwargs):
        self.submitted.append(
            {'name': name, 'module': module, 'args': args,
             'inputs': inputs, **kwargs})


_fake_agent = _FakeAgent()

_fly_runtime_stub = types.ModuleType('fly.runtime')
_fly_runtime_stub.get_agent = lambda: _fake_agent
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

_agent_pkg_stub = types.ModuleType('agent')
_agent_pkg_stub.EXTaskExecResult = object
_agent_pkg_stub.EXTaskExecStatus = type('EXTaskExecStatus', (), {'OK': 0})
sys.modules['agent'] = _agent_pkg_stub

# write_context_hash 用确定性拼接 stub：hash 输入即 serialized 载体本身，
# 「同参数两次提交 hash 一致」断言等价于「同参数两次编码载体一致」。
_storage_stub = types.ModuleType('storage')
_storage_stub.Database = type('Database', (), {'_ROLE_REGISTRY': {}})
_storage_stub.DbMetaFile = type('DbMetaFile', (), {})
_storage_stub.make_meta = lambda *a, **kw: {}
_storage_stub.get_registry = lambda: type('_R', (), {'register': lambda s, u, p: None})()


def _fake_write_hash(task_name, module, serialized, task_inputs):
    return "|".join([task_name, module] + list(serialized) + list(task_inputs))


_storage_stub.ex_stg_compute_write_context_hash = _fake_write_hash
_storage_stub.ex_stg_get_data_service = lambda: _FakeDataService()
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

from task import _serialize_args, as_task
from executor_ser import deserialize_args

KWARGS_TAG = "__fly_kwargs__:"


def _run_on_worker(submission, wrapper):
    """worker 端消费：线格式 → deserialize → 以修复后形态调用原函数
    （executor.execute 同形态 func(*args, **kwargs)），返回原函数结果。"""
    got_args, got_kwargs = deserialize_args(submission['args'], None)
    return wrapper._fly_original_func(*got_args, **got_kwargs)


# ── 序列化形态 ────────────────────────────────────────────────────────

def test_serialize_args_returns_args_kwargs_pair():
    serialized = _serialize_args((1, "x"), {"k": 2})
    assert isinstance(serialized, tuple) and len(serialized) == 2, \
        f"_serialize_args must produce an (args, kwargs) pair, got {type(serialized)}"
    args_list, kwargs_map = serialized
    assert isinstance(args_list, list) and isinstance(kwargs_map, dict)
    assert len(args_list) == 2 and list(kwargs_map) == ["k"]


def test_submit_payload_carries_kwargs_section():
    """线格式：位置编码元素后恒定追加 kwargs 尾段（空 kwargs 也追加——
    尾段存在性即新旧载体判别依据）。"""
    @as_task()
    def sample(a, b=None):
        return None

    sample(1, b=2)
    assert _fake_agent.submitted, "task must be submitted"
    args = _fake_agent.submitted[-1]['args']
    assert args and args[-1].startswith(KWARGS_TAG), \
        f"payload must end with kwargs section, got {args}"
    _fake_agent.submitted.clear()

    # 无 kwargs 调用同样恒定追加（旧载体缺失尾段 → 显式失效的判别前提）
    sample(1)
    args = _fake_agent.submitted[-1]['args']
    assert args and args[-1].startswith(KWARGS_TAG), \
        f"empty kwargs must still emit the section, got {args}"
    _fake_agent.submitted.clear()


# ── 危险形态修复验证（worker 端收到调用值）────────────────────────────

def test_kwargs_no_default_reaches_worker():
    """原 TypeError 形态：kwargs 无默认值参数 → worker 端收到值而非缺参。"""
    calls = []

    @as_task()
    def needs_key(key):
        calls.append(key)
        return key

    needs_key(key="abc")
    submission = _fake_agent.submitted[-1]
    _fake_agent.submitted.clear()

    result = _run_on_worker(submission, needs_key)
    assert result == "abc" and calls == ["abc"], \
        f"worker must receive kwarg value, calls={calls}"


def test_kwargs_with_default_worker_gets_call_value_not_default():
    """静默错误危险形态：有默认值参数传不同值 → worker 收到调用值而非默认值。
    缺陷期此场景静默产出错误数据（默认值生效），无任何报错。"""
    seen = []

    @as_task()
    def with_default(mode="slow"):
        seen.append(mode)
        return mode

    with_default(mode="fast")
    submission = _fake_agent.submitted[-1]
    _fake_agent.submitted.clear()

    result = _run_on_worker(submission, with_default)
    assert result == "fast", \
        f"worker must receive the caller value 'fast', got {result!r} " \
        f"(silent-default hazard)"


def test_mixed_positional_and_kwargs():
    @as_task()
    def mixed(a, b, repeat=1, tag=None):
        return (a, b, repeat, tag)

    mixed(1, "x", repeat=3, tag="t")
    submission = _fake_agent.submitted[-1]
    _fake_agent.submitted.clear()

    assert _run_on_worker(submission, mixed) == (1, "x", 3, "t")


# ── 既有行为回归 ──────────────────────────────────────────────────────

def test_inputs_lambda_receives_kwargs():
    """inputs lambda 的 kwargs 透传（既有行为，随载体修复回归）。"""
    captured = {}

    @as_task(inputs=lambda a, scale=None: [f"obj_{a}_{scale}"])
    def dep_lambda(a, scale=None):
        return None

    dep_lambda(7, scale=2)
    submission = _fake_agent.submitted[-1]
    _fake_agent.submitted.clear()
    assert submission['inputs'] == ["obj_7_2"], submission['inputs']


def test_write_context_hash_stable_across_resubmit():
    """同任务同参数重复提交 → write_context_hash 一致（hash 输入为新
    serialized 载体，kwargs 必须参与——载体含 kwargs 尾段）。"""
    @as_task()
    def hashed(a, b=None):
        return None

    hashes = []
    for _ in range(2):
        hashed(1, b="same")
        hashes.append(_fake_agent.submitted[-1]['write_context_hash'])
        _fake_agent.submitted.clear()
    assert hashes[0] and hashes[0] == hashes[1], hashes

    # 不同 kwargs 值 → 载体不同 → hash 不同（kwargs 确实参与 hash 输入）
    hashed(1, b="other")
    other = _fake_agent.submitted[-1]['write_context_hash']
    _fake_agent.submitted.clear()
    assert other != hashes[0], "different kwargs must change the write context hash"


# ── 旧格式载体显式失效 ────────────────────────────────────────────────

def test_deserialize_legacy_payload_raises():
    """修复前载体（纯位置编码列表，无 kwargs 尾段）→ worker 端显式报错
    而非静默当空 kwargs。"""
    import pickle
    legacy_payload = [pickle.dumps(1).hex(), pickle.dumps("x").hex()]
    try:
        deserialize_args(legacy_payload, None)
        raise AssertionError("legacy payload without kwargs section must fail loudly")
    except ValueError as e:
        assert "kwargs" in str(e), f"error must name the missing kwargs section: {e}"


def main():
    tests = [v for k, v in sorted(globals().items()) if k.startswith("test_")]
    ran = 0
    for t in tests:
        if not _HAS_CP and t.__name__ in (
                "test_kwargs_no_default_reaches_worker",
                "test_kwargs_with_default_worker_gets_call_value_not_default",
                "test_mixed_positional_and_kwargs"):
            print(f"  [SKIP] {t.__name__} (cloudpickle unavailable outside bazel)")
            continue
        t()
        ran += 1
        print(f"  [PASS] {t.__name__}")
    print(f"kwargs args: {ran} tests passed")


if __name__ == "__main__":
    main()
