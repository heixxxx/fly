"""UserDoc — 面向用户的 Help + Validator 系统。

三件套：

1. :class:`Schema` — 参数校验规则。归约为正交积木：
   - **类型 + 深度校验**：``Schema(type, *, check=, error=)``
   - **容器**：``Schema.dict(*, required=, optional=)`` / ``Schema.list(items, *, ...)``
   - **多分支**：``Schema.any_of(*schemas)``

   每个 schema（含容器、分支）都可挂 ``check``+``error``；嵌套位置（dict field value、
   list items、分支）一律接受 Schema 或裸 callable（自动包装）。

2. :class:`UserDoc` — 单个 API 的元数据收集器（描述 / 参数校验 / 使用范例 / 关键词）。
   冻结后注册进全局 :data:`_HELP_REGISTRY`。

3. :func:`document` — 装饰器，同时做 **doc 注册**（注入 api_name + signature，注册进 help
   系统）和 **参数校验**（调用时按 schema 校验）。与 :func:`fly.register_flow` 组合使用时
   由后者回填 owner（所属类）。

Help 查询入口 :func:`help`，``from fly import help`` 后 ``help()`` / ``help(keyword)`` 使用。

设计原则：纯 Python，无外部依赖，无 C++ 绑定。装饰器在 ``import`` 时执行，fly 启动
``import fly`` → ``import solver`` → ``solver.flows`` 导入 → ``@document`` 执行 →
registry 自动填充，无需显式调用。
"""

import functools
import inspect


# ═══════════════════════════════════════════════════════════════════════════
# Schema — 校验规则
# ═══════════════════════════════════════════════════════════════════════════

_NO_DEFAULT = object()   # 哨兵：参数无默认值


def _wrap_schema(value):
    """把裸 callable 包装成 Schema（自动包装约定）。

    凡期望 Schema 的位置传入非 Schema 的 callable 时，等价于
    ``Schema(object, check=fn)``——即"任意值通过类型检查，由 fn 决定是否合规"。
    Schema 实例与非 callable 原样返回。
    """
    if isinstance(value, Schema):
        return value
    if callable(value):
        return Schema(object, check=value)
    return value


def _type_name(t):
    """类型/类名串的显示名。int→'int'，(int,float)→'int | float'，'_Database'→'_Database'。"""
    if isinstance(t, str):
        return t
    if isinstance(t, tuple):
        return " | ".join(_type_name(x) for x in t)
    return getattr(t, "__name__", str(t))


def _matches_type(value, type_spec):
    """类型匹配：Python type/元组走 isinstance；str 走类名匹配（规避循环导入）。"""
    if isinstance(type_spec, str):
        return type(value).__name__ == type_spec
    if isinstance(type_spec, tuple):
        # 元组里可能混有 str（类名串）和 type
        for t in type_spec:
            if _matches_type(value, t):
                return True
        return False
    return isinstance(value, type_spec)


def _render_error(error, value):
    """渲染失败消息：模板 str（支持 {value} 占位）/ callable(value) -> str / 默认。"""
    if error is None:
        return f"failed check: {value!r}"
    if callable(error):
        return error(value)
    return error.format(value=repr(value))


class Schema:
    """校验规则。详见模块 docstring。

    Args:
        type: Python 类型 / 类型元组 / 类名字符串（如 ``"_Database"``）。
        check: 纯判定函数 ``fn(value) -> bool``。``True`` 通过，``False`` 失败。
        error: 失败消息，与 check 分离。可为模板 str（支持 ``{value}`` 占位）、
            ``callable(value) -> str``、或 ``None``（默认消息）。
        desc: 可选人类可读描述，覆盖默认 :meth:`__str__`（help 输出用）。
    """

    def __init__(self, type, *, check=None, error=None, desc=None):
        self._type = type
        self._check = check
        self._error = error
        self._desc = desc

    # ── 容器工厂 ──────────────────────────────────────────────────────

    @classmethod
    def dict(cls, *, required=None, optional=None, allow_extra=False,
             check=None, error=None, desc=None):
        """dict 容器 schema。

        Args:
            required: ``{key: schema_or_fn}``，必填 key 及其校验规则。
            optional: ``{key: schema_or_fn}``，可选 key 及其校验规则。
            allow_extra: ``False``（默认）时多余 key 报错（白名单模式）。
            check: 所有 field 校验通过后对整个 dict 执行的深度校验。
            error / desc: 同基础 schema。
        """
        return _DictSchema(
            required={k: _wrap_schema(v) for k, v in (required or {}).items()},
            optional={k: _wrap_schema(v) for k, v in (optional or {}).items()},
            allow_extra=allow_extra,
            check=check, error=error, desc=desc,
        )

    @classmethod
    def list(cls, items, *, min_len=None, max_len=None,
             check=None, error=None, desc=None):
        """list 容器 schema。

        Args:
            items: 单个元素 schema（所有元素同规则），裸 callable 自动包装。
            min_len / max_len: 长度范围（闭区间）。
            check / error / desc: 同基础 schema。
        """
        return _ListSchema(
            items=_wrap_schema(items),
            min_len=min_len, max_len=max_len,
            check=check, error=error, desc=desc,
        )

    @classmethod
    def any_of(cls, *schemas):
        """多分支 schema：任一分支完全通过即通过。

        每分支是完整 Schema（含各自 type+check+error）。全部分支失败时聚合错误。
        用于"参数可为多种形态"（如单元素 vs list）。``__str__`` = ``"(<s1>) | (<s2>)"``。

        Note: 不能用 ``Schema.or`` —— ``or`` 是 Python 保留关键字，无法定义。
        """
        return _AnyOfSchema([_wrap_schema(s) for s in schemas])

    # ── 校验核心 ──────────────────────────────────────────────────────

    def validate(self, value, path=""):
        """校验 value，返回错误消息列表（空 = 通过）。

        执行顺序：类型检查（失败短路）→ 深度校验 check。
        子类（dict/list/any_of）覆写以插入容器子项校验。
        """
        errors = []
        if not _matches_type(value, self._type):
            errors.append(f"{path or 'value'}: expected {_type_name(self._type)}, "
                          f"got {type(value).__name__}")
            return errors    # 短路：类型不对，check 无意义
        if self._check is not None and not self._check(value):
            errors.append(f"{path or 'value'}: {_render_error(self._error, value)}")
        return errors

    # ── 显示 ──────────────────────────────────────────────────────────

    def __str__(self):
        return self._desc if self._desc else _type_name(self._type)


class _DictSchema(Schema):
    """dict 容器。required/optional 直接是 {key: schema} 字典。"""

    def __init__(self, *, required, optional, allow_extra, check, error, desc):
        super().__init__(dict, check=check, error=error, desc=desc)
        self._required = required
        self._optional = optional
        self._allow_extra = allow_extra

    def validate(self, value, path=""):
        errors = []
        if not isinstance(value, dict):
            errors.append(f"{path or 'value'}: expected dict, got {type(value).__name__}")
            return errors
        # 1. required key 缺失
        for k in self._required:
            if k not in value:
                errors.append(f"{path or 'value'}: missing required key '{k}'")
        # 2. extra key（白名单模式）
        if not self._allow_extra:
            allowed = set(self._required) | set(self._optional)
            for k in value:
                if k not in allowed:
                    errors.append(f"{path or 'value'}: unexpected key '{k}'")
        # 3. 递归校验存在的 field（子项错误收集后仍跑深度 check）
        for k, schema in self._required.items():
            if k in value:
                errors.extend(schema.validate(value[k], f"{path}.{k}" if path else k))
        for k, schema in self._optional.items():
            if k in value:
                errors.extend(schema.validate(value[k], f"{path}.{k}" if path else k))
        # 4. 整体 check（子项全过后才跑）
        if not errors and self._check is not None and not self._check(value):
            errors.append(f"{path or 'value'}: {_render_error(self._error, value)}")
        return errors

    def __str__(self):
        if self._desc:
            return self._desc
        req = ", ".join(self._required.keys())
        opt = ", ".join(f"{k}?" for k in self._optional.keys())
        inner = ", ".join(p for p in (req, opt) if p)
        return f"dict{{{inner}}}"


class _ListSchema(Schema):
    """list 容器。items 为单个元素 schema，所有元素同规则。"""

    def __init__(self, *, items, min_len, max_len, check, error, desc):
        super().__init__(list, check=check, error=error, desc=desc)
        self._items = items
        self._min_len = min_len
        self._max_len = max_len

    def validate(self, value, path=""):
        errors = []
        if not isinstance(value, list):
            errors.append(f"{path or 'value'}: expected list, got {type(value).__name__}")
            return errors
        # 长度范围
        if self._min_len is not None and len(value) < self._min_len:
            errors.append(f"{path or 'value'}: length {len(value)} < min {self._min_len}")
        if self._max_len is not None and len(value) > self._max_len:
            errors.append(f"{path or 'value'}: length {len(value)} > max {self._max_len}")
        # 递归校验每个元素
        for i, item in enumerate(value):
            errors.extend(self._items.validate(item, f"{path}[{i}]"))
        # 整体 check
        if not errors and self._check is not None and not self._check(value):
            errors.append(f"{path or 'value'}: {_render_error(self._error, value)}")
        return errors

    def __str__(self):
        if self._desc:
            return self._desc
        return f"list[{self._items}]"


class _AnyOfSchema(Schema):
    """多分支：任一完全通过即通过，全失败则聚合错误。"""

    def __init__(self, schemas):
        super().__init__(object)   # 类型交给分支判断
        self._schemas = schemas

    def validate(self, value, path=""):
        # 任一分支完全通过即通过
        branch_errors = []
        for i, schema in enumerate(self._schemas):
            errs = schema.validate(value, path)
            if not errs:
                return []    # 通过
            branch_errors.append((i, schema, errs))
        # 全失败：聚合
        label = path or "value"
        lines = [f"{label}: expected one of: {' | '.join(f'({s})' for _, s, _ in branch_errors)}, "
                 f"got {value!r}"]
        for i, schema, errs in branch_errors:
            for e in errs:
                lines.append(f"  [{schema}]: {e.split(': ', 1)[-1]}")
        return lines

    def __str__(self):
        return " | ".join(f"({s})" for s in self._schemas)


# ═══════════════════════════════════════════════════════════════════════════
# UserDoc — 单 API 元数据收集器
# ═══════════════════════════════════════════════════════════════════════════

class UserDoc:
    """单个 API 的元数据收集器（描述 / 参数校验 / 范例 / 关键词）。

    构造时只接首行描述；api_name 与 signature 由 :func:`document` 装饰器注入，
    owner 由 :func:`fly.register_flow` 回填。``register_help()`` 后冻结内容追加
    （身份注入不受冻结限制）。

    Example::

        doc = UserDoc("求解矩阵，结果存入 name 的 db。")
        doc.add_param("nsd", schema=Schema(int, check=lambda n: n >= 1,
                                           error="must be >= 1, got {value}"),
                      required=True, desc="子域数")
        doc.add_example("基础", code="proj.solve(name='s', matrix_db=db, nsd=4)")
        doc.add_keyword(["ras", "solver"])

        @register_flow(SolverProject)
        @document(doc)
        def solve(self, name, matrix_db, nsd, ...):
            ...
    """

    def __init__(self, desc: str = ""):
        self._desc = desc
        self._params = []              # list[dict]：{name, schema, required, default, desc, hidden}
        self._examples = []            # list[dict]：{title, code, desc}
        self._keywords = []            # list[str]
        self._hidden_names = set()     # hidden 参数名集合（help prototype + Parameters 渲染时剔除）
        # 身份（由装饰器注入，register_help 不冻结这些）
        self._api_name = None
        self._owner = None
        self._signature = None
        self._frozen = False

    # ── 内容收集（冻结后报错）──────────────────────────────────────────

    def _check_frozen(self):
        if self._frozen:
            raise RuntimeError(f"UserDoc '{self._api_name}' is frozen; "
                               f"cannot modify after register_help()")

    def add_param(self, name, schema, *, required=True,
                  default=_NO_DEFAULT, desc="", hidden=False, none_ok=False):
        """添加一个参数的校验规则与文档。

        Args:
            name: 参数名。
            schema: :class:`Schema` 或裸 callable（自动包装）。
            required: 是否必填。
            default: 默认值（有默认值的参数通常 required=False）。
            desc: 参数说明（help 输出）。
            hidden: ``True`` 时 help 的 prototype 与 Parameters 均不显示该参数，
                但校验照常生效。
            none_ok: ``True`` 时允许该参数值为 ``None``（跳过 schema 校验直接通过）；
                默认 ``False``，即值为 ``None`` 报错。用于可选的 ``param=None`` 参数，
                需显式声明而非依赖隐式跳过。
        """
        self._check_frozen()
        self._params.append({
            "name": name,
            "schema": _wrap_schema(schema),
            "required": required,
            "default": default,
            "desc": desc,
            "hidden": hidden,
            "none_ok": none_ok,
        })
        if hidden:
            self._hidden_names.add(name)

    def add_example(self, title, code, desc=""):
        """添加使用范例。"""
        self._check_frozen()
        self._examples.append({"title": title, "code": code, "desc": desc})

    def add_keyword(self, keywords: list):
        """添加检索关键词。``keywords`` 为 ``list[str]``。"""
        self._check_frozen()
        if not isinstance(keywords, list):
            raise TypeError(f"add_keyword: keywords must be a list, got {type(keywords).__name__}")
        for kw in keywords:
            if not isinstance(kw, str):
                raise TypeError(f"add_keyword: each keyword must be str, got {type(kw).__name__}")
            if kw not in self._keywords:
                self._keywords.append(kw)

    # ── 注册 ──────────────────────────────────────────────────────────

    def register_help(self, api_name=None):
        """冻结并注册进全局 :data:`_HELP_REGISTRY`（幂等）。

        通常由 :func:`document` 装饰器调用，无需手动调用。api_name 由装饰器注入。
        """
        if api_name is not None:
            self._api_name = api_name
        self._frozen = True
        if self._api_name is not None and self._api_name not in _HELP_REGISTRY:
            _HELP_REGISTRY[self._api_name] = self

    # ── 校验执行 ──────────────────────────────────────────────────────

    def _validate_call(self, bound_args):
        """对已绑定的参数（inspect.BoundArguments）执行校验。

        按 doc._params 中的参数名校验；self 及不在 _params 中的参数自然跳过。
        失败时聚合所有错误抛 ValueError。

        None 的处理由各参数的 ``none_ok`` 显式控制：
          - 值为 None 且 ``none_ok=True`` → 直接通过（跳过 schema）
          - 值为 None 且 ``none_ok=False`` → 报错 "must not be None"
          - 非 None 值 → 按 schema 校验
        required 参数未传入且无默认值 → "missing required argument"。
        """
        bound_args.apply_defaults()
        arguments = bound_args.arguments
        all_errors = []
        for p in self._params:
            name = p["name"]
            if name not in arguments:
                if p["required"] and p["default"] is _NO_DEFAULT:
                    all_errors.append(f"{name}: missing required argument")
                continue
            value = arguments[name]
            # None 由 none_ok 显式控制
            if value is None:
                if not p["none_ok"]:
                    all_errors.append(f"{name}: must not be None")
                continue
            errs = p["schema"].validate(value, name)
            all_errors.extend(errs)
        if all_errors:
            raise ValueError(
                f"{self._api_name}: parameter validation failed:\n  - "
                + "\n  - ".join(all_errors)
            )

    # ── help 渲染辅助 ─────────────────────────────────────────────────

    def _visible_params(self):
        """help 显示用的参数（排除 hidden）。"""
        return [p for p in self._params if not p["hidden"]]

    def _render_prototype(self):
        """渲染 prototype（剔除 self 与 hidden 参数）。"""
        if self._signature is None:
            return f"{self._api_name}(...)"
        params = [p for n, p in self._signature.parameters.items()
                  if n != "self" and n not in self._hidden_names]
        short = self._signature.replace(parameters=params)
        return f"{self._api_name}{short}"

    def _matches_keyword(self, keyword):
        """该 doc 是否匹配关键词。

        api_name / owner 类名 / 参数名用子串匹配（这些是标识符，部分输入合理）；
        keywords 用精确匹配（避免 'solve' 误匹配 keyword 'solver'）。
        """
        kw = keyword.lower()
        if kw in (self._api_name or "").lower():
            return True
        if self._owner is not None and kw in self._owner.__name__.lower():
            return True
        if any(k.lower() == kw for k in self._keywords):
            return True
        if any(kw in p["name"].lower() for p in self._params):
            return True
        return False


# ═══════════════════════════════════════════════════════════════════════════
# document 装饰器（doc 注册 + 参数校验）
# ═══════════════════════════════════════════════════════════════════════════

def document(doc: UserDoc):
    """装饰器：同时做 **doc 注册**（注入 api_name + signature，注册进 help 系统）和
    **参数校验**（调用时按 schema 校验）。自动检测目标是函数还是类：

    - **函数**（flow / method / 普通函数）：包装函数本体，校验其参数。
    - **类**：api_name 取类名，signature 取 ``inspect.signature(cls)``（即 ``__init__``
      签名，自动去 self），校验 ``__init__`` 的参数（在构造时拦截非法输入）。

    与 :func:`fly.register_flow` 组合时，``@document`` 在内层先执行（注册，owner=None），
    ``@register_flow`` 在外层后执行并回填 owner。

    Example（函数）::

        @register_flow(SolverProject)
        @document(solve_doc)
        def solve(self, name, matrix_db, nsd, ...):
            ...

    Example（类）::

        @document(solver_project_doc)
        class SolverProject(Project):
            pass
    """
    def decorator(func):
        # ── 类：包装 __init__，signature 取 inspect.signature(cls) ──
        if isinstance(func, type):
            cls = func
            doc._api_name = cls.__name__
            doc._signature = inspect.signature(cls)     # __init__ 签名，已去 self
            doc.register_help()

            orig_init = cls.__init__

            @functools.wraps(orig_init)
            def init_wrapper(self, *args, **kwargs):
                try:
                    bound = doc._signature.bind(*args, **kwargs)
                except TypeError as e:
                    raise ValueError(f"{doc._api_name}: parameter binding failed: {e}") from None
                doc._validate_call(bound)
                return orig_init(self, *args, **kwargs)

            cls.__init__ = init_wrapper
            cls._fly_userdoc = doc      # 类级属性，与函数的 wrapper._fly_userdoc 对齐

            # 回填类内方法的 owner：类体内被 @document 装饰的方法此时 owner=None，
            # 类装饰器执行时统一回填为该类，使 help 能显示方法归属。与 register_flow
            # 回填 flow owner 的机制一致。
            for attr_name in dir(cls):
                try:
                    method = getattr(cls, attr_name)
                except AttributeError:
                    continue
                method_doc = getattr(method, "_fly_userdoc", None)
                if method_doc is not None and method_doc._owner is None:
                    method_doc._owner = cls
            return cls

        # ── 函数：原逻辑 ──
        doc._api_name = func.__name__
        doc._signature = inspect.signature(func)
        doc.register_help()

        @functools.wraps(func)
        def wrapper(*args, **kwargs):
            try:
                bound = doc._signature.bind(*args, **kwargs)
            except TypeError as e:
                # sig.bind 对缺失必填参数等抛 TypeError；统一转成我们的 ValueError 风格
                raise ValueError(f"{doc._api_name}: parameter binding failed: {e}") from None
            doc._validate_call(bound)
            return func(*args, **kwargs)

        wrapper._fly_userdoc = doc    # 沿用"挂私有属性"约定，供 register_flow 回填 owner
        return wrapper
    return decorator


# ═══════════════════════════════════════════════════════════════════════════
# Help 系统
# ═══════════════════════════════════════════════════════════════════════════

_HELP_REGISTRY: dict = {}   # api_name -> UserDoc（模块级全局）

# 延迟加载钩子：help 查询前调用，触发重业务模块（如 solver）的 @document 注册。
# 由 bootstrap 注册，避免 userdoc 直接依赖业务模块（保持 userdoc 纯粹）。
_HELP_LAZY_LOADERS: list = []   # list[callable]
_HELP_LAZY_DONE = False


def register_lazy_loader(loader):
    """注册一个延迟加载钩子（callable，无参）。help 首次查询时统一触发，幂等。"""
    global _HELP_LAZY_DONE
    _HELP_LAZY_LOADERS.append(loader)
    _HELP_LAZY_DONE = False     # 新钩子注册后重置，确保下次 help 触发


def _run_lazy_loaders():
    """触发所有延迟加载钩子（幂等，仅首次执行）。"""
    global _HELP_LAZY_DONE
    if _HELP_LAZY_DONE:
        return
    for loader in _HELP_LAZY_LOADERS:
        try:
            loader()
        except Exception:
            pass
    _HELP_LAZY_DONE = True


def _format_param_line(p):
    """渲染单行参数说明：name : type [required/default] desc。"""
    schema = p["schema"]
    type_str = str(schema)
    if p["required"]:
        tag = "[required]"
    elif p["default"] is not _NO_DEFAULT:
        tag = f"[default {p['default']!r}]"
    else:
        tag = "[optional]"
    line = f"    {p['name']:<14} : {type_str:<14} {tag}"
    if p["desc"]:
        line += f"   {p['desc']}"
    return line


def _format_detail(doc, detail=False):
    """渲染单个 doc 的详情。

    Args:
        doc: UserDoc 实例。
        detail: ``False``（默认）仅输出标题 + 描述 + prototype（快速浏览）；
            ``True`` 追加 Parameters + Examples + Keywords（完整文档）。
    """
    sep = "═" * 50
    lines = [sep]
    # 标题：api_name — owner（owner=None 时省略）
    if doc._owner is not None:
        lines.append(f"{doc._api_name} — {doc._owner.__name__}")
    else:
        lines.append(f"{doc._api_name}")
    lines.append(sep)
    # 描述
    if doc._desc:
        lines.append(doc._desc)
        lines.append("")
    # Prototype（剔除 self 与 hidden）
    if doc._signature is not None:
        lines.append("Prototype:")
        lines.append(f"  {doc._render_prototype()}")
        lines.append("")
    # detail=True 时追加参数 / 示例 / 关键词
    if not detail:
        # 精简模式：提示如何看完整文档
        lines.append("(use detail=True for parameters & examples)")
        return "\n".join(lines)
    # Parameters（剔除 hidden）
    visible = doc._visible_params()
    if visible:
        lines.append("Parameters:")
        for p in visible:
            lines.append(_format_param_line(p))
        lines.append("")
    # Examples
    if doc._examples:
        lines.append("Examples:")
        for ex in doc._examples:
            title = ex["title"]
            if ex["desc"]:
                lines.append(f"  [{title}]  {ex['desc']}")
            else:
                lines.append(f"  [{title}]")
            for code_line in ex["code"].splitlines():
                lines.append(f"    {code_line}")
        lines.append("")
    # Keywords
    if doc._keywords:
        lines.append(f"Keywords: {', '.join(doc._keywords)}")
    return "\n".join(lines)


def help(keyword=None, detail=False, all=False):
    """Help 查询入口。

    - ``help()``：列出所有已注册 API（api_name + owner + 一句话描述），字母序。
    - ``help(keyword)``：查找 API。
      若某 API 的 api_name **完全等于** keyword，直接返回其详情（精确优先）；
      否则模糊匹配 api_name / owner / 参数名 / keywords，单条返回详情，多条列表。
    - ``help(all=True)``：输出**全部**已注册 API 的完整详情（逐个展开），用于一次性
      导览。等价于对每个 API 调 ``help(name, detail=True)``。
    - ``detail=True``：匹配到单个 API 时，输出完整文档（含 Parameters / Examples /
      Keywords）；默认 ``False`` 仅输出描述 + prototype（快速浏览）。

    经 ``from fly import help`` 引入，不污染 builtins。
    """
    # 触发延迟加载钩子（如 solver 的 @document 注册），确保查询前 registry 完整
    _run_lazy_loaders()

    # all=True：输出全部 API 的完整详情
    if all:
        if not _HELP_REGISTRY:
            print("No API registered. Import modules to populate help registry.")
            return
        names = sorted(_HELP_REGISTRY.keys())
        blocks = []
        for name in names:
            blocks.append(_format_detail(_HELP_REGISTRY[name], detail=True))
        print("\n\n".join(blocks))
        return

    if keyword is None:
        # 列出全部（精简列表）
        if not _HELP_REGISTRY:
            print("No API registered. Import modules to populate help registry.")
            return
        names = sorted(_HELP_REGISTRY.keys())
        print(f"Registered APIs ({len(names)}):")
        for name in names:
            doc = _HELP_REGISTRY[name]
            owner = doc._owner.__name__ if doc._owner else "-"
            desc = doc._desc.splitlines()[0] if doc._desc else ""
            print(f"  {name:<24} {owner:<20} {desc}")
        return
    # 关键词查找
    keyword = str(keyword)
    # 精确匹配 api_name 优先：直接返回详情
    if keyword in _HELP_REGISTRY:
        print(_format_detail(_HELP_REGISTRY[keyword], detail=detail))
        return
    # 模糊匹配
    matched = [doc for doc in _HELP_REGISTRY.values() if doc._matches_keyword(keyword)]
    if not matched:
        print(f"No API matched '{keyword}'. Call help() to list all APIs.")
        return
    if len(matched) == 1:
        print(_format_detail(matched[0], detail=detail))
        return
    # 多个匹配：列表 + 提示
    print(f"Found {len(matched)} APIs matching '{keyword}':")
    for doc in sorted(matched, key=lambda d: d._api_name):
        owner = doc._owner.__name__ if doc._owner else "-"
        print(f"  {doc._api_name:<24} {owner:<20}")
    print(f"\nUse help('<exact_name>') for details.")
