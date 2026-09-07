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
assert "build_lib_db" in proj.list_flows(), f"flows={proj.list_flows()}"

# ── build_lib_db：两文件分布式解析（异步返回）──
lib_db = proj.build_lib_db(name="lib", lib_paths=[LIB_A, LIB_B])
INFO(f"build_lib_db returned db (async): {lib_db}")

assert proj.wait_frozen("lib", timeout=120), "lib db should freeze after merge"
INFO("[WAIT] lib db frozen — full pipeline done")

# ── 校验整合容器 ──
library = lib_db.load_library()
cell_names = sorted(c.name for c in library.cells)
assert cell_names == ["DFF_X1", "INV_X1"], f"cells={cell_names}"
assert library.template_names == ["power_2d"], \
    f"templates={library.template_names}"

inv = next(c for c in library.cells if c.name == "INV_X1")
pin_a = next(p for p in inv.pins if p.name == "A")
assert abs(pin_a.capacitance - 1.5) < 1e-12, f"cap={pin_a.capacitance}"

dff = next(c for c in library.cells if c.name == "DFF_X1")
assert dff.is_sequence_cell, "DFF_X1 should be marked sequential (ff group)"

# 来源可追溯（裁定 12/13）：cell 记录所属库组名与解析来源文件完整路径
assert inv.library_name == "minitest_typ", f"library_name={inv.library_name}"
assert inv.source_file == LIB_A, f"source_file={inv.source_file}"
assert dff.source_file == LIB_B, f"dff source_file={dff.source_file}"

# 重名 cell 冲突（同 cell 出现在两文件 = 库版本混用）：业务异常场景不抛
# 异常（用户裁定）——保留当前（首份）、抛弃后续重复 + LIBR::0001 WARN 提醒，
# 合并正常完成并冻结（语义下沉 C++ EXLIBLibrary.merge）。
dup_path = os.path.join(LOG_DIR, "dup.lib")
shutil.copy(LIB_A, dup_path)
dup_db = proj.build_lib_db(name="lib_dup", lib_paths=[LIB_A, dup_path])
assert proj.wait_frozen("lib_dup", timeout=120), \
    "duplicate-cell merge must complete (keep-first semantics)"
dup_lib = dup_db.load_library()
dup_names = sorted(c.name for c in dup_lib.cells)
assert dup_names == ["INV_X1"], f"dup cells={dup_names} (keep first, drop rest)"
dup_inv = dup_lib.cells[0]
assert dup_inv.source_file == LIB_A, \
    f"kept cell should be the first occurrence ({LIB_A}), got {dup_inv.source_file}"
INFO("[OK] duplicate cell merge: keep-first + drop rest, no raise")

assert proj.is_db_frozen("lib"), "main lib db stays frozen"

# EMIRProject 类身份：load_project 按 meta class 动态还原（flow 可用）
import fly
restored = fly.load_project(PROJ_PATH)
assert isinstance(restored, EMIRProject), \
    f"load_project should restore EMIRProject, got {type(restored)}"
assert "build_lib_db" in restored.list_flows()
assert restored.get_db("lib").load_library() is not None, \
    "restored project should expose LIBLibrary"
INFO("[OK] load_project restored EMIRProject with flows")

get_agent().stop()
INFO("[PASS] test_emir_project_lib")
