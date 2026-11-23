"""WFAlphaSettings — waveform db alpha 设置的声明式定义 + settings/alpha
键级 validator（plan §4）。

命名 validator 统一落点（dev-rules §3 值域单一来源）：header schema
（build_waveform_db 的 vcd_files/settings/alpha 参数）与本文件的
AlphaSetting validator 引用同一组 is_* 函数，改值域只动一处。

建库入口接线（同 timing 先例 2026-09-17 裁定）：header 的 Schema.dict
引用本模块 validator 拦截未知键/非法值（直接 raise）→ 默认实例
apply(alpha) 防御性覆盖 → alpha 对象以固定对象名 "alpha_settings" 写入
db → 消费点 read_object 读回后 normalize() 兜底。

无 format 键——VCD 为 IEEE 1364 单一标准格式，无方言维度（timing 的
TWF 有 auto/innovus 两方言，本库不需要）。gzip 压缩由文件入口任务魔数
嗅探自动识别（裁定 IV-1），无需用户键；其余压缩/扩展格式列演进 §12。
"""

from emir.common import (AlphaSetting, AlphaSettings,
                         is_nonneg_finite_number)

# x/z 消费口径三枚举（plan §4；值序 = 文案呈现序。常量单点定义——
# wf_utils 等消费面引用此处，值域单一来源纪律）
X_Z_HANDLING_DENOMINATOR_INCLUDE = "denominator_include"
X_Z_HANDLING_EXCLUDE = "exclude"
X_Z_HANDLING_HOLD_PREVIOUS = "hold_previous"
X_Z_HANDLING_VALUES = (X_Z_HANDLING_DENOMINATOR_INCLUDE,
                       X_Z_HANDLING_EXCLUDE,
                       X_Z_HANDLING_HOLD_PREVIOUS)

# settings.activity.duration 缺省（秒；plan §4）
DEFAULT_DURATION_SEC = 1.0


# ── is_*：header schema / AlphaSetting validator 共用（值域单一来源）─────


def is_chunk_size_mb(value):
    """int 且非 bool 且 >= 16（plan §4 约束：单文件字节区间切块大小的
    下限护栏——过小的块使任务调度开销反超 I/O 收益）。

    header schema（build_waveform_db 的 alpha.chunk_size_mb）引用同一
    函数——值域单一来源。"""
    return (isinstance(value, int) and not isinstance(value, bool)
            and value >= 16)


def is_x_z_handling(value):
    """x/z 消费口径三枚举（plan §4）。"""
    return isinstance(value, str) and value in X_Z_HANDLING_VALUES


def is_probability(value):
    """逻辑 1 概率 / 上升沿占比（有限数且落在 [0, 1]；bool 伪装拒绝）。"""
    if not is_nonneg_finite_number(value):
        return False
    return value <= 1


def is_duration_sec(value):
    """统计窗口时长（秒；有限数且 > 0——零窗口无统计意义）。"""
    return is_nonneg_finite_number(value) and value > 0


def is_activity_default(value):
    """settings.activity.default 条目：恰两键 toggle_rate/static_
    probability，值域 is_nonneg_finite_number / is_probability。"""
    if not isinstance(value, dict) or set(value) != {"toggle_rate",
                                                     "static_probability"}:
        return False
    return (is_nonneg_finite_number(value["toggle_rate"])
            and is_probability(value["static_probability"]))


_OVERRIDE_VALUE_CHECKS = {
    "toggle_rate": is_nonneg_finite_number,
    "static_probability": is_probability,
    "rise_fraction": is_probability,
}


def is_override_entry(value):
    """settings.activity.overrides 单条目：键白名单（toggle_rate /
    static_probability / rise_fraction，均可选——缺省 rise_fraction=0.5、
    static_probability=0.5 与 C++ 侧 WFActivityOverride 缺省一致）。"""
    if not isinstance(value, dict):
        return False
    for key, item in value.items():
        check = _OVERRIDE_VALUE_CHECKS.get(key)
        if check is None or not check(item):
            return False
    return True


def is_override_table(value):
    """settings.activity.overrides 表（动态键 = 实体名）。"""
    return (isinstance(value, dict)
            and all(is_override_entry(item) for item in value.values()))


def is_activity_struct(value):
    """settings.activity 整体结构：default 与 overrides 至少其一（空壳
    activity 无输入意义——settings-only 主输入或混合模式覆盖二居其一定
    义了本结构的用途）；duration 可选；白名单严格——未知键 False。

    default 可选（裁定 IV-2 混合模式：VCD + overrides 并存时 default
    在 VCD 模式不生效——用户给 overrides 不应被强制携带不生效的
    default）；settings-only 判定不变（vcd_files=None 时 activity 键
    仍必给，由 build_waveform_db 入口拦截）。"""
    if not isinstance(value, dict):
        return False
    if set(value) - {"default", "duration", "overrides"}:
        return False
    if "default" in value and not is_activity_default(value["default"]):
        return False
    if "duration" in value and not is_duration_sec(value["duration"]):
        return False
    if "overrides" in value and not is_override_table(value["overrides"]):
        return False
    # 至少其一：default 存在，或 overrides 非空（空 overrides 与缺省同
    # 义——不构成实质输入）
    return "default" in value or bool(value.get("overrides"))


# ── 声明式 alpha ─────────────────────────────────────────────────────


class WFAlphaSettings(AlphaSettings):
    """waveform db 建库 alpha 设置（单键声明式，plan §4）。"""

    chunk_size_mb = AlphaSetting(
        default=256, value_type="int", constraint=">= 16",
        validator=is_chunk_size_mb,
        description="单文件字节区间切块大小（MB；逐块解析任务粒度，块界"
                    "对齐 #时间戳行边界）——缺省 256")


def get_default_alpha_settings():
    """返回全新 WFAlphaSettings（deepcopy 语义：多次创建互不污染默认值）。"""
    return WFAlphaSettings()
