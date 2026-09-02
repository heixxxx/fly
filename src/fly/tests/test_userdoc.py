"""Unit tests for fly.userdoc (Schema / UserDoc / document / help).

纯 Python 测试，无 C++ 依赖。直接从源码路径加载 fly.userdoc 子模块，
绕过 fly/__init__.py 的 C++ 扩展导入，保证测试可独立运行。
"""
import os
import sys
import importlib.util

_this_dir = os.path.dirname(os.path.abspath(__file__))

# 直接从源码加载 fly.userdoc（避免触发 fly/__init__.py 的 _fly_log 等 C++ 依赖）
_userdoc_path = os.path.normpath(os.path.join(_this_dir, "..", "userdoc.py"))
_spec = importlib.util.spec_from_file_location("fly.userdoc", _userdoc_path)
userdoc = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(userdoc)

Schema = userdoc.Schema
UserDoc = userdoc.UserDoc
document = userdoc.document
help = userdoc.help


def _fresh_registry():
    """每个测试用独立的 registry，避免互相干扰。"""
    userdoc._HELP_REGISTRY.clear()


# ═══════════════════════════════════════════════════════════════════════════
# Schema 基础类型
# ═══════════════════════════════════════════════════════════════════════════

def test_schema_basic_pass():
    s = Schema(int)
    assert s.validate(5) == []
    assert s.validate(True) == []          # bool 是 int 子类


def test_schema_basic_type_error():
    s = Schema(int)
    errs = s.validate("x")
    assert len(errs) == 1
    assert "expected int" in errs[0]
    assert "got str" in errs[0]


def test_schema_type_tuple():
    s = Schema((int, float))
    assert s.validate(1) == []
    assert s.validate(1.5) == []
    assert "expected int | float" in s.validate("x")[0]


def test_schema_typename_string():
    """类名字符串匹配（规避循环导入）。"""
    s = Schema("_Database")

    class _Database:        # 类名与 schema 字符串一致
        pass

    assert s.validate(_Database()) == []

    class Other:
        pass
    errs = s.validate(Other())
    assert len(errs) == 1
    assert "_Database" in errs[0]


def test_schema_check_pass():
    s = Schema(int, check=lambda n: n >= 1, error="must be >= 1")
    assert s.validate(5) == []


def test_schema_check_fail_default_error():
    """check 失败、error=None 时用默认消息。"""
    s = Schema(int, check=lambda n: n >= 1)
    errs = s.validate(0)
    assert len(errs) == 1
    assert "failed check" in errs[0]


def test_schema_check_fail_template_error():
    """error 模板支持 {value} 占位渲染。"""
    s = Schema(int, check=lambda n: n >= 1, error="must be >= 1, got {value}")
    errs = s.validate(0)
    assert "must be >= 1, got 0" in errs[0]


def test_schema_check_fail_callable_error():
    """error 为 callable(value) -> str。"""
    s = Schema(int, check=lambda n: n >= 1, error=lambda v: f"too small: {v}")
    errs = s.validate(-3)
    assert "too small: -3" in errs[0]


def test_schema_type_error_short_circuits_check():
    """类型不对时短路，不跑 check（避免 check 对错误类型崩溃）。"""
    called = [0]

    def boom(value):
        called[0] += 1
        return value >= 1

    s = Schema(int, check=boom, error="bad")
    s.validate("not_an_int")
    assert called[0] == 0      # check 未被调用


def test_schema_desc_overrides_str():
    s = Schema(int, desc="正整数")
    assert str(s) == "正整数"


def test_schema_str_no_desc():
    assert str(Schema(int)) == "int"
    assert str(Schema((int, float))) == "int | float"


# ═══════════════════════════════════════════════════════════════════════════
# 裸函数自动包装
# ═══════════════════════════════════════════════════════════════════════════

def test_callable_auto_wrapped_in_add_param():
    """add_param(schema=<裸 callable>) 自动包装成 Schema(object, check=fn)。

    裸函数包装时无 error 参数，check 失败用默认消息；需自定义消息时显式构造 Schema。
    """
    _fresh_registry()
    doc = UserDoc("test")
    doc.add_param("x", schema=lambda v: v > 0)
    p = doc._params[0]
    assert isinstance(p["schema"], Schema)
    assert p["schema"].validate(5) == []
    errs = p["schema"].validate(-1)
    assert len(errs) == 1
    assert "failed check" in errs[0]


# ═══════════════════════════════════════════════════════════════════════════
# Schema.dict
# ═══════════════════════════════════════════════════════════════════════════

def test_dict_pass():
    d = Schema.dict(
        required={"host": Schema(str), "port": Schema(int)},
        optional={"attr": Schema(str)},
    )
    assert d.validate({"host": "h", "port": 80}) == []
    assert d.validate({"host": "h", "port": 80, "attr": "x"}) == []


def test_dict_missing_required():
    d = Schema.dict(required={"host": Schema(str), "port": Schema(int)})
    errs = d.validate({"host": "h"})
    assert any("missing required key 'port'" in e for e in errs)


def test_dict_extra_key_rejected():
    d = Schema.dict(required={"host": Schema(str)}, allow_extra=False)
    errs = d.validate({"host": "h", "extra": 1})
    assert any("unexpected key 'extra'" in e for e in errs)


def test_dict_allow_extra():
    d = Schema.dict(required={"host": Schema(str)}, allow_extra=True)
    assert d.validate({"host": "h", "extra": 1}) == []


def test_dict_field_check():
    d = Schema.dict(
        required={"port": Schema(int, check=lambda p: 1 <= p <= 65535,
                                 error="must be in [1,65535], got {value}")}
    )
    errs = d.validate({"port": 70000})
    assert any("must be in [1,65535]" in e for e in errs)


def test_dict_whole_check():
    """整个 dict 的深度校验。"""
    d = Schema.dict(
        required={"a": Schema(int), "b": Schema(int)},
        check=lambda v: v["a"] < v["b"], error="a must be < b",
    )
    assert d.validate({"a": 1, "b": 2}) == []
    errs = d.validate({"a": 5, "b": 1})
    assert any("a must be < b" in e for e in errs)


def test_dict_str_repr():
    d = Schema.dict(required={"host": Schema(str)}, optional={"attr": Schema(str)})
    s = str(d)
    assert "host" in s and "attr?" in s


# ═══════════════════════════════════════════════════════════════════════════
# Schema.list
# ═══════════════════════════════════════════════════════════════════════════

def test_list_pass():
    s = Schema.list(Schema(int))
    assert s.validate([1, 2, 3]) == []


def test_list_element_recursive():
    s = Schema.list(Schema(int, check=lambda n: n >= 0, error="negative"))
    errs = s.validate([1, -2, 3])
    assert len(errs) == 1
    assert "[1]" in errs[0]          # 路径带索引
    assert "negative" in errs[0]


def test_list_min_max_len():
    s = Schema.list(Schema(int), min_len=2, max_len=3)
    assert s.validate([1, 2]) == []
    errs = s.validate([1])
    assert any("min" in e for e in errs)
    errs = s.validate([1, 2, 3, 4])
    assert any("max" in e for e in errs)


def test_list_whole_check():
    s = Schema.list(Schema(int), check=lambda v: len(v) > 0, error="must not be empty")
    assert s.validate([1]) == []
    errs = s.validate([])
    assert any("must not be empty" in e for e in errs)


# ═══════════════════════════════════════════════════════════════════════════
# Schema.any_of
# ═══════════════════════════════════════════════════════════════════════════

def test_any_of_first_branch_passes():
    a = Schema.any_of(Schema(float), Schema.list(Schema(int)))
    assert a.validate(1.5) == []


def test_any_of_second_branch_passes():
    a = Schema.any_of(Schema(float), Schema.list(Schema(int)))
    assert a.validate([1, 2]) == []


def test_any_of_all_fail_aggregates():
    """全部分支失败时聚合各分支错误。

    用数值 3.0：float 分支类型匹配但 check（<=2）失败报 check 错误；
    str 分支类型不匹配报类型错误。两分支各自独立错误都被收集。
    """
    a = Schema.any_of(
        Schema(float, check=lambda w: 0 < w <= 2, error="float in (0,2]"),
        Schema(str, check=lambda w: w in ("coarse", "adaptive"),
               error="must be coarse/adaptive"),
    )
    errs = a.validate(3.0)
    assert len(errs) >= 2
    assert any("expected one of" in e for e in errs)
    assert any("float in (0,2]" in e for e in errs)      # float 分支的 check 错误
    assert any("coarse/adaptive" in e for e in errs) or any("expected str" in e for e in errs)


def test_any_of_str_repr():
    a = Schema.any_of(Schema(float), Schema(str))
    assert "(float)" in str(a) and "(str)" in str(a)


# ═══════════════════════════════════════════════════════════════════════════
# 任意嵌套
# ═══════════════════════════════════════════════════════════════════════════

def test_deep_nesting():
    """list[dict{required: list[str]}] 多层错误聚合。"""
    s = Schema.list(
        Schema.dict(required={"tags": Schema.list(Schema(str), min_len=1)})
    )
    assert s.validate([{"tags": ["a"]}]) == []
    errs = s.validate([{"tags": []}, {"tags": [1]}])
    assert len(errs) == 2
    assert "[0].tags" in errs[0]        # min_len
    assert "[1].tags" in errs[1]        # 元素类型


# ═══════════════════════════════════════════════════════════════════════════
# UserDoc 冻结 + 幂等
# ═══════════════════════════════════════════════════════════════════════════

def test_freeze_locks_content():
    _fresh_registry()
    doc = UserDoc("test")
    doc.register_help(api_name="x")
    try:
        doc.add_param("y", schema=Schema(int))
        assert False, "should have raised"
    except RuntimeError as e:
        assert "frozen" in str(e)


def test_register_help_idempotent():
    _fresh_registry()
    doc = UserDoc("test")
    doc.register_help(api_name="x")
    doc.register_help(api_name="x")      # 幂等：不报错、不重复
    assert len(userdoc._HELP_REGISTRY) == 1


def test_owner_injection_not_blocked_by_freeze():
    """owner 注入是身份注入，不受冻结限制。"""
    _fresh_registry()
    doc = UserDoc("test")
    doc.register_help(api_name="x")
    doc._api_name = "y"            # 身份注入允许
    doc._owner = type("C", (), {})   # owner 注入允许


# ═══════════════════════════════════════════════════════════════════════════
# document 装饰器
# ═══════════════════════════════════════════════════════════════════════════

def test_document_validates_and_calls():
    _fresh_registry()
    doc = UserDoc("加法")
    doc.add_param("x", schema=Schema(int, check=lambda n: n >= 1, error="x>=1"),
                  required=True)

    @document(doc)
    def add(self, x):
        return x + 1

    assert add(None, 5) == 6
    try:
        add(None, 0)
        assert False
    except ValueError as e:
        assert "add" in str(e)
        assert "x>=1" in str(e)


def test_document_aggregates_errors():
    _fresh_registry()
    doc = UserDoc("multi")
    doc.add_param("a", schema=Schema(int, check=lambda n: n >= 1, error="a bad"),
                  required=True)
    doc.add_param("b", schema=Schema(str), required=True)

    @document(doc)
    def f(self, a, b):
        return None

    try:
        f(None, a=0, b=123)
        assert False
    except ValueError as e:
        msg = str(e)
        assert "a bad" in msg
        assert "expected str" in msg


def test_document_self_skipped():
    """self 不在 doc._params 中，自然不校验。"""
    _fresh_registry()
    doc = UserDoc("test")
    doc.add_param("x", schema=Schema(int), required=True)

    @document(doc)
    def f(self, x):
        return x

    assert f("anything_as_self", 5) == 5


def test_document_api_name_injected():
    _fresh_registry()
    doc = UserDoc("test")
    doc.add_param("x", schema=Schema(int))

    @document(doc)
    def my_func(self, x):
        return x

    assert doc._api_name == "my_func"
    assert "my_func" in userdoc._HELP_REGISTRY


def test_document_signature_preserved():
    _fresh_registry()
    doc = UserDoc("test")

    @document(doc)
    def f(self, a, b=10, *args, **kwargs):
        return a + b

    sig_str = str(doc._signature)
    assert "a" in sig_str and "b=10" in sig_str


def test_document_missing_required():
    _fresh_registry()
    doc = UserDoc("test")
    doc.add_param("x", schema=Schema(int), required=True)
    doc.add_param("y", schema=Schema(int), required=True)

    @document(doc)
    def f(self, x, y):
        return x + y

    try:
        f(None, x=1)       # 缺 y
        assert False
    except ValueError as e:
        assert "missing" in str(e).lower()
        assert "y" in str(e)


def test_none_ok_default_rejects_none():
    """none_ok 默认 False：值为 None 报错。"""
    _fresh_registry()
    doc = UserDoc("test")
    doc.add_param("x", schema=Schema(int), required=True)

    @document(doc)
    def f(self, x):
        return x

    try:
        f(None, None)       # x=None
        assert False
    except ValueError as e:
        assert "must not be None" in str(e)
        assert "x" in str(e)


def test_none_ok_true_accepts_none():
    """none_ok=True：值为 None 通过，跳过 schema 校验。"""
    _fresh_registry()
    doc = UserDoc("test")
    doc.add_param("db", schema=Schema(int), required=False, default=None, none_ok=True)

    @document(doc)
    def f(self, db=None):
        return db

    assert f(None, None) is None          # None 通过（即使 schema 是 int）
    assert f(None, db=5) == 5             # 非 None 走 schema


def test_none_ok_true_non_none_still_validated():
    """none_ok=True 只豁免 None，非 None 值仍按 schema 校验。"""
    _fresh_registry()
    doc = UserDoc("test")
    doc.add_param("db", schema=Schema(int, check=lambda n: n >= 1, error=">=1"),
                  required=False, default=None, none_ok=True)

    @document(doc)
    def f(self, db=None):
        return db

    assert f(None, None) is None          # None 豁免
    try:
        f(None, db=0)                     # 非 None 但 check 失败
        assert False
    except ValueError as e:
        assert ">=1" in str(e)


# ═══════════════════════════════════════════════════════════════════════════
# document 装饰器 — 类（包装 __init__）
# ═══════════════════════════════════════════════════════════════════════════

def test_document_class_basic():
    """类装饰器：api_name=类名，校验 __init__ 参数。"""
    _fresh_registry()
    doc = UserDoc("测试类")
    doc.add_param("db_path", schema=Schema(str, check=lambda s: len(s) > 0,
                                             error="must not be empty"),
                  required=True, desc="路径")

    class Base:
        def __init__(self, db_path: str):
            self.db_path = db_path

    @document(doc)
    class Child(Base):
        pass

    assert doc._api_name == "Child"
    assert str(doc._signature) == "(db_path: str)"
    # 合法构造
    c = Child("/valid")
    assert c.db_path == "/valid"
    assert "Child" in userdoc._HELP_REGISTRY


def test_document_class_validates_init():
    """类装饰器：非法 __init__ 参数被校验拦截。"""
    _fresh_registry()
    doc = UserDoc("测试类")
    doc.add_param("db_path", schema=Schema(str, check=lambda s: len(s) > 0,
                                             error="must not be empty"),
                  required=True)

    class Base:
        def __init__(self, db_path):
            self.db_path = db_path

    @document(doc)
    class Child(Base):
        pass

    # 空字符串 → check 失败
    try:
        Child("")
        assert False
    except ValueError as e:
        assert "Child" in str(e)
        assert "must not be empty" in str(e)

    # 错误类型 → 类型错误
    try:
        Child(123)
        assert False
    except ValueError as e:
        assert "expected str" in str(e)


def test_document_class_base_untouched():
    """类装饰器只包装子类 __init__，不影响基类。"""
    _fresh_registry()
    doc = UserDoc("测试类")
    doc.add_param("db_path", schema=Schema(str, check=lambda s: len(s) > 0,
                                             error="must not be empty"),
                  required=True)

    class Base:
        def __init__(self, db_path):
            self.db_path = db_path

    @document(doc)
    class Child(Base):
        pass

    # Base 不受影响：空字符串允许（基类无校验）
    b = Base("")
    assert b.db_path == ""


def test_document_class_owner_none():
    """类装饰器 owner=None（类本身就是 api_name，无需 register_flow 回填）。"""
    _fresh_registry()
    doc = UserDoc("测试类")

    class Base:
        def __init__(self, x):
            self.x = x

    @document(doc)
    class Cls(Base):
        pass

    assert doc._owner is None


def test_document_class_prototype():
    """类装饰器 help prototype 形如 ClassName(...)。"""
    _fresh_registry()
    doc = UserDoc("测试类")
    doc.add_param("db_path", schema=Schema(str), required=True)
    doc.add_param("mode", schema=Schema(str), required=False, default="r")

    class Base:
        def __init__(self, db_path, mode="r"):
            pass

    @document(doc)
    class MyClass(Base):
        pass

    proto = doc._render_prototype()
    assert proto.startswith("MyClass(")
    assert "db_path" in proto and "mode='r'" in proto


# ═══════════════════════════════════════════════════════════════════════════
# hidden 参数
# ═══════════════════════════════════════════════════════════════════════════

def test_hidden_param_validated_but_not_shown():
    """hidden 参数：校验生效，help prototype + Parameters 都不显示。"""
    _fresh_registry()
    doc = UserDoc("test")
    doc.add_param("x", schema=Schema(int), required=True, desc="可见")
    doc.add_param("secret", schema=Schema(str), required=False,
                  hidden=True, default="d", desc="隐藏")

    @document(doc)
    def f(self, x, secret="d"):
        return x

    # 校验生效
    try:
        f(None, 1, secret=123)
        assert False
    except ValueError as e:
        assert "secret" in str(e)

    # prototype 不含 secret
    proto = doc._render_prototype()
    assert "secret" not in proto
    assert "x" in proto

    # Parameters 不含 secret
    visible = doc._visible_params()
    assert all(p["name"] != "secret" for p in visible)
    assert any(p["name"] == "x" for p in visible)


# ═══════════════════════════════════════════════════════════════════════════
# add_keyword
# ═══════════════════════════════════════════════════════════════════════════

def test_add_keyword_list():
    _fresh_registry()
    doc = UserDoc("test")
    doc.add_keyword(["ras", "solver"])
    assert doc._keywords == ["ras", "solver"]


def test_add_keyword_dedup():
    _fresh_registry()
    doc = UserDoc("test")
    doc.add_keyword(["a", "b"])
    doc.add_keyword(["b", "c"])
    assert doc._keywords == ["a", "b", "c"]


def test_add_keyword_rejects_non_list():
    _fresh_registry()
    doc = UserDoc("test")
    try:
        doc.add_keyword("ras")          # str 不是 list
        assert False
    except TypeError:
        pass


# ═══════════════════════════════════════════════════════════════════════════
# register_flow owner 回填（模拟）
# ═══════════════════════════════════════════════════════════════════════════

def test_register_flow_owner_backfill():
    """模拟 @register_flow(cls) + @document(doc) 组合：owner 回填。"""
    _fresh_registry()
    doc = UserDoc("flow test")

    @document(doc)
    def my_flow(self, x):
        return x

    # 模拟 register_flow 的 owner 回填逻辑
    class MockProject:
        pass
    wrapped_doc = getattr(my_flow, "_fly_userdoc", None)
    assert wrapped_doc is not None
    wrapped_doc._owner = MockProject

    assert doc._owner is MockProject


def test_standalone_api_owner_none():
    """独立 API（仅 @document 无 register_flow）owner=None 正常注册。"""
    _fresh_registry()
    doc = UserDoc("standalone")

    @document(doc)
    def standalone_api(x):
        return x

    assert doc._owner is None
    assert "standalone_api" in userdoc._HELP_REGISTRY


# ═══════════════════════════════════════════════════════════════════════════
# help 系统输出
# ═══════════════════════════════════════════════════════════════════════════

def test_help_list_all(capsys=None):
    _fresh_registry()

    doc1 = UserDoc("第一个")
    doc1.add_keyword(["alpha"])

    @document(doc1)
    def alpha_api(x):
        return x

    doc2 = UserDoc("第二个")
    doc2.add_keyword(["beta"])

    @document(doc2)
    def beta_api(x):
        return x

    import io
    import contextlib
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        help()
    out = buf.getvalue()
    assert "alpha_api" in out
    assert "beta_api" in out


def test_help_default_compact():
    """help(keyword) 默认精简：仅描述 + prototype，不含参数/示例。"""
    _fresh_registry()
    doc = UserDoc("求解矩阵")
    doc.add_param("nsd", schema=Schema(int, check=lambda n: n >= 1,
                                       error="must be >= 1, got {value}"),
                  required=True, desc="子域数")
    doc.add_example("基础", code="f(4)", desc="调用")
    doc.add_keyword(["ras", "solver"])

    @document(doc)
    def solve(self, nsd):
        return nsd

    import io
    import contextlib
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        help("solve")
    out = buf.getvalue()
    # 精简模式含：标题 / 描述 / prototype
    assert "solve" in out
    assert "Prototype" in out
    assert "求解矩阵" in out
    # 精简模式不含：Parameters 段落 / 参数描述 / 示例 / 关键词
    # （注意 prototype 本身含参数名 nsd，这不算"参数详情"）
    assert "Parameters" not in out
    assert "子域数" not in out          # 参数描述
    assert "Examples" not in out
    assert "Keywords" not in out
    # 含提示如何看完整文档
    assert "detail=True" in out


def test_help_detail_true_full():
    """help(keyword, detail=True) 输出完整文档（参数 + 示例 + 关键词）。"""
    _fresh_registry()
    doc = UserDoc("求解矩阵")
    doc.add_param("nsd", schema=Schema(int, check=lambda n: n >= 1,
                                       error="must be >= 1, got {value}"),
                  required=True, desc="子域数")
    doc.add_example("基础", code="f(4)", desc="调用")
    doc.add_keyword(["ras", "solver"])

    @document(doc)
    def solve(self, nsd):
        return nsd

    import io
    import contextlib
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        help("solve", detail=True)
    out = buf.getvalue()
    assert "solve" in out
    assert "nsd" in out
    assert "Prototype" in out
    assert "Parameters" in out
    assert "Examples" in out
    assert "Keywords" in out
    assert "ras" in out
    assert "must be >= 1" in out or "子域数" in out


def test_help_all_outputs_every_detail():
    """help(all=True) 输出全部 API 的完整详情。"""
    _fresh_registry()

    doc1 = UserDoc("第一个 API")
    doc1.add_param("x", schema=Schema(int), desc="参数x")

    @document(doc1)
    def alpha(x):
        return x

    doc2 = UserDoc("第二个 API")
    doc2.add_param("y", schema=Schema(str), desc="参数y")

    @document(doc2)
    def beta(y):
        return y

    import io
    import contextlib
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        help(all=True)
    out = buf.getvalue()
    # 两个 API 都展开完整详情
    assert "alpha" in out
    assert "beta" in out
    assert "参数x" in out          # detail 内容
    assert "参数y" in out
    assert out.count("Prototype") == 2    # 每个 API 都有 prototype
    assert "detail=True" not in out       # all 模式不显示精简提示


def test_help_no_match():
    _fresh_registry()
    import io
    import contextlib
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        help("nonexistent_xyz")
    out = buf.getvalue()
    assert "No API matched" in out or "nonexistent_xyz" in out


def test_help_hidden_not_in_output():
    """help(keyword, detail=True) 详情输出不含 hidden 参数。"""
    _fresh_registry()
    doc = UserDoc("test")
    doc.add_param("visible", schema=Schema(int), required=True, desc="可见")
    doc.add_param("hidden_one", schema=Schema(str), hidden=True, default="x")

    @document(doc)
    def f(self, visible, hidden_one="x"):
        return visible

    import io
    import contextlib
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        help("f", detail=True)
    out = buf.getvalue()
    assert "hidden_one" not in out
    assert "visible" in out


# ═══════════════════════════════════════════════════════════════════════════
# register_module（业务模块统一接入范式 — userdoc 侧）
# ═══════════════════════════════════════════════════════════════════════════
# 注：_LazyAttr 代理与 get_script_namespace 的惰性加载行为依赖 fly 运行时
# （bootstrap.py 顶部 import fly），由 qa/api/test_userdoc_e2e.py 端到端覆盖
# （验证 SolverProject 实例化、help 惰性加载、类方法转发）。此处只测 userdoc
# 纯 Python 侧的 register_module 注册逻辑。

def test_register_module_populates_table():
    """register_module 把符号登记进 _REGISTERED_MODULES + 注册 help 延迟钩子。"""
    _fresh_registry()
    saved = dict(userdoc._REGISTERED_MODULES)
    saved_loaders = list(userdoc._HELP_LAZY_LOADERS)
    userdoc._REGISTERED_MODULES.clear()
    userdoc._HELP_LAZY_LOADERS.clear()
    userdoc._HELP_LAZY_DONE = False
    try:
        userdoc.register_module("MyClass", "mymod:MyClass")
        assert userdoc._REGISTERED_MODULES["MyClass"] == "mymod:MyClass"
        assert len(userdoc._HELP_LAZY_LOADERS) >= 1
    finally:
        userdoc._REGISTERED_MODULES.clear()
        userdoc._REGISTERED_MODULES.update(saved)
        userdoc._HELP_LAZY_LOADERS[:] = saved_loaders


def test_register_module_help_loader_triggers_import():
    """register_module 注册的 help 钩子在 _run_lazy_loaders 时 import 目标模块。"""
    import sys, types
    _fresh_registry()
    saved_loaders = list(userdoc._HELP_LAZY_LOADERS)
    userdoc._HELP_LAZY_LOADERS.clear()
    userdoc._HELP_LAZY_DONE = False
    # 构造假模块
    fake = types.ModuleType("fakebiz_for_test")
    sys.modules["fakebiz_for_test"] = fake
    loaded_before = "fakebiz_for_test" in sys.modules
    try:
        userdoc.register_module("FakeSym", "fakebiz_for_test:FakeSym")
        userdoc._HELP_LAZY_DONE = False
        userdoc._run_lazy_loaders()   # 触发钩子 → __import__("fakebiz_for_test")
        assert "fakebiz_for_test" in sys.modules
        # 幂等：再跑不重复 import（_HELP_LAZY_DONE=True）
        userdoc._run_lazy_loaders()
    finally:
        userdoc._HELP_LAZY_LOADERS[:] = saved_loaders
        userdoc._HELP_LAZY_DONE = False


# ═══════════════════════════════════════════════════════════════════════════
# 2026-09 覆盖率批次补充：容器边界 / keyword 匹配 / detail 渲染分支
# ═══════════════════════════════════════════════════════════════════════════

def test_container_schema_error_branches_and_str():
    """Schema.list/dict 非容器输入报错 + callable 自动包装 + __str__ 变体。"""
    # 裸 callable 元素规则自动包装成 Schema（_wrap_schema callable 分支）
    ls = Schema.list(lambda v: v > 0)
    assert ls.validate([1, 2]) == []
    assert ls.validate([1, -1]) != []          # 元素级 check 生效

    # 非容器输入的专用错误分支
    assert any("expected dict" in e for e in
               Schema.dict(required={"a": Schema(int)}).validate("not_a_dict"))
    assert any("expected list" in e for e in ls.validate("not_a_list"))
    assert any("expected list" in e for e in ls.validate(123))

    # __str__：无 desc → 类型签名；有 desc → 描述覆盖
    assert str(Schema.list(Schema(int))) == "list[int]"
    assert str(Schema.list(Schema(int), desc="nums")) == "nums"


def test_add_keyword_element_type_and_matches_keyword():
    """add_keyword 非字符串元素 TypeError + _matches_keyword 全分支。"""
    _fresh_registry()
    doc = UserDoc("solve_it")
    doc._api_name = "solve_it"   # 身份由 document 装饰器注入；此处手动指定
    try:
        doc.add_keyword(["ok", 42])
        raise AssertionError("non-str keyword must raise TypeError")
    except TypeError as e:
        assert "must be str" in str(e)

    class MyOwner:
        pass

    doc._owner = MyOwner
    doc.add_keyword(["solverish"])
    doc.add_param("nsd", Schema(int))
    # api_name 子串
    assert doc._matches_keyword("solve") is True
    # owner 类名子串
    assert doc._matches_keyword("myowner") is True
    # keyword 精确匹配（大小写归一）
    assert doc._matches_keyword("SOLVERISH") is True
    # 参数名子串
    assert doc._matches_keyword("ns") is True
    # 全 miss
    assert doc._matches_keyword("zzz") is False
    # keyword 精确语义：部分输入不误匹配
    assert doc._matches_keyword("solveris") is False


def test_format_param_line_and_detail_render_branches():
    """_format_param_line 三种 tag + desc；_format_detail 的 owner/desc/
    prototype/Examples/Keywords 分支。"""
    doc = UserDoc(desc="does everything")
    doc._api_name = "full_api"
    doc._owner = type("Widget", (), {})
    # 真实 Signature 对象（prototype 渲染需要 replace/bind 能力）
    import inspect
    doc._signature = inspect.Signature([
        inspect.Parameter("must", inspect.Parameter.POSITIONAL_OR_KEYWORD),
        inspect.Parameter("opts", inspect.Parameter.POSITIONAL_OR_KEYWORD),
        inspect.Parameter("free", inspect.Parameter.POSITIONAL_OR_KEYWORD),
    ])
    doc.add_param("must", Schema(int), required=True, desc="a required one")
    doc.add_param("opts", Schema(str), required=False,
                  default="hello", desc="has a default")
    doc.add_param("free", Schema(float), required=False, desc=None)
    doc.add_example("basic", "full_api(1)", desc="call it simply")
    doc.add_example("bare", "full_api(2)")          # 无 desc 的 example
    doc.add_keyword(["alpha", "beta"])

    line_req = userdoc._format_param_line(doc._params[0])
    assert "[required]" in line_req and "a required one" in line_req
    line_def = userdoc._format_param_line(doc._params[1])
    assert "[default 'hello']" in line_def
    line_opt = userdoc._format_param_line(doc._params[2])
    assert "[optional]" in line_opt

    text = userdoc._format_detail(doc, detail=True)
    assert "full_api — Widget" in text            # owner 标题
    assert "does everything" in text              # desc 块
    assert "Prototype:" in text
    assert "Parameters:" in text
    assert "[basic]  call it simply" in text      # example 含 desc
    assert "[bare]" in text                       # example 无 desc
    assert "    full_api(1)" in text              # code 缩进渲染
    assert "Keywords: alpha, beta" in text

    # 无 owner 标题、无 desc、无 signature（prototype 省略）、detail=False 提示
    bare = UserDoc()
    bare._api_name = "bare_api"
    bare._signature = None
    plain = userdoc._format_detail(bare, detail=False)
    assert "bare_api\n" in plain or plain.strip().startswith("═")
    assert "bare_api — " not in plain
    assert "(use detail=True for parameters & examples)" in plain
    assert "Prototype:" not in plain




if __name__ == "__main__":
    import traceback

    funcs = [(name, obj) for name, obj in sorted(globals().items())
             if name.startswith("test_") and callable(obj)]
    passed = 0
    failed = 0
    for name, fn in funcs:
        _fresh_registry()
        try:
            fn()
            passed += 1
            print(f"  [PASS] {name}")
        except Exception:
            failed += 1
            print(f"  [FAIL] {name}")
            traceback.print_exc()
    print(f"\n{passed} passed, {failed} failed, {len(funcs)} total")
    sys.exit(1 if failed else 0)
