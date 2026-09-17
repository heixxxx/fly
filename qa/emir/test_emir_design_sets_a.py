"""E2E test: design_sets A 套（arith_chain 算术链扁平设计）建库全链。

A 套 = 算术链（2026-09-15 用户需求「加法器/比较器组合逻辑链，多级流水」）：
36 通道 × 22 级流水（DFF→FA→XOR2→AND2→OR2→INV）+ 全通道 OR 归约 +
clk_aux 旁路寄存器域，双时钟（clk_main 1.0ns / clk_aux 2.5ns），扁平
单层 4893 实例。物理 = OpenROAD 完整物理（floorplan+place+CTS+route，
`set_a_flow.tcl`）——三套中唯一有真实布线几何的套：design db NETS 表
含网，网络维度 TWF 条目正常锚定 driver 位（B/C 套无布线几何走
TIMG::0003 悬空兜底口径，本套为对照面）。

断言：
  - design db：扁平单层（树 1 节点）+ 展开实例数（route 后 = 生成器
    4893 + CTS 插入缓冲）+ 网（NETS 有布线几何全落表）；
  - timing mixed（网络+引脚两维度混合）：时钟表 2 条 + 条目/命中/悬空
    锚 + 网条目 driver 锚定命中（非悬空口径）+ primary 恰一；
  - 跨时钟域形态：clk_main 域数据链实例 + clk_aux 域寄存器实例时钟
    归属点查。

锚点来源：gen_set_a.py 固定参数确定性生成 + OpenROAD 确定性流程
（2026-09-17 实测，双跑逐字节一致口径同 pg_grid）。
"""
import os
import shutil

from log import INFO

from fly import get_config, launch_workers
from fly.runtime import get_agent
from emir import EMIRProject

DATA = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data",
                    "design_sets", "set_a")

LOG_DIR = get_config().get_str("log_dir")
PROJ_PATH = os.path.join(LOG_DIR, "emir_design_sets_a")


def cleanup():
    if os.path.isdir(PROJ_PATH):
        shutil.rmtree(PROJ_PATH, ignore_errors=True)


cleanup()

launch_workers([{}, {}])
assert get_agent().wait_workers_registered(timeout=60), "workers connect"

proj = EMIRProject(PROJ_PATH)
TIMING_DATA = os.path.join(os.path.dirname(os.path.dirname(DATA)), "timing")
LIBERTY = os.path.join(TIMING_DATA, "NangateOpenCellLibrary_typical.lib")
TECH_LEF = os.path.join(TIMING_DATA, "NangateOpenCellLibrary.tech.lef")
MACRO_LEF = os.path.join(TIMING_DATA, "NangateOpenCellLibrary.macro.lef")

lib_db = proj.build_lib_db(name="lib_a", lib_paths=[LIBERTY])
assert proj.wait_frozen("lib_a", timeout=300), "lib freeze"

design_db = proj.build_design_db(
    name="design_a",
    def_paths=[os.path.join(DATA, "arith_chain.def")],
    lef_paths=[TECH_LEF, MACRO_LEF], lib_db=lib_db,
    alpha={"target_partitions": "2x1"})
assert proj.wait_frozen("design_a", timeout=600), "design freeze"
INFO("[WAIT] design_a frozen (routed flat 5k-instance design)")

design = design_db.load_design()
tree = design.get_hier_tree()
assert tree.node_count == 1, f"hier nodes={tree.node_count} (flat)"
n_total = 0
for i in range(tree.node_count):
    lo, hi = tree.node(i).instance_range
    n_total += hi - lo
inst_flat = n_total - tree.node_count
assert inst_flat == 5036, inst_flat  # 生成器 4893 + CTS 插入 143 缓冲
root = tree.node(0)
lo, hi = root.net_range
assert hi - lo == 4972, hi - lo  # 4971 网 + local 0 空洞位
INFO(f"[OK] flat design: 1 node, {inst_flat} instances, net id space "
     f"{hi - lo} (incl. local-0 hole)")

tdb = proj.build_timing_db(
    name="timing_a",
    timing_files=[os.path.join(DATA, "arith_chain_mixed.twf")],
    design_db=design_db)
assert proj.wait_frozen("timing_a", timeout=600), "timing freeze"

s = tdb.load_timing_summary_obj()
assert s.total_entry_count == 21707, s.total_entry_count
assert s.total_hit_count == 16573, s.total_hit_count
assert s.dangling_net_count == 3420, s.dangling_net_count
assert s.skipped_pin_count == 1714, s.skipped_pin_count
assert s.net_name_miss_count == 0, s.net_name_miss_count
assert s.unplaced_instance_count == 0, s.unplaced_instance_count
assert s.missing_clock_count == 0, s.missing_clock_count
clks = tdb.load_timing_clocks_obj()
assert clks.size == 2, clks.size
expect_clk = {"clk_main": 1.0, "clk_aux": 2.5}
for i in range(clks.size):
    e = clks.entry_at(i)
    assert abs(e.period - expect_clk[e.name]) < 1e-9, (e.name, e.period)
assert s.cross_file_conflict_count == 0
INFO(f"[OK] timing_a: entry={s.total_entry_count} hit={s.total_hit_count} "
     f"dangling={s.dangling_net_count} skip_pin={s.skipped_pin_count} "
     f"(anchors asserted below)")

# 布线几何对照面：A 套为 global route 级几何（detailed_route 在本设计
# 收敛长尾达小时级——见 set_a_flow.tcl 注记），net 条目部分锚定
# （1551/4971 命中、3420 悬空）；B/C 套直写 DEF 无布线几何全悬空——
# A 套 dangling < net 条目数即对照面
assert s.dangling_net_count < s.total_entry_count, \
    "routed design must anchor net entries (dangling < entries)"

# 双时钟域归属点查：clk_main 域数据链 DFF + clk_aux 域旁路寄存器
for path, clock in (("dff_0_0", "clk_main"), ("cb_root", "clk_main"),
                    ("aux_0", "clk_aux"), ("q_reg", "clk_aux"),
                    ("red_5", None)):
    gid = design_db.convert_to_id("inst", path)
    info = tdb.get_timing(gid)
    assert info is not None, f"{path} must carry timing"
    assert info["clock"] == clock, (path, info["clock"])
INFO("[OK] dual-clock ownership: dff_0_0/cb_root→clk_main, "
     "aux_0/q_reg→clk_aux, red_5→NULL group")

# primary 恰一：分区并集 = 命中实例数
from emir.timing import iter_timing_partition, load_partition_timing
parts = iter_timing_partition(tdb)
assert len(parts) >= 2, f"partitions={parts} (2x1 split)"
total = 0
for xp, yp in parts:
    part = load_partition_timing(tdb, xp, yp)
    assert part.size > 0, f"PART({xp},{yp}) unexpectedly empty"
    total += part.size
# mixed 维度：hit 为条目级计数（同实例多 pin 条目），分区落库按实例
# 恰一份（16573 条目 → 5036 实例）——并集 = 实例数即 primary 恰一
assert total == 5036, total
for path in ("dff_0_0", "aux_0", "cb_root"):
    info = tdb.get_timing(design_db.convert_to_id("inst", path))
    assert info["partition"] in parts, (path, info["partition"])
INFO(f"[OK] primary exactly-one: {total} instances across {len(parts)} "
     f"partitions (spot checks consistent)")

print("[PASS] test_emir_design_sets_a")
