"""spef 模块内部工具函数（worker 任务函数 + 入口校验辅助）。

不对外导出（dev-rules §2 六文件规范）。
"""


def normalize_spef_files(spef_files):
    """spef_files 列表 → 绑定描述归一化（plan §2 名字空间三形态，逐条
    复用 timing/waveform 设计）。

    结构合法性（list 类型/非空、元素形态、描述符键白名单、互斥、
    strip_prefix 仅块绑定形态）已由 build_spef_db 的 header schema 拦截
    （2026-09-17 裁定：重复校验分支删除，本函数只保留规范化职责）；输
    入均按已通过 schema 的形态处理。返回 dict 列表：
    ``{"file_name", "kind", "block_inst", "block_cell", "strip_prefix"}``
    （kind：0 全路径 / 1 block_inst / 2 block_cell，与 C++ 侧
    SPFileBindingKind 对齐）。
    """
    normalized = []
    for item in spef_files:
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
