"""LIBAlphaSettings — lib db alpha 设置的声明式定义（2026-09-13 裁定）。

lib 建库首版无激活 alpha 键：空字段起步，体系就位（对象名 "alpha_settings"
随建库写入 db；后续新增键在此以 AlphaSetting 声明五要素即可——apply/
normalize/读回兜底与 message 透出（LIBR::0005）链路已由基座与 build 接
线就位，键级规则零新增接线）。
"""

from emir.common import AlphaSettings


class LIBAlphaSettings(AlphaSettings):
    """lib db 建库 alpha 设置（首版空字段）。"""

    pass


def get_default_alpha_settings():
    """返回全新 LIBAlphaSettings（deepcopy 语义同基座）。"""
    return LIBAlphaSettings()
