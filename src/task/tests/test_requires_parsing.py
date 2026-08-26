"""测试 as_task 装饰器对 requires 返回值的解析逻辑。

验证 requires 的各种形式被正确解析为 (required_capabilities, attribute_timeout)
并传递给 agent.submit()。通过 monkeypatch get_agent 拦截 submit 调用。
"""
import sys
import os
import types
import importlib.util

_SRC_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..'))
_TASK_PY = os.path.join(_SRC_ROOT, 'src', 'task', 'py', 'task.py')

# task.py 依赖 _fly_storage 和 _fly_log，测试只验证解析逻辑，
# 通过注入 stub 模块绕过这些依赖。
# Stub _fly_storage.ex_stg_compute_write_context_hash
_fly_storage_stub = types.ModuleType('_fly_storage')
_fly_storage_stub.ex_stg_compute_write_context_hash = lambda *a, **kw: ""
sys.modules['_fly_storage'] = _fly_storage_stub

# Stub _fly_log.DBG
_fly_log_stub = types.ModuleType('_fly_log')
_fly_log_stub.DBG = lambda *a, **kw: None
sys.modules['_fly_log'] = _fly_log_stub

# Stub fly.runtime（task.py 的 wrapper 内部 from fly.runtime import get_agent）
_fly_runtime_stub = types.ModuleType('fly.runtime')
_fly_runtime_stub.get_agent = lambda: None
_fly_pkg = types.ModuleType('fly')
_fly_pkg.__path__ = []
sys.modules['fly'] = _fly_pkg
sys.modules['fly.runtime'] = _fly_runtime_stub

# 直接按文件路径加载 task.py 为独立模块，避免与 task 包名冲突
_spec = importlib.util.spec_from_file_location("_test_task_module", _TASK_PY)
task_mod = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(task_mod)


class _FakeAgent:
    """记录 submit 调用的假 agent"""
    def __init__(self):
        self.last_submit = None
        self.mode = "test"

    def submit(self, name, module, args, inputs=None,
               required_capabilities=None, attribute_timeout=-1.0,
               write_context_hash="", vars=None, priority=10,
               owner_db_path=""):
        self.last_submit = {
            'name': name,
            'module': module,
            'args': args,
            'inputs': inputs,
            'required_capabilities': required_capabilities,
            'attribute_timeout': attribute_timeout,
            'write_context_hash': write_context_hash,
            'vars': vars,
            'priority': priority,
            'owner_db_path': owner_db_path,
        }


def _make_wrapper(requires=None, vars=None, priority=10):
    """构造一个带指定 requires/vars/priority 的 as_task wrapper，返回 (wrapper, fake_agent, restore)"""
    import fly.runtime as runtime
    fake_agent = _FakeAgent()
    orig_get_agent = runtime.get_agent
    runtime.get_agent = lambda: fake_agent

    @task_mod.as_task(requires=requires, vars=vars, priority=priority)
    def my_task(db):
        pass

    def restore():
        runtime.get_agent = orig_get_agent
    return my_task, fake_agent, restore


def test_requires_pure_list():
    """纯 list → caps=list, timeout=-1 (死等)"""
    wrapper, agent, restore = _make_wrapper(["gpu"])
    try:
        wrapper(None)
    finally:
        restore()
    assert agent.last_submit['required_capabilities'] == ["gpu"]
    assert agent.last_submit['attribute_timeout'] == -1.0
    assert agent.last_submit['priority'] == 10  # 默认优先级（向后兼容回归保护）


def test_requires_tuple_positive_timeout():
    """tuple (list, float>0) → caps=list, timeout=float"""
    wrapper, agent, restore = _make_wrapper((["gpu", "cuda"], 5.0))
    try:
        wrapper(None)
    finally:
        restore()
    assert agent.last_submit['required_capabilities'] == ["gpu", "cuda"]
    assert agent.last_submit['attribute_timeout'] == 5.0


def test_requires_tuple_zero_timeout():
    """tuple (list, 0) → caps=list, timeout=0 (立即降级)"""
    wrapper, agent, restore = _make_wrapper((["gpu"], 0))
    try:
        wrapper(None)
    finally:
        restore()
    assert agent.last_submit['required_capabilities'] == ["gpu"]
    assert agent.last_submit['attribute_timeout'] == 0


def test_requires_tuple_negative_timeout():
    """tuple (list, 负数) → caps=list, timeout=负数 (死等)"""
    wrapper, agent, restore = _make_wrapper((["gpu"], -2.5))
    try:
        wrapper(None)
    finally:
        restore()
    assert agent.last_submit['required_capabilities'] == ["gpu"]
    assert agent.last_submit['attribute_timeout'] == -2.5


def test_requires_callable_returns_list():
    """callable 返回 list → caps=list, timeout=-1 (死等)"""
    wrapper, agent, restore = _make_wrapper(lambda *a, **kw: ["gpu"])
    try:
        wrapper(None)
    finally:
        restore()
    assert agent.last_submit['required_capabilities'] == ["gpu"]
    assert agent.last_submit['attribute_timeout'] == -1.0


def test_requires_callable_returns_tuple():
    """callable 返回 tuple → 解析 tuple"""
    wrapper, agent, restore = _make_wrapper(lambda *a, **kw: (["gpu"], 3.0))
    try:
        wrapper(None)
    finally:
        restore()
    assert agent.last_submit['required_capabilities'] == ["gpu"]
    assert agent.last_submit['attribute_timeout'] == 3.0


def test_requires_none_defaults_to_empty():
    """requires=None → 空 caps, timeout=-1"""
    wrapper, agent, restore = _make_wrapper(None)
    try:
        wrapper(None)
    finally:
        restore()
    assert agent.last_submit['required_capabilities'] == []
    assert agent.last_submit['attribute_timeout'] == -1.0


def test_requires_callable_with_args():
    """callable 接收 task 参数动态决定 requires"""
    def dynamic_requires(db, key, cap_name):
        return ([cap_name], 1.0)

    wrapper, agent, restore = _make_wrapper(dynamic_requires)
    try:
        wrapper(None, "mykey", "gpu")
    finally:
        restore()
    assert agent.last_submit['required_capabilities'] == ["gpu"]
    assert agent.last_submit['attribute_timeout'] == 1.0


def test_requires_empty_list():
    """空 list → 空 caps, timeout=-1"""
    wrapper, agent, restore = _make_wrapper([])
    try:
        wrapper(None)
    finally:
        restore()
    assert agent.last_submit['required_capabilities'] == []
    assert agent.last_submit['attribute_timeout'] == -1.0


# ---- vars 参数解析测试 ----

def test_vars_pure_list():
    """vars=list[str] → 直接传递"""
    wrapper, agent, restore = _make_wrapper(vars=["counter", "threshold"])
    try:
        wrapper(None)
    finally:
        restore()
    assert agent.last_submit['vars'] == ["counter", "threshold"]


def test_vars_callable():
    """vars=callable → 提交时动态解析"""
    wrapper, agent, restore = _make_wrapper(vars=lambda db, key: [f"var_{key}"])
    try:
        wrapper(None, "abc")
    finally:
        restore()
    assert agent.last_submit['vars'] == ["var_abc"]


def test_vars_none_defaults_to_empty():
    """vars=None → 空 list"""
    wrapper, agent, restore = _make_wrapper()
    try:
        wrapper(None)
    finally:
        restore()
    assert agent.last_submit['vars'] == []


def test_vars_combined_with_requires():
    """vars 和 requires 同时使用"""
    wrapper, agent, restore = _make_wrapper(requires=(["gpu"], 5.0), vars=["cfg"])
    try:
        wrapper(None)
    finally:
        restore()
    assert agent.last_submit['required_capabilities'] == ["gpu"]
    assert agent.last_submit['attribute_timeout'] == 5.0
    assert agent.last_submit['vars'] == ["cfg"]


# ---- priority 参数解析测试 ----

def test_priority_default_is_ten():
    """不指定 priority → 默认 10（向后兼容：所有 task 同值，退化为 FIFO）"""
    wrapper, agent, restore = _make_wrapper()
    try:
        wrapper(None)
    finally:
        restore()
    assert agent.last_submit['priority'] == 10


def test_priority_high_value_passed_through():
    """高优先级 priority=20 完整透传到 agent.submit"""
    wrapper, agent, restore = _make_wrapper(priority=20)
    try:
        wrapper(None)
    finally:
        restore()
    assert agent.last_submit['priority'] == 20


def test_priority_low_value_passed_through():
    """低优先级 priority=1（让路）完整透传到 agent.submit"""
    wrapper, agent, restore = _make_wrapper(priority=1)
    try:
        wrapper(None)
    finally:
        restore()
    assert agent.last_submit['priority'] == 1


def test_priority_combined_with_requires_and_vars():
    """priority + requires(capability+timeout) + vars 三者组合，各自独立透传"""
    wrapper, agent, restore = _make_wrapper(
        requires=(["gpu"], 2.0), vars=["cfg"], priority=15)
    try:
        wrapper(None)
    finally:
        restore()
    assert agent.last_submit['required_capabilities'] == ["gpu"]
    assert agent.last_submit['attribute_timeout'] == 2.0
    assert agent.last_submit['vars'] == ["cfg"]
    assert agent.last_submit['priority'] == 15


def test_priority_combined_with_high_value_requires():
    """priority + 高优先级 capability 组合（典型关键路径抢先场景）"""
    wrapper, agent, restore = _make_wrapper(requires=["gpu"], priority=20)
    try:
        wrapper(None)
    finally:
        restore()
    assert agent.last_submit['required_capabilities'] == ["gpu"]
    assert agent.last_submit['attribute_timeout'] == -1.0
    assert agent.last_submit['priority'] == 20


if __name__ == "__main__":
    test_requires_pure_list()
    print("PASS: test_requires_pure_list")
    test_requires_tuple_positive_timeout()
    print("PASS: test_requires_tuple_positive_timeout")
    test_requires_tuple_zero_timeout()
    print("PASS: test_requires_tuple_zero_timeout")
    test_requires_tuple_negative_timeout()
    print("PASS: test_requires_tuple_negative_timeout")
    test_requires_callable_returns_list()
    print("PASS: test_requires_callable_returns_list")
    test_requires_callable_returns_tuple()
    print("PASS: test_requires_callable_returns_tuple")
    test_requires_none_defaults_to_empty()
    print("PASS: test_requires_none_defaults_to_empty")
    test_requires_callable_with_args()
    print("PASS: test_requires_callable_with_args")
    test_requires_empty_list()
    print("PASS: test_requires_empty_list")
    test_vars_pure_list()
    print("PASS: test_vars_pure_list")
    test_vars_callable()
    print("PASS: test_vars_callable")
    test_vars_none_defaults_to_empty()
    print("PASS: test_vars_none_defaults_to_empty")
    test_vars_combined_with_requires()
    print("PASS: test_vars_combined_with_requires")
    test_priority_default_is_ten()
    print("PASS: test_priority_default_is_ten")
    test_priority_high_value_passed_through()
    print("PASS: test_priority_high_value_passed_through")
    test_priority_low_value_passed_through()
    print("PASS: test_priority_low_value_passed_through")
    test_priority_combined_with_requires_and_vars()
    print("PASS: test_priority_combined_with_requires_and_vars")
    test_priority_combined_with_high_value_requires()
    print("PASS: test_priority_combined_with_high_value_requires")
    print("\nAll requires + vars + priority parsing tests passed!")
