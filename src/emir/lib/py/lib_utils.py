"""lib 模块内部工具——worker 侧解析任务。

供 lib_flow 的 MapReduce processor 引用；每 .lib 文件一个独立任务
（天然分布式点）。
"""

import os

from .lib_export import EXLIBLibrary, lib_parse_lib_file


def lib_parse_one(part_path: str) -> EXLIBLibrary:
    """解析单个 .lib 文件（C++ 解析器）。

    文件缺失抛 FileNotFoundError——文件不可读属可 raise 场景（用户裁定：
    仅文件不可读/语法错误可 raise，其余解析场景兜底不抛）。
    """
    if not os.path.isfile(part_path):
        raise FileNotFoundError(f"lib_parse_one: lib file missing: {part_path}")
    return lib_parse_lib_file(part_path)
