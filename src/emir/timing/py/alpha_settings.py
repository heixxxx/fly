"""TMAlphaSettings — timing db alpha 设置的声明式定义（plan §4，两键）。

建库入口接线与 design 同构：默认实例 apply(alpha) 逐键校验覆盖 → 问题
一次汇总 TIMG::0005（user warn，不 raise）→ settings 对象以固定对象名
"alpha_settings" 写入 db → 消费点 read_object 读回后 normalize() 兜底。
"""

from emir.common import AlphaSetting, AlphaSettings

# format 键的合法方言集（plan §4：首版仅 innovus 一种，键为后续方言
# 扩展预留——RedHawk sta.timing / 新思 twf 列演进项）
_TWF_FORMATS = ("auto", "innovus")


def _is_chunk_size_mb(value):
    """int 且非 bool 且 >= 16（plan §4 约束：单文件字节区间切块大小的
    下限护栏——过小的块使任务调度开销反超 I/O 收益）。"""
    return (isinstance(value, int) and not isinstance(value, bool)
            and value >= 16)


def _is_format(value):
    return isinstance(value, str) and value in _TWF_FORMATS


class TMAlphaSettings(AlphaSettings):
    """timing db 建库 alpha 设置（两键声明式，plan §4）。"""

    chunk_size_mb = AlphaSetting(
        default=256, value_type="int", constraint=">= 16",
        validator=_is_chunk_size_mb,
        description="单文件字节区间切块大小（MB；逐块解析任务粒度，顶层"
                    "构造边界对齐切分）——后续单文件流式分布式增强的调节"
                    "钮，缺省 256")
    format = AlphaSetting(
        default="auto", value_type="str", constraint="auto/innovus",
        validator=_is_format,
        description="TWF 方言覆盖；auto = 头嗅探（首版仅 innovus 一种，键"
                    "为后续方言扩展预留）")


def get_default_alpha_settings():
    """返回全新 TMAlphaSettings（deepcopy 语义：多次创建互不污染默认值）。"""
    return TMAlphaSettings()
