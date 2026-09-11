"""wait_obj 依赖传播体系单测（R9 批次，裁定 ㊼）。

验证框架支撑三件套（DEVELOPMENT_GUIDELINES Section 17 标准形态的依赖面）：
  - wrapper.deps(*args, **kwargs)：inputs lambda 解析结果透传（参数传递/
    None inputs 返回 []/返回值即列表）——上层 as_task inputs 传播的数据源；
  - run_direct(func, ...)：经 _fly_original_func 直调原函数，剥离本地等待
    （monkeypatch _wait_for_objects 断言零调用）；无 _fly_original_func 的
    普通函数直调本身；返回值与直调一致；
  - wait_obj 等待语义本体（deps 兜底的基础）：依赖未就绪时轮询等待、
    就绪后执行；确认无法产出时 RuntimeError 兜底（漏声明依赖场景的
    明确报错，而非静默错读）。
"""
import sys
import types

# stub task.py 的包依赖（同 test_callable_args.py 模式：业务代码走包根
# `from log/storage import`，此处以 stub 模块接管）

_log_stub = types.ModuleType('log')
_log_stub.DBG = lambda *a, **kw: None
_log_stub.INFO = lambda *a, **kw: None
_log_stub.WARN = lambda *a, **kw: None
_log_stub.ERR = lambda *a, **kw: None
sys.modules['log'] = _log_stub


class _DataServiceHolder:
    """可替换的假 DataService（每个测试场景注入自己的可见性行为）。"""

    def __init__(self):
        self.ds = None

    def __call__(self):
        assert self.ds is not None, "test must inject a fake DataService"
        return self.ds


_ds_holder = _DataServiceHolder()

_storage_stub = types.ModuleType('storage')
_storage_stub.ex_stg_get_data_service = _ds_holder
# as_task 提交路径的 write_context 计算（stub 恒空串）
_storage_stub.ex_stg_compute_write_context_hash = lambda *a, **kw: ""
sys.modules['storage'] = _storage_stub

# as_task 提交路径的 fly.runtime stub（延迟导入 from fly.runtime import
# get_agent——同 test_callable_args.py 模式）
class _RecordingAgent:
    mode = "master"

    def __init__(self):
        self.submitted = []

    def submit(self, *a, **kw):
        self.submitted.append((a, kw))


_fake_agent = _RecordingAgent()

_fly_runtime_stub = types.ModuleType('fly.runtime')
_fly_runtime_stub.get_agent = lambda: _fake_agent
_fly_pkg = types.ModuleType('fly')
_fly_pkg.__path__ = []
_fly_pkg.runtime = _fly_runtime_stub
sys.modules['fly'] = _fly_pkg
sys.modules['fly.runtime'] = _fly_runtime_stub

import importlib.util
import os

_SRC_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..'))


def _load(name, rel):
    spec = importlib.util.spec_from_file_location(name, os.path.join(_SRC_ROOT, rel))
    mod = importlib.util.module_from_spec(spec)
    sys.modules[name] = mod
    spec.loader.exec_module(mod)
    return mod


task_mod = _load('task', 'src/task/py/task.py')

from task import run_direct, wait_obj


class _FakeDb:
    """最小 db 协议对象（get_full_name 供 inputs lambda 构造对象全名）。"""

    def get_full_name(self, name):
        return f"/tmp/fake_db:{name}"


class _AlwaysVisibleDs:
    """依赖对象恒可见（wait_obj 首轮立即命中，函数体直接执行）。"""

    def has_local_object(self, name):
        return True

    def has_remote_location(self, name):
        return False

    def try_read_remote(self, name):
        return (False, None, "", True)


class _ReadyAfterNLocalChecks:
    """前 N 次 local 检查 miss、之后命中——驱动 wait_obj 真实轮询路径。"""

    def __init__(self, n):
        self._remaining = n

    def has_local_object(self, name):
        if self._remaining > 0:
            self._remaining -= 1
            return False
        return True

    def has_remote_location(self, name):
        return False

    def try_read_remote(self, name):
        return (False, None, "", True)


class _NeverProducedDs:
    """依赖对象恒不可见 + master 确认无法产出（兜底 RuntimeError 路径）。"""

    def has_local_object(self, name):
        return False

    def has_remote_location(self, name):
        return False

    def try_read_remote(self, name):
        return (False, None, "", False)  # found, data, py_name, can_still_produce


# ── wrapper.deps：inputs lambda 解析透传 ─────────────────────────────

def test_deps_resolves_lambda_with_args():
    # deps(*args, **kwargs) 透传参数给 inputs lambda（上层 task 传播依赖的
    # 数据源；load_block_names 类带参 API 的形态）。
    @wait_obj(inputs=lambda db, index: [db.get_full_name(f"DSBlockNames_{index}")])
    def load_names(db, index):
        return None

    db = _FakeDb()
    assert load_names.deps(db, 2) == ["/tmp/fake_db:DSBlockNames_2"]
    # kwargs 形态同样透传
    assert load_names.deps(db, index=3) == ["/tmp/fake_db:DSBlockNames_3"]


def test_deps_none_inputs_returns_empty_list():
    # inputs 为 None（@wait_obj() 裸装饰）→ deps 返回 []。
    @wait_obj()
    def no_deps(x):
        return x

    assert no_deps.deps(1) == []
    assert isinstance(no_deps.deps(), list)


def test_deps_returns_list_value():
    # 返回值即列表（inputs lambda 原样解析结果，不改写不排序）。
    dep_list = ["/a:obj1", "/a:obj2"]

    @wait_obj(inputs=lambda db: list(dep_list))
    def api(db):
        return None

    assert api.deps(_FakeDb()) == dep_list


# ── run_direct：剥离包装直调原函数 ───────────────────────────────────

def test_run_direct_skips_wait_and_calls_original():
    # monkeypatch _wait_for_objects 断言零调用——run_direct 剥离本地等待，
    # 不走 wait_obj 轮询/master 查询（㊼ 裁定的冗余网络 IO 消除点）。
    wait_calls = []

    def _spy_wait(deps, poll_interval, timeout=None):
        wait_calls.append(deps)

    original_wait = task_mod._wait_for_objects
    task_mod._wait_for_objects = _spy_wait
    try:
        @wait_obj(inputs=lambda db: [db.get_full_name("DSDesign")])
        def load_design(db):
            return "design-value"

        db = _FakeDb()
        # 直调 wrapper（未经 run_direct）→ 走等待路径
        assert load_design(db) == "design-value"
        assert len(wait_calls) == 1, wait_calls
        # run_direct → 零等待，直调原函数
        assert run_direct(load_design, db) == "design-value"
        assert len(wait_calls) == 1, "run_direct must not trigger _wait_for_objects"
    finally:
        task_mod._wait_for_objects = original_wait


def test_run_direct_plain_function_calls_itself():
    # 无 _fly_original_func 的普通函数：run_direct 直调本身。
    def plain(x, y=1):
        return x + y

    assert run_direct(plain, 2) == 3
    assert run_direct(plain, 2, y=40) == 42


def test_run_direct_return_value_matches_direct_call():
    # 返回值一致：run_direct(包装函数) ≡ 直调原函数（任意返回对象原样透传）。
    sentinel = object()

    @wait_obj()
    def api():
        return sentinel

    assert run_direct(api) is sentinel


def test_run_direct_on_as_task_wrapper_calls_original_func():
    # _fly_original_func 在 as_task wrapper 上同样存在——run_direct 直调原
    # 函数本体（不触发提交）。注意 as_task 任务函数按规范不这样用（会绕过
    # 任务提交语义），此处仅锁定框架行为：直调的是原函数而非 wrapper。
    from task import as_task

    _fake_agent.submitted.clear()

    @as_task(inputs=lambda db: [])
    def task_fn(db):
        return "ran-locally"

    assert run_direct(task_fn, _FakeDb()) == "ran-locally"
    assert _fake_agent.submitted == [], "run_direct must bypass task submission"


# ── wait_obj 等待语义本体（deps 兜底的基础）──────────────────────────

def test_wait_obj_polls_until_deps_ready():
    # 依赖前几轮不可见、随后就绪 → wait_obj 轮询等待后执行函数体。
    _ds_holder.ds = _ReadyAfterNLocalChecks(n=3)

    @wait_obj(inputs=lambda db: [db.get_full_name("late_obj")],
              poll_interval=0.01)
    def late_reader(db):
        return f"read:{db.get_full_name('late_obj')}"

    db = _FakeDb()
    assert late_reader(db) == f"read:{db.get_full_name('late_obj')}"


def test_wait_obj_cannot_produce_raises_runtimeerror():
    # 依赖恒不可见 + master 确认无法产出（无 pending/running 任务）→
    # RuntimeError 兜底——漏声明依赖场景的明确报错（而非静默错读）。
    # probe_interval=max(poll*5, 0.5)=0.5s → 3 次确认 ≈1s，确定性成立。
    _ds_holder.ds = _NeverProducedDs()

    @wait_obj(inputs=lambda db: [db.get_full_name("never_obj")],
              poll_interval=0.02)
    def never_ready(db):
        return None

    try:
        never_ready(_FakeDb())
        raise AssertionError("wait_obj must raise when deps cannot be produced")
    except RuntimeError as e:
        assert "cannot be produced" in str(e), str(e)


def test_wait_obj_ready_deps_execute_immediately():
    # 数据已就绪（QA 现网主路径）：首轮 local 命中，零轮询直接执行。
    _ds_holder.ds = _AlwaysVisibleDs()

    @wait_obj(inputs=lambda db: [db.get_full_name("DSDesign")])
    def load_design(db):
        return "ok"

    assert load_design(_FakeDb()) == "ok"


def main():
    tests = [v for k, v in sorted(globals().items()) if k.startswith("test_")]
    for t in tests:
        t()
        print(f"  [PASS] {t.__name__}")
    print(f"wait_obj deps/run_direct: {len(tests)} tests passed")


if __name__ == "__main__":
    main()
