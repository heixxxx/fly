"""emir 建库 API 入口参数的命名 validator 函数库。

header（UserDoc add_param 的 Schema）与 alpha_settings 的 AlphaSetting
validator 共用本库——**值域单一来源**：同一条规则只在此（或各模块
alpha_settings.py 的键级 validator）定义一次，schema 与声明式键表引用
同一函数，改值域只动一处。

规范（dev-rules §3「建库 API 配置参数标准」，2026-09-17 裁定）：
  - Schema 声明一律容器工厂（``Schema.dict``/``Schema.list``/
    ``Schema.any_of``）+ 命名 validator，**禁止内联 lambda**；
  - 白名单严格模式：``allow_extra=False``（dict 默认），未知键一律报错；
  - 校验失败由 header 直接 raise 终止（document 装饰器聚合 ValueError），
    不走 user message。

命名约定：``is_*`` 返回 bool（check 用）；``ensure_*`` 无返回值、失败
抛异常（入口函数体用——文件可读性校验需区分 FileNotFoundError /
PermissionError 两类异常，bool check 表达不了）。
"""

import os


# ── is_*：bool 判定（Schema check / AlphaSetting validator 共用）─────────


def is_nonempty_str(value):
    """非空 str（``""`` 拒绝；非 str 拒绝）。"""
    return isinstance(value, str) and len(value) > 0


def is_bool(value):
    """bool（bool 是 int 子类——int 伪装值一律拒绝）。"""
    return isinstance(value, bool)


def is_plain_int(value):
    """int 且非 bool（0/负数不拒——未设置/回退语义在消费侧）。"""
    return isinstance(value, int) and not isinstance(value, bool)


def is_positive_int(value):
    """int 且非 bool 且 >= 1（bool 伪装拒绝）。"""
    return is_plain_int(value) and value >= 1


def is_int_in_range(minimum=None, maximum=None):
    """工厂：int 且非 bool 且落在闭区间 [minimum, maximum]（界缺省不限）。"""
    def _check(value):
        if not is_plain_int(value):
            return False
        if minimum is not None and value < minimum:
            return False
        if maximum is not None and value > maximum:
            return False
        return True
    return _check


def is_one_of(*choices):
    """工厂：值与 choices 中之一相等（== 相等比较——适合 str 枚举集）。"""
    def _check(value):
        return any(value == choice for choice in choices)
    return _check


def is_none_or_str(value):
    """None（未设置）或 str——'{x}x{y}' 一类格式判别在消费侧的类型级护栏。"""
    return value is None or isinstance(value, str)


def is_nonneg_finite_number(value):
    """非负有限数（int/float；NaN/inf/负数/bool 伪装一律拒绝）。"""
    if not isinstance(value, (int, float)) or isinstance(value, bool):
        return False
    import math
    return not (math.isnan(value) or math.isinf(value) or value < 0)


def is_nonempty_str_list(value):
    """list 且每项为非空 str（列表本身可为空——长度约束由 Schema.list 的
    min_len/max_len 表达，职责分离）。"""
    if not isinstance(value, list):
        return False
    return all(is_nonempty_str(item) for item in value)


def is_valid_binding_desc(value):
    """timing_files 块绑定描述符 dict 的整体校验（键级类型已由 Schema.dict
    子项校验过，这里只判互斥）：block_inst 与 block_cell 恰传其一——
    同传（绑定目标歧义）与都缺（纯路径该用 str 元素）均不合法。"""
    has_inst = "block_inst" in value
    has_cell = "block_cell" in value
    return has_inst != has_cell


def is_readable_file(value):
    """文件存在且可读（isfile + os.access R_OK）。"""
    return os.path.isfile(value) and os.access(value, os.R_OK)


# ── ensure_*：失败抛异常（入口函数体用）──────────────────────────────────


def ensure_readable_file(path, api_name, param_name):
    """入口文件参数的显式可读校验（统一报错文案含 api/参数名/路径）。

    - 不存在 → FileNotFoundError；
    - 存在但不可读 → PermissionError。

    dev-rules §3：文件等资源问题在 Step 1（master 侧）抛异常拦截，
    不建库、不起任务。
    """
    if not os.path.isfile(path):
        raise FileNotFoundError(
            f"{api_name}: {param_name}: file not found: {path}")
    if not os.access(path, os.R_OK):
        raise PermissionError(
            f"{api_name}: {param_name}: file not readable: {path}")
