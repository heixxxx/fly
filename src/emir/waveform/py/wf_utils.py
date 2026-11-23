"""waveform 模块内部工具函数（worker 任务函数 + 入口校验辅助）。

不对外导出（dev-rules §2 六文件规范）。
"""

# settings 值域常量单点在 alpha_settings.py（validator 落点同源——值域
# 单一来源纪律），本文件只引用
from .alpha_settings import (DEFAULT_DURATION_SEC,
                             X_Z_HANDLING_DENOMINATOR_INCLUDE)


def normalize_vcd_files(vcd_files):
    """vcd_files 列表 → 绑定描述归一化（plan §2 名字空间三形态，逐条
    复用 timing db §7.1/§7.2/§7.4 设计）。

    结构合法性（list 类型/非空、元素形态、描述符键白名单、互斥）已由
    build_waveform_db 的 header schema 拦截（同 timing 2026-09-17 裁定：
    重复校验分支删除，本函数只保留规范化职责）；输入均按已通过 schema
    的形态处理。返回 dict 列表：
    ``{"file_name", "kind", "block_inst", "block_cell", "strip_prefix"}``
    （kind：0 全路径 / 1 block_inst / 2 block_cell，与 C++ 侧
    WFFileBindingKind 对齐）。
    """
    normalized = []
    for item in vcd_files:
        if isinstance(item, str):
            normalized.append({"file_name": item, "kind": 0,
                               "block_inst": "", "block_cell": "",
                               "strip_prefix": ""})
            continue
        # dict 块绑定描述符：block_inst 与 block_cell 恰其一（schema 互斥
        # check 保证）；strip_prefix 仅块绑定形态可附加（可选，缺省空）
        if "block_inst" in item:
            kind = 1
        else:
            kind = 2
        normalized.append({
            "file_name": item["file_name"], "kind": kind,
            "block_inst": item.get("block_inst", ""),
            "block_cell": item.get("block_cell", ""),
            "strip_prefix": item.get("strip_prefix", ""),
        })
    return normalized


def default_settings_meta_params():
    """settings 缺省元数据（VCD 模式无 settings / settings 缺键时的
    summary 元数据口径——与 C++ 侧 WFSettingsMeta 缺省一致）。"""
    return {"default_toggle_rate": 0.0,
            "default_static_probability": 0.5,
            "duration_sec": DEFAULT_DURATION_SEC,
            "x_z_handling": X_Z_HANDLING_DENOMINATOR_INCLUDE}


def settings_meta_params(settings):
    """用户 settings dict → summary 元数据参数（plan §4：default 不逐
    引脚物化——作为元数据随 summary 落库，消费侧展开）。

    输入已由 header schema 拦截（白名单严格 + 键级值域）；缺键补缺省。
    default 可选（裁定 IV-2 混合模式：overrides-only activity 的 default
    元数据取缺省值——VCD 模式不生效，仅 summary 元数据）。
    返回 dict：default_toggle_rate / default_static_probability /
    duration_sec / x_z_handling。
    """
    meta = default_settings_meta_params()
    if settings is None:
        return meta
    activity = settings.get("activity")
    if activity is not None:
        default = activity.get("default")
        if default is not None:
            meta["default_toggle_rate"] = default["toggle_rate"]
            meta["default_static_probability"] = \
                default["static_probability"]
        meta["duration_sec"] = activity.get("duration",
                                            DEFAULT_DURATION_SEC)
    if "x_z_handling" in settings:
        meta["x_z_handling"] = settings["x_z_handling"]
    return meta


def duration_fs_of(duration_sec: float) -> int:
    """统计窗口时长（秒）→ 飞秒 int64（plan §4 换算口径：非整值四舍
    五入取整——与 C++ 侧 wf_materialize_activity 的 llround 口径一致）。"""
    return int(round(duration_sec * 1e15))
