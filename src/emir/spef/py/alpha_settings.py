"""SPAlphaSettings — spef db alpha 设置的声明式定义 + alpha 键级
validator（plan §4）。

命名 validator 统一落点（dev-rules §3 值域单一来源）：header schema
（build_spef_db 的 spef_files/alpha 参数）与本文件的 AlphaSetting
validator 引用同一组 is_* 函数，改值域只动一处。

建库入口接线（同 timing/waveform 先例 2026-09-17 裁定）：header 的
Schema.dict 引用本模块 validator 拦截未知键/非法值（直接 raise）→
默认实例 apply(alpha) 防御性覆盖 → alpha 对象以固定对象名
"alpha_settings" 写入 db → 消费点 read_object 读回后 normalize() 兜底。

两键（plan §4）：chunk_size_mb——单文件字节切块大小；value_index——
多值形态取值下标，缺省 None 表示「未给」（消费侧按缺省取值规则取：
三个值取第 1 个〔典型值〕、其它个数取第 0 个——裁定 6）。
"""

from emir.common import AlphaSetting, AlphaSettings


# ── is_*：header schema / AlphaSetting validator 共用（值域单一来源）─────


def is_chunk_size_mb(value):
    """int 且非 bool 且 >= 16（plan §4 约束：单文件字节区间切块大小的
    下限护栏——过小的块使任务调度开销反超 I/O 收益）。

    header schema（build_spef_db 的 alpha.chunk_size_mb）引用同一函数
    ——值域单一来源。"""
    return (isinstance(value, int) and not isinstance(value, bool)
            and value >= 16)


def is_value_index(value):
    """int 且非 bool 且 >= 0（多值形态取值下标；None = 未给的缺省形态
    ——缺省值合法性由缺省规则兜底，此处只校验显式给的值）。

    header schema（build_spef_db 的 alpha.value_index）引用同一函数
    ——值域单一来源。"""
    return isinstance(value, int) and not isinstance(value, bool) \
        and value >= 0


# ── 声明式 alpha ─────────────────────────────────────────────────────


class SPAlphaSettings(AlphaSettings):
    """spef db 建库 alpha 设置（两键声明式，plan §4）。"""

    chunk_size_mb = AlphaSetting(
        default=256, value_type="int", constraint=">= 16",
        validator=is_chunk_size_mb,
        description="单文件字节区间切块大小（MB；块界对齐网段边界，单个"
                    "网段超过切块阈值不拆分）——缺省 256")

    value_index = AlphaSetting(
        default=None, value_type="int", constraint=">= 0",
        validator=is_value_index,
        description="多值形态取第几个值（None = 未给，按缺省规则：三个"
                    "值取第 1 个典型值、其它个数取第 0 个）——缺省 None")


def get_default_alpha_settings():
    """返回全新 SPAlphaSettings（deepcopy 语义：多次创建互不污染默认值）。"""
    return SPAlphaSettings()
