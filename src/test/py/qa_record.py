"""QA 基准比对机制：记录接口、值规范化、比对预设、基准文件读写、差异报告、重录规则。

设计见 QA 改造任务书第 5 节。核心流程：子阶段脚本（fly 进程内）调用 ``record()``
把待检查的输出写入结果文件；``qa/runqa`` 在子阶段结束后按 ``check_list`` 逐项与
入库的基准文件（gold）比对，差异非空即用例失败；``--regold`` 用上次运行的结果
文件重录基准。

本模块被两种方式加载，加载环境决定它能用什么：

1. fly 子进程内（子阶段脚本 ``from test import record``）——作为 ``test.py.qa_record``
   正常导入；
2. ``qa/runqa`` 进程内——runqa 没有 ``.py`` 扩展名不能被导入，共享逻辑全部放在
   本模块，runqa 用 importlib 按源码树路径加载。

因此【模块顶层只允许标准库】；需要 fly 的导入（``get_config``）放在函数内部
延迟导入。
"""
import hashlib
import inspect
import json
import math
import os
import pathlib
import sys

# 仓库根：本文件位于 src/test/py/ 下，realpath 解析 install 布局的符号链接后
# 上溯 4 级（py → test → src → 仓库根），与 src/test/py/qa_tmp.py 的推导一致。
_REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.dirname(os.path.realpath(__file__)))))

# 差异展示上限：每个结果文件最多展示 20 处，超出给总数（完整列表落 check 报告文件）。
_MAX_SHOWN_DIFFS = 20
# 差异文案里单个值的序列化截断长度。
_MAX_VALUE_TEXT = 80

# 结果文件里每个结果文件名已记录的键集合（防止同文件重复记录）。
# 仅在执行子阶段脚本的 fly 进程内使用：脚本是单线程扁平脚本，无需加锁；
# runqa 进程按路径加载本模块只调用比对/重录函数，不触碰这个状态。
_recorded_keys = {}


class RegoldRejected(Exception):
    """重录被拒绝（判定型条目为假）。拒绝写入基准，否则该检查从此永久失效。"""


# ---------------------------------------------------------------------------
# 值规范化与路径掩蔽
# ---------------------------------------------------------------------------

def _mask_mappings():
    """路径掩蔽映射表：被替换路径 → 占位符，按路径从长到短排序。

    环境变量不存在时跳过对应项。目的是换机器、换检出位置后基准仍然有效。
    """
    mappings = []
    for var, placeholder in (
        ("FLY_CASE_SUB_DIR", "<sub_log_dir>"),
        ("FLY_CASE_LOG_DIR", "<case_log_dir>"),
        ("FLY_CASE_DIR", "<case_dir>"),
        ("FLY_BUILD", "<build>"),
    ):
        value = os.environ.get(var)
        if value:
            mappings.append((value, placeholder))
    mappings.append((_REPO_ROOT, "<repo>"))
    mappings.sort(key=lambda item: len(item[0]), reverse=True)
    return mappings


def normalize(value, mask_paths=True):
    """把任意 Python 值规范化为可入 JSON 的形态（见任务书 5.2 规则表）。

    基准里存的是规范化之后的值，结果文件与基准因此同形可比。
    不支持的类型抛 ``TypeError``（提示逐字段记录），不静默跳过。
    """
    mask = _make_mask_fn() if mask_paths else None
    return _normalize_value(value, mask)


def _make_mask_fn():
    mappings = _mask_mappings()

    def mask(text):
        for path_value, placeholder in mappings:
            if path_value in text:
                text = text.replace(path_value, placeholder)
        return text

    return mask


def _normalize_value(value, mask):
    if value is None or isinstance(value, (bool, int, float)):
        return value
    if isinstance(value, str):
        return mask(value) if mask else value
    if isinstance(value, (list, tuple)):
        return [_normalize_value(item, mask) for item in value]
    if isinstance(value, (set, frozenset)):
        # 集合无序：规范化后按文本表示排序，保证结果确定。
        items = [_normalize_value(item, mask) for item in value]
        return sorted(items, key=lambda item: json.dumps(item, ensure_ascii=False, sort_keys=True))
    if isinstance(value, dict):
        out = {}
        seen_sources = {}
        for raw_key, raw_value in value.items():
            key = _json_key(raw_key)
            if mask:
                key = mask(key)
            if key in seen_sources:
                raise ValueError(
                    f"字典键规范化后冲突：{key!r}（来自 {seen_sources[key]!r} 与 {raw_key!r}）")
            seen_sources[key] = raw_key
            out[key] = _normalize_value(raw_value, mask)
        return out
    # numpy（不 import，按模块名识别）：数组 → 嵌套列表；标量 → Python 标量。
    if type(value).__module__ == "numpy":
        if hasattr(value, "tolist"):
            return _normalize_value(value.tolist(), mask)
        if hasattr(value, "item"):
            return value.item()
    if isinstance(value, (bytes, bytearray, memoryview)):
        raw = bytes(value)
        return {"len": len(raw), "sha256": hashlib.sha256(raw).hexdigest()}
    if isinstance(value, pathlib.PurePath):
        text = str(value)
        return mask(text) if mask else text
    raise TypeError(
        f"record 不支持类型 {type(value).__name__}（模块 {type(value).__module__}），"
        "请逐字段记录（取值、类型名、布尔结果等），不静默跳过")


def _json_key(key):
    """字典键转字符串：字符串原样；整数、浮点、布尔、None 按标准库 json 的规则。"""
    if isinstance(key, str):
        return key
    if isinstance(key, bool):
        return "true" if key else "false"
    if isinstance(key, (int, float)):
        return repr(key)
    if key is None:
        return "null"
    raise TypeError(
        f"字典键类型不支持：{type(key).__name__}（只允许字符串、整数、浮点、布尔、None）")


# ---------------------------------------------------------------------------
# 记录接口（仅在执行子阶段脚本的 fly 进程内调用）
# ---------------------------------------------------------------------------

def script_stem():
    """子阶段脚本名去掉 .py（测例产物命名约定的主题 S）。

    优先取运行器传给 fly 的脚本路径（``sys._fly_script_path``，见
    src/fly/main.py 的用户脚本装载），没有它时退回 ``sys.argv[0]``。
    """
    script_path = getattr(sys, "_fly_script_path", None) or (sys.argv[0] if sys.argv else "")
    if not script_path:
        raise RuntimeError("无法确定子阶段脚本名（sys._fly_script_path 与 sys.argv[0] 都为空）")
    base = os.path.basename(script_path)
    return base[:-3] if base.endswith(".py") else base


def resolve_sub_dir():
    """结果文件落点目录：运行器注入的 FLY_CASE_SUB_DIR；手动直跑时退到 fly 日志目录。"""
    env = os.environ.get("FLY_CASE_SUB_DIR")
    if env:
        return env
    # 延迟导入：runqa 进程按路径加载本模块时没有 fly 的 C++ 扩展，不能在顶层 import。
    from core import get_config
    return get_config().get_str("log_dir")


def record(key, value, *, detail=None, file=None, mask_paths=True):
    """把一个待检查的键值写入结果文件（立即写盘，先写临时文件再 os.replace）。

    key       非空字符串；同一结果文件里重复记录同一键抛 RuntimeError。
    value     任意受支持的值（先 normalize）。
    detail    可选诊断信息，只用于失败报告，不参与比对，不进基准。
    file      结果文件名（纯文件名，不允许路径分隔符）。缺省是
              ``<S>.result.json``（S = 子阶段脚本名去掉 .py，见测例产物命名
              约定）；传入其他文件名时必须以 ``<S>.`` 开头，否则抛 ValueError。
    mask_paths 路径掩蔽（缺省开启），见 normalize。

    诊断信息写入伴随文件 ``<S>.detail.json``（一个子阶段一份），键为
    ``<结果文件名>/<键>``，只用于失败报告，不比对、不重录。

    只能在执行子阶段脚本的 fly 主进程里调用；任务函数内不能调用，任务的
    结果通过读库带回主进程后再记录。
    """
    if not isinstance(key, str) or not key:
        raise ValueError(f"record 的键必须是非空字符串，收到 {key!r}")
    stem = script_stem()
    if file is None:
        rel = f"{stem}.result.json"
    else:
        rel = validate_relative_path(file, what="结果文件")
        prefix = stem + "."
        if not rel.startswith(prefix):
            raise ValueError(
                f"结果文件必须以 {prefix!r} 开头（测例产物命名约定），收到 {rel!r}")
    sub_dir = resolve_sub_dir()
    result_path = os.path.join(sub_dir, rel)

    known = _recorded_keys.setdefault(rel, set())
    data = _read_json_strict(result_path, "结果文件")
    if key in known or key in data:
        raise RuntimeError(f"结果文件 {rel} 里重复记录键 {key!r}（同一结果文件的键不可重复）")
    data[key] = normalize(value, mask_paths)
    write_json_atomic(result_path, data, sort_keys=False)
    known.add(key)

    # 伴随诊断文件 <S>.detail.json（一个子阶段一份）：键是 <结果文件名>/<键>，
    # 只用于失败报告，不比对、不重录。诊断信息先做与值相同的规范化，无法
    # 序列化时退回 repr——诊断信息不能成为用例失败的原因。
    detail_path = os.path.join(sub_dir, stem + ".detail.json")
    details = _read_json_strict(detail_path, "诊断文件")
    try:
        detail_value = normalize(detail, mask_paths)
    except TypeError:
        detail_value = repr(detail)
    details[f"{rel}/{key}"] = {"at": _call_site(), "detail": detail_value}
    write_json_atomic(detail_path, details, sort_keys=False)


def validate_relative_path(rel, what="结果文件"):
    """校验一个产物文件名：非空字符串、非绝对路径、纯文件名（不含路径分隔符）。"""
    if not isinstance(rel, str) or not rel:
        raise ValueError(f"{what}必须是非空相对路径，收到 {rel!r}")
    if os.path.isabs(rel):
        raise ValueError(f"{what}必须是相对路径，收到绝对路径 {rel!r}")
    if "/" in rel or "\\" in rel:
        raise ValueError(
            f"{what}只能是纯文件名，不允许路径分隔符（测例产物命名约定），收到 {rel!r}")
    return rel


def _call_site():
    """调用点「脚本名:行号」，用于差异报告定位。跳过本模块自身的帧。"""
    frame = inspect.currentframe()
    try:
        caller = frame.f_back
        this_file = os.path.basename(__file__)
        while caller is not None and os.path.basename(caller.f_code.co_filename) == this_file:
            caller = caller.f_back
        if caller is None:
            return "<unknown>"
        return f"{os.path.basename(caller.f_code.co_filename)}:{caller.f_lineno}"
    finally:
        del frame  # 打破 ReferenceError 提示所警告的引用环


def _read_json_or(path, fallback):
    if not os.path.isfile(path):
        return fallback
    try:
        with open(path, encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return fallback


def _read_json_strict(path, what):
    """读调用方自己的产物文件；文件已存在但不是合法 JSON 时抛 RuntimeError。

    静默当成空文件覆盖会让已记录的键无声丢失，之后盲目重录会把残缺的结果
    固化成基准。文件不存在返回空字典（首写场景）。
    """
    if not os.path.isfile(path):
        return {}
    try:
        with open(path, encoding="utf-8") as f:
            return json.load(f)
    except ValueError as exc:
        raise RuntimeError(
            f"{what}已存在但不是合法 JSON，路径：{path}（{exc}）——"
            "请清理该文件后重跑，不要盲目重录") from exc


def write_json_atomic(path, obj, sort_keys):
    """JSON 落盘：ensure_ascii=False、缩进 2、末尾换行；先写临时文件再 os.replace。"""
    text = json.dumps(obj, ensure_ascii=False, indent=2, sort_keys=sort_keys) + "\n"
    tmp_path = path + ".tmp"
    with open(tmp_path, "w", encoding="utf-8") as f:
        f.write(text)
    os.replace(tmp_path, path)


# ---------------------------------------------------------------------------
# 比对核心：结构化差异节点 → 差异文案
# ---------------------------------------------------------------------------

def _join_path(path, key):
    return f"{path}.{key}" if path else key


def _diff_object_nodes(actual, expected, path, nested):
    """dict 键值比对：返回 (差异节点, 差异所属顶层键) 列表。

    差异按结果文件里的键顺序列出（记录顺序），第一处差异即因果上最早的偏差；
    基准独有的缺键差异补在末尾。顶层键供诊断条目（<S>.detail.json 按
    <结果文件名>/<键> 登记）直接定位，不从差异路径反推。
    """
    out = []
    for key in actual:
        child = _join_path(path, key)
        if key not in expected:
            out.append(({"kind": "extra", "path": child,
                         "expected": None, "actual": actual[key]}, key))
        else:
            for node in _diff_nodes(actual[key], expected[key], child, is_nested=nested):
                out.append((node, key))
    for key in expected:
        if key not in actual:
            out.append(({"kind": "missing", "path": _join_path(path, key),
                         "expected": expected[key], "actual": None}, key))
    return out


def _diff_nodes(actual, expected, path, is_nested=False):
    """递归比较，返回结构化差异节点列表。输入必须是 JSON 值（规范化的值落盘后的形态）。

    is_nested=True 表示该值处于嵌套位置（字典/列表内部）：此时布尔 false 是
    合法的数据，按值比对；只有顶层条目的布尔 false 对 true 才记「判定不成立」。
    """
    if type(actual) is not type(expected):
        return [{"kind": "type", "path": path, "expected": expected, "actual": actual}]
    if isinstance(actual, dict):
        # 顶层对象（path 为空且不在嵌套位置）的键值仍是顶层条目；更深的都是嵌套。
        nested = is_nested or path != ""
        return [node for node, _key in
                _diff_object_nodes(actual, expected, path, nested)]
    if isinstance(actual, list):
        if len(actual) != len(expected):
            return [{"kind": "length", "path": path,
                     "expected": len(expected), "actual": len(actual)}]
        diffs = []
        for i, (a, e) in enumerate(zip(actual, expected)):
            diffs.extend(_diff_nodes(a, e, f"{path}[{i}]", is_nested=True))
        return diffs
    if (isinstance(actual, float) and isinstance(expected, float)
            and math.isnan(actual) and math.isnan(expected)):
        return []
    if actual != expected:
        if expected is True and actual is False and not is_nested:
            # 判定型条目：基准恒为 true，结果为 false 说明判定不成立。
            return [{"kind": "verdict", "path": path,
                     "expected": expected, "actual": actual}]
        return [{"kind": "value", "path": path,
                 "expected": expected, "actual": actual}]
    return []


def _short(value):
    text = json.dumps(value, ensure_ascii=False)
    if len(text) > _MAX_VALUE_TEXT:
        text = text[:_MAX_VALUE_TEXT] + "……"
    return text


def _format_diff_line(node, entry=None):
    """把一个差异节点格式化成报告行；entry 是该顶层键的诊断条目（可为 None）。"""
    kind = node["kind"]
    path = node["path"]
    at = entry.get("at") if entry else None
    detail = entry.get("detail") if entry else None
    if kind == "value":
        line = (f"值不一致  {path}   期望 {_short(node['expected'])}"
                f"   实际 {_short(node['actual'])}")
        if at:
            line += f"   ({at})"
        return line
    if kind == "verdict":
        line = f"判定不成立  {path}"
        if detail is not None:
            line += f"   detail: {_short(detail)}"
        if at:
            line += f"   ({at})"
        return line
    if kind == "type":
        return (f"类型不一致  {path}   期望 {type(node['expected']).__name__}"
                f"   实际 {type(node['actual']).__name__}")
    if kind == "missing":
        return f"缺键  {path}"
    if kind == "extra":
        return f"多键  {path}"
    if kind == "length":
        return f"长度不一致  {path}   期望 {node['expected']}   实际 {node['actual']}"
    return f"{kind}  {path}"


def diff_value(actual, expected, path=""):
    """比较一对规范化后的值，返回差异文案列表（自定义比较方式可复用）。

    参数顺序是（实际值, 期望值, 键路径）。空列表表示通过。没有诊断信息，
    差异行不带调用点与 detail。
    """
    return [_format_diff_line(node) for node in _diff_nodes(actual, expected, path)]


def find_top_level_false(value):
    """结果/基准对象顶层的布尔条目里为 false 的键（判定型条目的期望恒为 true）。

    嵌套在字典、列表里的布尔值是数据：按类型加值比对，false 是合法的期望值，
    不在检查范围。
    """
    if not isinstance(value, dict):
        return []
    return [key for key, sub in value.items() if sub is False]


# ---------------------------------------------------------------------------
# 预设比较方式
# ---------------------------------------------------------------------------

class KvPreset:
    """JSON 对象键值比对（缺省预设）。基准与结果同形（就是结果 JSON 对象本身），可重录。"""

    name = "kv"
    manual_by_default = False

    def validate_options(self, options):
        return options

    def check(self, result_path, gold, options, detail_map=None):
        if not isinstance(gold, dict):
            return ["基准格式不是 JSON 对象"]
        false_keys = find_top_level_false(gold)
        if false_keys:
            # 顶层的布尔条目是判定型，期望恒为 true；false 说明基准本身坏了。
            # 嵌套的布尔值是数据，false 是合法期望值，不在检查范围。
            return [f"基准里顶层的布尔值只允许 true  {k}" for k in false_keys]
        result = _read_json_or(result_path, _NOT_A_DICT)
        if not isinstance(result, dict):
            return ["文件格式不是 JSON 对象"]
        # 诊断条目（<S>.detail.json）按 <结果文件名>/<顶层键> 登记，直接按
        # 差异所属的顶层键查（键含点也能正确带出 at/detail）。
        detail_prefix = os.path.basename(result_path) + "/"
        lines = []
        for node, key in _diff_object_nodes(result, gold, "", nested=False):
            entry = detail_map.get(detail_prefix + key) if detail_map else None
            lines.append(_format_diff_line(node, entry))
        return lines

    def make_gold(self, result_path, options):
        return _read_json_or(result_path, None)

    def stats(self, result_path, gold, options):
        if not isinstance(gold, dict):
            return None
        result = _read_json_or(result_path, _NOT_A_DICT)
        if not isinstance(result, dict):
            return None
        return f"基准 {len(gold)} 个键，结果 {len(result)} 个键"


class ContainsPreset:
    """文本包含检查。基准是 {"must": [...], "must_not": [...]}，手工维护，不可重录。"""

    name = "contains"
    manual_by_default = True

    def validate_options(self, options):
        return options

    def check(self, result_path, gold, options, detail_map=None):
        if not isinstance(gold, dict):
            return ["基准格式不是 JSON 对象"]
        try:
            with open(result_path, encoding="utf-8", errors="replace") as f:
                text = f.read()
        except OSError as exc:
            return [f"结果文件读取失败  {exc}"]
        diffs = []
        for fragment in gold.get("must", []):
            if fragment not in text:
                diffs.append(f"缺少片段  {fragment}")
        for fragment in gold.get("must_not", []):
            if fragment in text:
                diffs.append(f"出现禁止片段  {fragment}")
        return diffs

    def make_gold(self, result_path, options):
        # 手工维护：make_gold 返回 None 表示不可重录。
        return None

    def stats(self, result_path, gold, options):
        return None


_NOT_A_DICT = object()

PRESETS = {"kv": KvPreset(), "contains": ContainsPreset()}

# check_list 项允许的键（任务书 5.4）。
_ITEM_KEYS = ("file", "method", "compare", "gold", "manual")


def validate_check_list(check_list, script_stem):
    """校验并规范化整个检查清单：逐项 validate_check_item，再做清单级校验。

    清单级规则：同一清单里结果文件重复抛 ValueError（重复项无法定位是哪个
    基准条目）。返回规范化后的选项字典列表。
    """
    validated = [validate_check_item(item, script_stem) for item in (check_list or [])]
    rels = [opts["file"] for opts in validated]
    duplicates = sorted({rel for rel in rels if rels.count(rel) > 1})
    if duplicates:
        raise ValueError(f"check_list 里同一结果文件重复：{duplicates}")
    return validated


def validate_check_item(item, script_stem):
    """校验并规范化一个检查清单项，返回统一的选项字典。

    字符串是结果文件的相对路径（用缺省预设 kv）；字典的键见 _ITEM_KEYS。
    按测例产物命名约定，完整文件名以 ``<S>.`` 开头；只写类型后缀（如
    ``result.json``、``messages.txt``）时自动补上 ``<S>.`` 前缀。产品生成的
    文件不能直接写进清单——它展开后会指向 ``<S>.`` 开头的摘录文件，子阶段
    脚本没有产出该文件时报告「结果文件缺失」。

    非法写法一律抛 ValueError（调度函数在启动子进程之前调用本函数）。
    """
    if isinstance(item, str):
        item = {"file": item}
    if not isinstance(item, dict):
        raise ValueError(f"check_list 项必须是字符串或字典，收到 {item!r}")
    unknown = sorted(set(item) - set(_ITEM_KEYS))
    if unknown:
        raise ValueError(
            f"check_list 项含未知键 {unknown}（允许的键：{list(_ITEM_KEYS)}）：{item!r}")
    rel = validate_relative_path(item.get("file"), what="check_list 项的 file")
    prefix = script_stem + "."
    if not rel.startswith(prefix):
        rel = prefix + rel
        expanded = True
    else:
        expanded = False
    # 提前按命名约定推导基准文件名：结果文件缺扩展名在这里就抛 ValueError
    # （启动子进程之前），而不是等到比对/重录阶段。
    method = item.get("method")
    compare = item.get("compare")
    if method is not None and compare is not None:
        raise ValueError(f"check_list 项 method 与 compare 互斥：{item!r}")
    if compare is not None and not callable(compare):
        raise ValueError(f"check_list 项 compare 必须是可调用对象：{item!r}")
    if method is None:
        method = "kv"
    preset = PRESETS.get(method)
    if preset is None:
        raise ValueError(
            f"check_list 项的预设比较方式不存在：{method!r}（可用：{sorted(PRESETS)}）")
    manual = item.get("manual")
    if manual is None:
        manual = preset.manual_by_default
    if not isinstance(manual, bool):
        raise ValueError(f"check_list 项 manual 必须是布尔值：{item!r}")
    gold = item.get("gold")
    if gold is not None:
        validate_relative_path(gold, what="check_list 项的 gold")
        if not gold.startswith(prefix) or not gold.endswith(".gold"):
            raise ValueError(
                f"独立基准文件名必须以 {prefix!r} 开头、以 '.gold' 结尾"
                f"（测例产物命名约定，放在 gold/data/ 下），收到 {gold!r}")
    gold_file_name(script_stem, rel)  # 缺扩展名在此提前报 ValueError
    return {"file": rel, "method": method, "compare": compare,
            "gold": gold, "manual": manual, "expanded": expanded}


# ---------------------------------------------------------------------------
# 基准文件（gold）：一个结果文件一个基准文件，命名按测例产物约定
# ---------------------------------------------------------------------------

def gold_file_name(script_stem, result_rel):
    """结果文件对应的基准文件名（测例产物命名约定，基准文件名固定以 ``.gold`` 结尾）。

    ``<S>.result.json``（缺省结果文件）→ ``<S>.gold``；
    ``<S>.<类型>.<扩展名>`` → ``<S>.<类型>.gold``。
    取 ``S.`` 之后、扩展名之前的部分作为主题加 ``.gold``；缺省类型 ``result``
    省略不写。基准内容仍是 JSON（kv / contains 预设），只是文件名不带 ``.json``。
    """
    prefix = script_stem + "."
    if not result_rel.startswith(prefix):
        raise ValueError(f"结果文件必须以 {prefix!r} 开头（测例产物命名约定）：{result_rel!r}")
    tail = result_rel[len(prefix):]
    topic, _, _ext = tail.rpartition(".")
    if not topic:
        raise ValueError(f"结果文件缺少扩展名，无法确定基准文件主题：{result_rel!r}")
    if topic == "result":
        return f"{script_stem}.gold"
    return f"{script_stem}.{topic}.gold"


def gold_path_for_result(gold_dir, script_stem, result_rel):
    """结果文件对应的基准文件绝对路径（qa/<编排脚本所在目录>/gold/ 下）。"""
    return os.path.join(gold_dir, gold_file_name(script_stem, result_rel))


def load_gold_file(path):
    """读基准文件；不存在或损坏返回 None。"""
    return _read_json_or(path, None)


def save_gold_file(path, content):
    """写基准文件：键排序（与结果文件的记录顺序不同）、缩进 2、末尾换行、原子替换。"""
    os.makedirs(os.path.dirname(path), exist_ok=True)
    write_json_atomic(path, content, sort_keys=True)


# ---------------------------------------------------------------------------
# 检查清单执行（正常运行，子阶段结束后由调度函数调用）
# ---------------------------------------------------------------------------

def check_result_files(check_items, sub_log_dir, gold_dir, script_stem, regold_hint):
    """按检查清单逐项比对结果文件与基准，返回（报告行列表, 不一致项数）。

    基准按测例产物命名约定存放在 gold_dir：一个结果文件一个基准文件（缺省
    ``<S>.gold``，其他类型 ``<S>.<类型>.gold``），kv 预设的基准内容就是与结果
    同形的 JSON 对象本身。报告行是带缩进的多行文本，空列表表示全部通过。
    终端展示每个结果文件最多 _MAX_SHOWN_DIFFS 处差异。
    """
    lines = []
    bad_items = 0
    # 诊断文件 <S>.detail.json 一个子阶段一份，键是 <结果文件名>/<键>。
    detail_map = _read_json_or(
        os.path.join(sub_log_dir, script_stem + ".detail.json"), {})
    for opts in check_items:
        rel = opts["file"]
        result_path = os.path.join(sub_log_dir, rel)
        # 报告行的比较方式标签：预设显示预设名；自定义比较显示 custom（method
        # 在自定义项上恒为缺省 kv，直接显示会误导）。
        label = "custom" if opts["compare"] is not None else opts["method"]
        if not os.path.isfile(result_path):
            if opts.get("expanded"):
                lines.append(
                    f"  {rel}  [{label}]  结果文件缺失"
                    f"（已按命名约定展开为 {rel}；产品生成的文件要先摘录成"
                    f" {script_stem}.<类型> 文件再检查）")
            else:
                lines.append(f"  {rel}  [{label}]  结果文件缺失")
            bad_items += 1
            continue
        if opts["compare"] is not None:
            if opts["gold"]:
                # 独立基准文件（相对 gold/data/），传绝对路径。
                gold_arg = os.path.join(gold_dir, "data", opts["gold"])
            else:
                # 缺省用本结果文件对应的基准 <S>.<类型>.gold 的 JSON 值，
                # 不存在则传 None。
                gold_arg = load_gold_file(gold_path_for_result(gold_dir, script_stem, rel))
            try:
                diffs = list(opts["compare"](result_path, gold_arg))
            except Exception as exc:  # 自定义比较的异常按失败处理，不中断报告
                diffs = [f"自定义比较方式执行出错  {type(exc).__name__}: {exc}"]
            stats_text = None
        else:
            gold = load_gold_file(gold_path_for_result(gold_dir, script_stem, rel))
            if gold is None:
                lines.append(
                    f"  {rel}  [{opts['method']}]  基准缺失（重录：{regold_hint}）")
                bad_items += 1
                continue
            preset = PRESETS[opts["method"]]
            diffs = preset.check(result_path, gold, opts, detail_map)
            stats_text = preset.stats(result_path, gold, opts)
        if diffs:
            bad_items += 1
            head = f"  {rel}  [{label}]"
            if stats_text:
                head += f"  {stats_text}"
            head += f"，不一致 {len(diffs)} 处"
            lines.append(head)
            for line in diffs[:_MAX_SHOWN_DIFFS]:
                lines.append(f"    {line}")
            if len(diffs) > _MAX_SHOWN_DIFFS:
                lines.append(
                    f"    ……其余 {len(diffs) - _MAX_SHOWN_DIFFS} 处省略"
                    "（完整列表见 check 报告文件）")
    return lines, bad_items


# ---------------------------------------------------------------------------
# 重录（--regold，子阶段处理后由调度函数/编排脚本收尾落盘）
# ---------------------------------------------------------------------------

def regold_result_files(check_items, sub_log_dir, fly_bin_mtime, script_stem):
    """按检查清单逐项生成基准值，返回（重录条目, 提示行列表）。

    重录条目是 {结果文件相对路径: {"gold_rel": 基准文件相对 gold 目录的路径,
    "value": 基准值}}：声明了 gold= 的自定义项落到 gold/data/<gold>，其余按
    命名约定落到 <S>.gold / <S>.<类型>.gold。

    - 结果文件不存在：跳过并提示（旧基准不动）。
    - 结果文件比 fly 二进制旧：非阻断提示。
    - 结果顶层布尔为 false：抛 RegoldRejected——顶层布尔是判定型条目，重录会
      把 false 写成期望值，该检查从此永久失效；嵌套布尔是数据，不拒绝。
    - 手工维护项（contains、没有 make_gold 的自定义比较）：跳过并提示。
    """
    pending = {}
    notes = []
    for opts in check_items:
        rel = opts["file"]
        if opts.get("manual"):
            # manual=True 声明基准手工维护：最先判断，任何情况下都不重录。
            notes.append(f"  [跳过] {rel}: 基准手工维护（manual=True），不重录")
            continue
        result_path = os.path.join(sub_log_dir, rel)
        if not os.path.isfile(result_path):
            notes.append(f"  [未重录] {rel}: 上次运行没有该子阶段的结果文件，旧基准不动")
            continue
        try:
            if os.path.getmtime(result_path) < fly_bin_mtime:
                notes.append(f"  [提示] {rel}: 结果文件比 build/bin/fly 旧，"
                             "结果可能来自旧版本的二进制")
        except OSError:
            pass
        if opts["gold"]:
            gold_rel = os.path.join("data", opts["gold"])
        else:
            gold_rel = gold_file_name(script_stem, rel)
        if opts["compare"] is not None:
            make_gold = getattr(opts["compare"], "make_gold", None)
            if make_gold is None:
                notes.append(
                    f"  [跳过] {rel}: 自定义比较方式未提供 make_gold，视为手工维护")
                continue
            pending[rel] = {"gold_rel": gold_rel, "value": make_gold(result_path)}
            continue
        preset = PRESETS[opts["method"]]
        value = preset.make_gold(result_path, opts)
        if value is None:
            notes.append(f"  [跳过] {rel}  [{opts['method']}]: 手工维护项，不重录")
            continue
        false_keys = find_top_level_false(value)
        if false_keys:
            raise RegoldRejected(
                f"{rel}: 判定型条目为假，不能重录（{', '.join(false_keys)}）——"
                "顶层的 false 重录后会写成期望值，这个检查从此永久失效；"
                "先修复用例使其记录 true")
        pending[rel] = {"gold_rel": gold_rel, "value": value}
    return pending, notes


def _flatten_leaves(value, prefix=""):
    """把基准值展平为 {键路径: 叶子值}，用于重录统计。字典递归；其余值整体算叶子。"""
    leaves = {}
    if isinstance(value, dict):
        for key, sub in value.items():
            leaves.update(_flatten_leaves(sub, _join_path(prefix, key)))
    else:
        leaves[prefix] = value
    return leaves


def _json_equal(a, b):
    if isinstance(a, float) and isinstance(b, float) and math.isnan(a) and math.isnan(b):
        return True
    return a == b


def gold_change_stats(old, new):
    """新旧基准的差异统计：新增/删除/改值的叶子键数（重录结束打印用）。"""
    old_leaves = _flatten_leaves(old) if old is not None else {}
    new_leaves = _flatten_leaves(new)
    added = sum(1 for key in new_leaves if key not in old_leaves)
    removed = sum(1 for key in old_leaves if key not in new_leaves)
    changed = sum(1 for key in new_leaves
                  if key in old_leaves and not _json_equal(old_leaves[key], new_leaves[key]))
    return {"added": added, "removed": removed, "changed": changed}
