"""E2E test: EMIRProject.build_lib_db — 多文件分布式解析 + LIBLibrary 整合。

验证 docs/emir-data-flow.md §5 的第一个 flow（MapReduce 裁定 14）：
  - 两份 .lib 文件（同库异 cell）分布式解析（每文件一独立任务）；
  - 全量合并为单一 LIBLibrary 容器写入 lib db（role=lib）；
  - freeze task 依赖 LIBLibrary 写完，frozen 即流程完成；
  - 容器内容可读（cell 全集 / 模板 / 引脚电容 / 功耗表）。
"""
import os
import shutil

from log import INFO

from fly import get_config, launch_workers
from fly.runtime import get_agent
from emir import EMIRProject

from test import record

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
LIB_A = os.path.join(SCRIPT_DIR, "data", "cells_a.lib")
LIB_B = os.path.join(SCRIPT_DIR, "data", "cells_b.lib")

LOG_DIR = get_config().get_str("log_dir")
PROJ_PATH = os.path.join(LOG_DIR, "emir_lib_flow")


def cleanup():
    if os.path.isdir(PROJ_PATH):
        shutil.rmtree(PROJ_PATH, ignore_errors=True)


cleanup()

# 用户预先唤起 worker（MapReduce 的分区/合并任务需要执行者）
launch_workers([{}, {}])
assert get_agent().wait_workers_registered(timeout=60), "workers should connect"
INFO("  2 workers connected (user-managed)")

proj = EMIRProject(PROJ_PATH)
# 原断言：assert "build_lib_db" in proj.list_flows(), f"flows={proj.list_flows()}"
record("flows/build_lib_db_registered", "build_lib_db" in proj.list_flows(),
       detail=sorted(proj.list_flows()))

# ── build_lib_db：两文件分布式解析（异步返回）──
lib_db = proj.build_lib_db(name="lib", lib_paths=[LIB_A, LIB_B])
INFO(f"build_lib_db returned db (async): {lib_db}")

assert proj.wait_frozen("lib", timeout=120), "lib db should freeze after merge"
INFO("[WAIT] lib db frozen — full pipeline done")

# ── 校验整合容器 ──
library = lib_db.load_library()
cell_names = sorted(c.name for c in library.cells)
# 原断言：assert cell_names == ["DFF_X1", "INV_X1"], f"cells={cell_names}"
record("library/cell_names", cell_names)
# 保留：守卫（后面 next(...) 按名字找 INV_X1、DFF_X1，找不到会以 StopIteration 崩溃；只保证这两个 cell 存在，全集与顺序由基准比对把关）
assert "INV_X1" in cell_names and "DFF_X1" in cell_names
# 原断言：assert library.template_names == ["power_2d"], \
record("library/template_names", library.template_names)

inv = next(c for c in library.cells if c.name == "INV_X1")
pin_a = next(p for p in inv.pins if p.name == "A")
# 原断言：assert abs(pin_a.capacitance - 1.5) < 1e-12, f"cap={pin_a.capacitance}"
record("pins/inv_a_cap_1p5ff", abs(pin_a.capacitance - 1.5) < 1e-12,
       detail=f"{pin_a.capacitance!r}")

dff = next(c for c in library.cells if c.name == "DFF_X1")
# 原断言：assert dff.is_sequence_cell, "DFF_X1 should be marked sequential (ff group)"
record("cells/dff_is_sequence_cell", dff.is_sequence_cell)

# 来源可追溯（裁定 12/13）：cell 记录所属库组名与解析来源文件完整路径
# 原断言：assert inv.library_name == "minitest_typ", f"library_name={inv.library_name}"
record("cells/inv_library_name", inv.library_name)
# 原断言：assert inv.source_file == LIB_A, f"source_file={inv.source_file}"
record("cells/inv_source_file", inv.source_file)
# 原断言：assert dff.source_file == LIB_B, f"dff source_file={dff.source_file}"
record("cells/dff_source_file", dff.source_file)

# 重名 cell 冲突（同 cell 出现在两文件 = 库版本混用）：业务异常场景不抛
# 异常（用户裁定）——保留当前（首份）、抛弃后续重复重复 + LIBR::0001 WARN 提醒，
# 合并正常完成并冻结（语义下沉 C++ EXLIBLibrary.merge）。
dup_path = os.path.join(LOG_DIR, "dup.lib")
shutil.copy(LIB_A, dup_path)
dup_db = proj.build_lib_db(name="lib_dup", lib_paths=[LIB_A, dup_path])
assert proj.wait_frozen("lib_dup", timeout=120), \
    "duplicate-cell merge must complete (keep-first semantics)"
dup_lib = dup_db.load_library()
dup_names = sorted(c.name for c in dup_lib.cells)
# 原断言：assert dup_names == ["INV_X1"], f"dup cells={dup_names} (keep first, drop rest)"
record("lib_dup/cell_names", dup_names)
assert len(dup_names) >= 1  # 保留：守卫（后面 dup_lib.cells[0] 按下标取，空列表会以 IndexError 崩溃）

# alpha settings 体系：header schema 直接校验（2026-09-17 裁定）——未知
# 键 raise ValueError 终止（不建库）；alpha=None 合法 + settings 对象落
# 库可读回（normalize 兜底）。
raised = None
try:
    proj.build_lib_db(name="lib_alpha_bad", lib_paths=[LIB_A],
                      alpha={"nonexistent_key": 42})
    raise AssertionError("unknown alpha key must raise ValueError")  # 保留：没抛 ValueError 时让脚本直接失败（否定性检查）
except ValueError as e:
    raised = {"type": type(e).__name__, "msg": str(e)}
# 原断言：assert "nonexistent_key" in str(e), str(e)
record("alpha_bad/exception_type", raised["type"] if raised else None)
record("alpha_bad/nonexistent_key_in_msg",
       raised is not None and "nonexistent_key" in raised["msg"],
       detail=raised)
# 原断言：assert "no available keys" in str(e), str(e)
record("alpha_bad/no_available_keys_in_msg",
       raised is not None and "no available keys" in raised["msg"],
       detail=raised)
# 原断言：assert "lib_alpha_bad" not in proj.list_dbs(), \
record("alpha_bad/db_not_created", "lib_alpha_bad" not in proj.list_dbs(),
       detail=sorted(proj.list_dbs()))
alpha_db = proj.build_lib_db(name="lib_alpha", lib_paths=[LIB_A], alpha=None)
assert proj.wait_frozen("lib_alpha", timeout=120), \
    "valid alpha (None) must build and freeze"
settings = alpha_db.read_object("alpha_settings")
# 原断言：assert settings is not None, "alpha_settings object must persist"
record("alpha/settings_exists", settings is not None)
# 非守卫：仅检查 settings 对象落库可读，返回值无后续解引用
INFO("[DONE] alpha settings: unknown key raises; valid call persists object")
dup_inv = dup_lib.cells[0]
# 原断言：assert dup_inv.source_file == LIB_A, \
record("lib_dup/kept_source_file", dup_inv.source_file)
INFO("[DONE] duplicate cell merge: keep-first + drop rest, no raise")

# 原断言：assert proj.is_db_frozen("lib"), "main lib db stays frozen"
record("lib/still_frozen", proj.is_db_frozen("lib"))

# EMIRProject 类身份：load_project 按 meta class 动态还原（flow 可用）
import fly
restored = fly.load_project(PROJ_PATH)
# 原断言：assert isinstance(restored, EMIRProject), \
record("restored/type", type(restored).__name__)
# 原断言：assert "build_lib_db" in restored.list_flows()
record("restored/build_lib_db_registered",
       "build_lib_db" in restored.list_flows(),
       detail=sorted(restored.list_flows()))
# 原断言：assert restored.get_db("lib").load_library() is not None, \
record("restored/lib_library_readable",
       restored.get_db("lib").load_library() is not None)
# 非守卫：仅检查还原后的库可读，返回值无后续解引用
INFO("[DONE] load_project restored EMIRProject with flows")

get_agent().stop()
INFO("[DONE] test_emir_project_lib")
