"""timing 模块内部工具函数（worker 任务函数 + 入口校验辅助）。

不对外导出（dev-rules §2 六文件规范）。
"""

# timing_files 列表元素的绑定描述符键（plan §2，2026-09-16 裁定键名）
_BINDING_KEYS = ("file_name", "block_inst", "block_cell", "strip_prefix")

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

    元素形态：
      纯路径字符串                     → 全层级路径名（顶层视图/全芯片）
      {"file_name", "block_inst"}      → 块实例绑定的局部名
      {"file_name", "block_cell"}      → 块定义绑定（定义级全实例复制）
      以上两 dict 形态可附加 "strip_prefix"（段级前缀剥离，§7.4）

    结构非法 → ValueError（输入语义错误，入口同步拦截）。返回 dict 列表：
    {"file_name", "kind", "block_inst", "block_cell", "strip_prefix"}
    （kind：0 全路径 / 1 block_inst / 2 block_cell，与 C++ 侧对齐）。
    """
    if not isinstance(timing_files, list) or not timing_files:
        raise ValueError(
            "timing_files must be a non-empty list of path strings or "
            "binding descriptors")
    normalized = []
    for item in timing_files:
        if isinstance(item, str):
            if not item:
                raise ValueError("timing_files: empty path string")
            normalized.append({"file_name": item, "kind": 0,
                               "block_inst": "", "block_cell": "",
                               "strip_prefix": ""})
            continue
        if not isinstance(item, dict):
            raise ValueError(
                f"timing_files: element must be a path string or dict, "
                f"got {type(item).__name__}")
        unknown = set(item) - set(_BINDING_KEYS)
        if unknown:
            raise ValueError(
                f"timing_files: unknown binding keys {sorted(unknown)} "
                f"(allowed: {list(_BINDING_KEYS)})")
        file_name = item.get("file_name")
        if not isinstance(file_name, str) or not file_name:
            raise ValueError("timing_files: 'file_name' must be a non-empty "
                             "string")
        has_inst = "block_inst" in item
        has_cell = "block_cell" in item
        if has_inst and has_cell:
            raise ValueError(
                f"timing_files: '{file_name}' cannot bind both block_inst "
                f"and block_cell")
        if has_inst:
            block = item["block_inst"]
            if not isinstance(block, str) or not block:
                raise ValueError(
                    f"timing_files: '{file_name}' block_inst must be a "
                    f"non-empty hierarchy path")
            kind = 1
        elif has_cell:
            block = item["block_cell"]
            if not isinstance(block, str) or not block:
                raise ValueError(
                    f"timing_files: '{file_name}' block_cell must be a "
                    f"non-empty cell name")
            kind = 2
        else:
            raise ValueError(
                f"timing_files: '{file_name}' binding dict requires "
                f"'block_inst' or 'block_cell'")
        # strip_prefix 仅块绑定形态可附加（plan §2 形态 4——本函数的 dict
        # 形态恒为绑定形态，纯路径形态为字符串元素无从附加）
        strip_prefix = item.get("strip_prefix", "")
        if not isinstance(strip_prefix, str):
            raise ValueError(
                f"timing_files: '{file_name}' strip_prefix must be a string")
        normalized.append({
            "file_name": file_name, "kind": kind,
            "block_inst": item.get("block_inst", ""),
            "block_cell": item.get("block_cell", ""),
            "strip_prefix": strip_prefix,
        })
    return normalized
