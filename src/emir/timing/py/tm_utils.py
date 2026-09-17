"""timing 模块内部工具函数（worker 任务函数 + 入口校验辅助）。

不对外导出（dev-rules §2 六文件规范）。
"""

# 头嗅探关键字（plan §3.1：误传文件秒级 ValueError 拦截）
_SNIFF_KEYWORD = "TIMING_WINDOWS"
_SNIFF_BYTES = 4096


def sniff_twf_header(path: str) -> None:
    """TWF 头嗅探：文件头部含 TIMING_WINDOWS 关键字，否则 ValueError。

    入口同步拦截（dev-rules §7 可 raise 两类之一：输入语法/格式错误），
    不建库、不起任务。
    """
    with open(path, "rb") as f:
        head = f.read(_SNIFF_BYTES)
    if _SNIFF_KEYWORD.encode() not in head:
        raise ValueError(
            f"not a timing windows file (no '{_SNIFF_KEYWORD}' in file "
            f"header): {path}")


def normalize_timing_files(timing_files):
    """timing_files 列表 → 绑定描述归一化（plan §2 名字空间三形态）。

    结构合法性（list 类型/非空、元素形态、描述符键白名单、互斥）已由
    build_timing_db 的 header schema 拦截（2026-09-17 裁定：重复校验
    分支删除，本函数只保留规范化职责）；输入均按已通过 schema 的形态
    处理。返回 dict 列表：
    ``{"file_name", "kind", "block_inst", "block_cell", "strip_prefix"}``
    （kind：0 全路径 / 1 block_inst / 2 block_cell，与 C++ 侧对齐）。
    """
    normalized = []
    for item in timing_files:
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
