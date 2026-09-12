"""AlphaSettings — 建库 alpha 配置的声明式基座（2026-09-13 裁定）。

alpha 项以**五要素**声明（dev-rules §3「建库 API 配置参数标准」）：
setting 名（类属性名）/ 默认值 / 值类型及约束介绍（value_type + constraint，
str）/ 值校验器（validator，None = 不校验）/ setting 介绍（description）。

各 db 子模块定义自己的子类（例：design 的 ``DSAlphaSettings``）+ 模块级
``get_default_alpha_settings()`` 工厂。建库入口（build_<角色>_db）接线：
默认实例 ``apply(alpha)`` 逐键校验覆盖 → settings 对象以固定对象名
``"alpha_settings"`` 写入 db → 消费点 ``read_object`` 读回后 ``normalize()``
兜底（旧对象缺键补默认、未知属性丢弃，向前兼容）。

校验语义（裁定 5，dev-rules §7 兜底）：
  - validator 拒绝（返回 False / 抛异常）→ rejected，保留默认值继续，不 raise；
  - 未知键 → unknown，调用方发 user warn message 后忽略；
  - None 默认值项（如 target_partitions）业务使用点自行判断值与处理。

本基座为**纯逻辑**：apply 只返回结构化结果
``{"rejected": {键: 原因}, "unknown": [键]}``，不直接发 message——透出归
build 接线处按模块前缀处理（design 复用 DSGN::0013、lib 用 LIBR::0005）。

序列化：pickle 友好——实例仅持当前值 dict（``_values``），描述符留在类
层（pickle 按引用存类），write_object/read_object 直接受益。
"""

import copy


class AlphaSetting:
    """alpha 单项描述符（五要素载体；数据描述符）。

    类属性挂在 AlphaSettings 子类上即完成声明；``__set_name__`` 在类创建
    时绑定 setting 名。实例读写经描述符直达持有方的 ``_values``（缺失/
    异常兜底 deepcopy 默认——读回旧对象的缺键场景不炸面）。
    """

    def __init__(self, default, value_type, constraint="", validator=None,
                 description="", key_validator=None):
        self.default = default
        self.value_type = value_type      # 值类型介绍（str，进拒绝原因文案）
        self.constraint = constraint      # 约束介绍（str，可空）
        self.validator = validator        # callable(value) -> bool；None 不校验
        self.description = description    # setting 介绍（help/文档用）
        # dict 型逐 key 键（review 2026-09-13 修复）：设置后 apply 走合并
        # 语义——deepcopy 当前值起步，用户 dict 逐 key 校验覆盖，缺 key
        # 保留（旧 _parse_channel_weights 语义恢复）；非法 key 逐个回退
        # 默认并计入 rejected（明细键名 "name.key"）；未知子键提醒忽略。
        # 与 validator 互斥使用（key_validator 设置时 validator 不参与）。
        self.key_validator = key_validator  # callable(value) -> bool
        self.name = None                  # __set_name__ 绑定

    def __set_name__(self, owner, name):
        self.name = name

    def __get__(self, obj, objtype=None):
        if obj is None:
            return self
        try:
            return obj._values[self.name]
        except Exception:
            return copy.deepcopy(self.default)

    def __set__(self, obj, value):
        # 直接赋值不经 validator——apply 是唯一校验入口（有意设计：
        # 校验与提醒绑定，防止绕过 message 透出），业务侧请勿以 setattr
        # 方式改值（review 提示固化）
        obj._values[self.name] = copy.deepcopy(value)

    def reject_reason(self, value):
        """拒绝原因文案（validator False 路径；抛异常路径由 apply 单独取）。"""
        reason = f"must be {self.value_type}"
        if self.constraint:
            reason += f", {self.constraint}"
        return f"{reason}, got {value!r}"


class AlphaSettings:
    """alpha 设置基类：子类以 AlphaSetting 类属性声明字段，实例仅存当前值。

    ``__init_subclass__`` 按类属性定义序收集字段表（含继承；同名后定义覆
    盖先定义），存为 ``_ALPHA_FIELDS``（dict 保序）。字段默认值在实例创建
    时 deepcopy——多次创建同种 db 互不污染默认值（dict 型默认值关键）。
    """

    def __init_subclass__(cls, **kwargs):
        super().__init_subclass__(**kwargs)
        fields = {}
        for klass in reversed(cls.__mro__):
            for name, attr in vars(klass).items():
                if isinstance(attr, AlphaSetting):
                    fields.pop(name, None)   # 后定义覆盖先定义（保最新位置）
                    fields[name] = attr
        cls._ALPHA_FIELDS = fields

    @classmethod
    def setting_names(cls):
        """声明字段名元组（定义序）。"""
        return tuple(cls._ALPHA_FIELDS)

    def __init__(self):
        if not hasattr(type(self), "_ALPHA_FIELDS"):
            raise TypeError(
                f"{type(self).__name__} is the base class — declare "
                f"AlphaSetting class attributes in a subclass "
                f"(see dev-rules §3)")
        self._values = {name: copy.deepcopy(field.default)
                        for name, field in self._ALPHA_FIELDS.items()}

    def apply(self, user_values):
        """逐键校验覆盖用户 alpha dict（纯逻辑，不发 message）。

        返回 ``{"rejected": {键: 原因}, "unknown": [键]}``：未知键入
        unknown（调用方透出 user warn 后忽略）；validator 返回 False /
        抛异常 → rejected（保留当前值）；合法 → deepcopy 覆盖当前值。
        带 ``key_validator`` 的 dict 型键走合并语义（见 AlphaSetting 注
        释）：缺 key 保留当前值、非法子键逐个回退该子键当前值（明细
        键名 "name.key"）、未知子键提醒忽略。user_values 为 None/空 dict
        时不做任何事。
        """
        rejected = {}
        unknown = []
        if not user_values:
            return {"rejected": rejected, "unknown": unknown}
        for name, value in user_values.items():
            field = self._ALPHA_FIELDS.get(name)
            if field is None:
                unknown.append(name)
                continue
            if field.key_validator is not None:
                if not isinstance(value, dict):
                    rejected[name] = field.reject_reason(value)
                    continue
                merged = copy.deepcopy(self._values.get(name,
                                                        field.default))
                for k, v in value.items():
                    if k not in merged:
                        rejected[f"{name}.{k}"] = \
                            f"unknown key of {name}, ignored"
                        continue
                    try:
                        ok = field.key_validator(v)
                    except Exception as exc:
                        rejected[f"{name}.{k}"] = \
                            f"{type(exc).__name__}: {exc}"
                        continue
                    if not ok:
                        rejected[f"{name}.{k}"] = (
                            f"{name}[{k!r}] must be {field.value_type}"
                            + (f", {field.constraint}"
                               if field.constraint else "")
                            + f", got {v!r} — kept "
                              f"{merged[k]!r}")
                        continue
                    merged[k] = copy.deepcopy(v)
                self._values[name] = merged
                continue
            if field.validator is not None:
                try:
                    ok = field.validator(value)
                except Exception as exc:
                    rejected[name] = f"{type(exc).__name__}: {exc}"
                    continue
                if not ok:
                    rejected[name] = field.reject_reason(value)
                    continue
            self._values[name] = copy.deepcopy(value)
        return {"rejected": rejected, "unknown": unknown}

    @staticmethod
    def format_apply_result(result):
        """apply 结果 → 单条 user warn 文案（无问题返回 ""）。

        只做文案汇总，消息发送归调用方（模块前缀 + message id 在接线处
        决定）。"""
        parts = [f"{name}: {reason}"
                 for name, reason in result["rejected"].items()]
        if result["unknown"]:
            parts.append("unknown keys ignored: "
                         + ", ".join(result["unknown"]))
        return "; ".join(parts)

    def normalize(self):
        """读回兜底（向前兼容）：未知属性丢弃、缺失字段补 deepcopy 默认。

        旧版本 db 里的 alpha_settings 对象（缺新键/含已删键）读回后调用
        一次即恢复完整字段面。"""
        for name in list(self._values):
            if name not in self._ALPHA_FIELDS:
                del self._values[name]
        for name, field in self._ALPHA_FIELDS.items():
            if name not in self._values:
                self._values[name] = copy.deepcopy(field.default)

    def to_dict(self):
        """当前值导出 dict（deepcopy——修改导出值不回灌实例）。"""
        return {name: copy.deepcopy(getattr(self, name))
                for name in self._ALPHA_FIELDS}
