"""lib 模块内部工具——worker 侧解析任务。

供 lib_flow 的 MapReduce processor 引用；每 .lib 文件一个独立任务
（天然分布式点）。入口嗅探（build_lib_db master 侧调用）也在本文件。
"""

import os
import re

from .lib_export import EXLIBLibrary, lib_parse_lib_file


def sniff_liberty_header(path: str) -> None:
    """入口防呆嗅探（秒级）：读文件前 1KB 剥注释/空白，校验 liberty 形态
    （``library`` 关键字 + ``(``）。

    仅做类型判定、不做完整语法检查（2026-09-13 裁定）——.lef 等非 liberty
    文件误传 build_lib_db 在此秒级拦截 ValueError（含路径与实际读到的头
    部字符，dev-rules §7 第一类变体），不建库、不起解析任务。
    """
    with open(path, "r", errors="replace") as f:
        head = f.read(1024)
    # 剥 liberty 注释：// 到行尾、/* ... */（未闭合块剥到结尾）
    stripped = re.sub(r"//[^\n]*", " ", head)
    stripped = re.sub(r"/\*.*?(\*/|$)", " ", stripped, flags=re.S)
    if not re.match(r"\s*library\s*\(", stripped, re.IGNORECASE):
        raise ValueError(
            f"build_lib_db: '{path}' does not look like a liberty (.lib) "
            f"file — expected header starting with 'library ('; got: "
            f"{stripped.strip()[:80]!r}")


def lib_parse_one(part_path: str):
    """解析单个 .lib 文件（C++ 解析器），返回 (EXLIBLibrary, 失败清单)。

    流程错误处理范式（2026-09-13 裁定，dev-rules §7.2）：单文件语法错误
    → 兜底——返回空 LIBLibrary + 失败清单（文件路径 + 原因），任务不
    FAILED，由 flow 汇总阶段发 LIBR message 并保持依赖链满足（全败由
    汇总 fatal，见 lib_flow._lib_finalize）。

    文件缺失抛 FileNotFoundError——文件不可读属可 raise 场景（用户裁定：
    仅文件不可读/语法错误可 raise，其余解析场景兜底不抛）。
    """
    if not os.path.isfile(part_path):
        raise FileNotFoundError(f"lib_parse_one: lib file missing: {part_path}")
    try:
        return lib_parse_lib_file(part_path), []
    except RuntimeError as e:
        # 「文件不可读」仍属可 raise 场景（dev-rules §7 第一类）：C++ 侧
        # SI2DR_INVALID_NAME（打开失败，worker 执行期权限变更/删除 race
        # 窗口）与语法错误同抛 runtime_error，按消息前缀分流 re-raise
        #（review 2026-09-13：原实现把不可读一并兜底吞掉，违反裁定）
        if str(e).startswith("lib_parse_lib_file: cannot open file"):
            raise
        # 语法/解析错误（si2drReadLibertyFile 错误码）：跳过该文件兜底。
        return EXLIBLibrary(), [f"{part_path}: {e}"]
