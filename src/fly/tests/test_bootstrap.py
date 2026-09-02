"""Unit tests for fly/bootstrap.py — 惰性代理与用户脚本命名空间构建。

运行：./fly.sh test //src/fly/tests:bootstrap_test

覆盖（2026-09 覆盖率批次 14 项之 9）：
  - _LazyAttr.__call__ 触发目标模块 import 并实例化
  - _LazyAttr.__getattr__ 属性转发
  - 下划线属性访问 → AttributeError（不触发 import）
  - get_script_namespace：fly 公共符号注入 + help 覆盖 + 惰性代理注入
"""
import sys
import types

import fly
import fly.bootstrap as bootstrap
from fly.bootstrap import _LazyAttr, get_script_namespace


def _install_fake_module(name="bootstrap_biz_mod"):
    """注入一个假业务模块（模拟 solver 的重 import 成本）。"""
    mod = types.ModuleType(name)

    class FakeBiz:
        tag = "biz"

        def __init__(self, *args, **kwargs):
            self.init_args = args

        @staticmethod
        def build(x):
            return f"built:{x}"

    mod.FakeBiz = FakeBiz
    sys.modules[name] = mod
    return mod


def test_lazy_attr_call_resolves_and_instantiates():
    mod = _install_fake_module()
    proxy = _LazyAttr("bootstrap_biz_mod:FakeBiz")
    # 未使用前不 import（sys.modules 注入仅是准备；_resolve 走 importlib 命中）。
    obj = proxy("hello")
    assert type(obj) is mod.FakeBiz, type(obj)
    assert obj.init_args == ("hello",), "构造参数应透传给真实类"
    print("  PASS: test_lazy_attr_call_resolves_and_instantiates")


def test_lazy_attr_getattr_forwards():
    _install_fake_module()
    proxy = _LazyAttr("bootstrap_biz_mod:FakeBiz")
    assert proxy.tag == "biz", "类属性访问应转发"
    assert proxy.build("x") == "built:x", "静态方法访问应转发"
    print("  PASS: test_lazy_attr_getattr_forwards")


def test_lazy_attr_underscore_raises_without_import():
    # 下划线属性：直接 AttributeError，不触发目标模块 import。
    sys.modules.pop("bootstrap_never_mod", None)
    proxy = _LazyAttr("bootstrap_never_mod:Never")
    try:
        proxy._some_private
        raise AssertionError("underscore access must raise AttributeError")
    except AttributeError:
        pass
    assert "bootstrap_never_mod" not in sys.modules, \
        "下划线访问不得触发目标模块 import"
    print("  PASS: test_lazy_attr_underscore_raises_without_import")


def test_lazy_attr_missing_attr_raises_attributeerror():
    _install_fake_module()
    proxy = _LazyAttr("bootstrap_biz_mod:FakeBiz")
    try:
        proxy.no_such_attr
        raise AssertionError("missing attribute must raise AttributeError")
    except AttributeError:
        pass
    print("  PASS: test_lazy_attr_missing_attr_raises_attributeerror")


def test_get_script_namespace_basics():
    ns = get_script_namespace()
    assert ns["__name__"] == "__main__"
    # fly 公共 API 全量注入（非下划线符号）
    for name in dir(fly):
        if name.startswith("_"):
            continue
        assert name in ns, f"fly 公共符号 {name} 应注入脚本命名空间"
    # help 覆盖为 fly.help
    assert ns["help"] is fly.help
    # 下划线符号不注入
    assert "_LazyAttr" not in ns
    print("  PASS: test_get_script_namespace_basics")


def test_get_script_namespace_injects_lazy_proxies():
    ns = get_script_namespace()
    # bootstrap 顶部已把 _LAZY_MODULES 注册进 userdoc → 注入同名惰性代理。
    for name, target in bootstrap._LAZY_MODULES.items():
        proxy = ns.get(name)
        assert isinstance(proxy, _LazyAttr), \
            f"{name} 应以 _LazyAttr 惰性代理注入, got {type(proxy)}"
        assert proxy._target == target  # 同模块内访问 _slots 私有，非转发路径
    print("  PASS: test_get_script_namespace_injects_lazy_proxies")


def _run_all():
    tests = [v for k, v in sorted(globals().items()) if k.startswith("test_")]
    failed = 0
    for t in tests:
        try:
            t()
        except Exception as e:
            failed += 1
            print(f"  FAIL: {t.__name__}: {e}")
            import traceback
            traceback.print_exc()
    print(f"bootstrap: {len(tests) - failed}/{len(tests)} passed")
    if failed:
        sys.exit(1)


if __name__ == "__main__":
    _run_all()
