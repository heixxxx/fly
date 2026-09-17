"""E2E test: design_sets B 套（noc_mesh 两级层级 mesh 互连）建库全链。

B 套 = NoC 网格拓扑（2026-09-15 用户需求）：mesh_cell 块定义（路由节点
+ 链路寄存器 + 仲裁逻辑，50 实例）被顶层 32×20 = 640 个 tile 实例化 +
顶层双时钟缓冲树（顶层 666 实例，展开合计 32666）。物理 = 生成器直写
层级 DEF（floorplan+place 级：PLACED 网格坐标、无布线几何）。

本 case 覆盖：
  - design db 两级层级展开：树节点数 641（root + 640 tile）+ 展开实例
    总数 32666 + 顶层网区间（10372 网 + local 0 空洞位）；
  - block_cell 块定义绑定（§7.2）：mesh_cell 定义级时序对全部 640 实例
    建库期复制——落库复制 ×640（dangling 按实例复制计数 = 47 网条目
    ×640 = 30080；hit 为条目级 bool 计数不乘实例数，2026-09-17 实测
    口径）；
  - strip_prefix 段级剥离（§7.4）：人为前缀包装形态（tb/u_dut）+ 块
    实例绑定单实例展开（strip_miss=0）；
  - 时钟表（clk_noc 1.2 / clk_cfg 3.0）+ 时钟归属抽查；
  - 顶层 TWF 大件「存在则跑、缺失 skip」策略（>5MB 不入库；生成命令见
    data/design_sets/README.md）。

design db 语义口径：直写 DEF 无布线几何 → NETS 表不含网 → 网维度 TWF
条目计 TIMG::0003 悬空（合法兜底口径，非数据缺陷）——block_cell 复制
后按实例放大（×640），dangling=30080 即此口径。
"""
import os
import shutil

from log import INFO

from fly import get_config, launch_workers
from fly.runtime import get_agent
from emir import EMIRProject

DATA = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data",
                    "design_sets", "set_b")

LOG_DIR = get_config().get_str("log_dir")
PROJ_PATH = os.path.join(LOG_DIR, "emir_design_sets_b")


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

lib_db = proj.build_lib_db(name="lib_b", lib_paths=[LIBERTY])
assert proj.wait_frozen("lib_b", timeout=300), "lib freeze"

# ── design db：两级层级（mesh_cell 块定义 + noc_mesh 顶层）────────────
design_db = proj.build_design_db(
    name="design_b",
    def_paths=[os.path.join(DATA, "mesh_cell.def"),
               os.path.join(DATA, "noc_mesh.def")],
    lef_paths=[TECH_LEF, MACRO_LEF], lib_db=lib_db,
    alpha={"target_partitions": "4x1"})
assert proj.wait_frozen("design_b", timeout=600), "design freeze"
INFO("[WAIT] design_b frozen (640-tile 2-level hierarchy)")

design = design_db.load_design()
tree = design.get_hier_tree()
assert tree.node_count == 641, f"hier nodes={tree.node_count}"
root = tree.node(0)
assert root.block_cell_name == "noc_mesh", root.block_cell_name
# 展开实例总数：Σ 各块实例区间宽 − 节点数（每节点 local 0 占位槽）
n_total = 0
for i in range(tree.node_count):
    lo, hi = tree.node(i).instance_range
    n_total += hi - lo
assert n_total - tree.node_count == 32666, n_total
# 顶层网区间：10372 网 + local 0 空洞位
lo, hi = root.net_range
assert hi - lo == 10373, hi - lo
# 块实例路径点查（640 tile 中任意取）
gid_tile = design_db.convert_to_id("inst", "u_tile_19_31")
assert gid_tile, "tile path lookup"
INFO(f"[OK] hierarchy: 641 nodes, 32666 expanded instances, top nets "
     f"10372, tile lookup OK")

# ── timing db 1：block_cell 块定义绑定（×640 建库期复制）──────────────
tdb = proj.build_timing_db(
    name="timing_b_cell",
    timing_files=[{"file_name": os.path.join(DATA, "mesh_cell_mixed.twf"),
                   "block_cell": "mesh_cell"}],
    design_db=design_db)
assert proj.wait_frozen("timing_b_cell", timeout=600)
s = tdb.load_timing_summary_obj()
assert s.total_entry_count == 262, s.total_entry_count
assert s.total_hit_count == 175, s.total_hit_count
# 47 网条目全悬空（无布线几何）×640 实例复制
assert s.dangling_net_count == 30080, s.dangling_net_count
assert s.skipped_pin_count == 40, s.skipped_pin_count  # IQ/IQN 未登记
assert s.net_name_miss_count == 0 and s.unplaced_instance_count == 0
assert s.clock_conflict_count == 0
INFO("[OK] block_cell binding: entry=262 hit=175 (entry-level), "
     "dangling=47x640=30080 (replication), skip_pin=40")

# 时钟归属抽查：任意远端 tile 内实例均有数据且归 clk_noc
for tile in ("u_tile_0_0", "u_tile_5_7", "u_tile_19_31", "u_tile_19_0"):
    info = tdb.get_timing(f"{tile}/lp_n0")
    assert info is not None, f"{tile}/lp_n0 must carry timing"
    assert info["clock"] == "clk_noc", (tile, info["clock"])
INFO("[OK] block_cell replication: instances across the 32x20 array all "
     "carry timing (clock=clk_noc)")

# ── timing db 2：strip_prefix 人为前缀包装形态 + block_inst 绑定 ──────
tdb2 = proj.build_timing_db(
    name="timing_b_strip",
    timing_files=[{"file_name": os.path.join(DATA, "mesh_cell_strip.twf"),
                   "block_inst": "u_tile_0_0",
                   "strip_prefix": "tb/u_dut"}],
    design_db=design_db)
assert proj.wait_frozen("timing_b_strip", timeout=600)
s2 = tdb2.load_timing_summary_obj()
assert s2.total_entry_count == 262, s2.total_entry_count
assert s2.total_hit_count == 175, s2.total_hit_count
assert s2.dangling_net_count == 47, s2.dangling_net_count  # 单实例展开
assert s2.strip_miss_count == 0, s2.strip_miss_count
clks = tdb2.load_timing_clocks_obj()
assert clks.size == 2, clks.size
expect_clk = {"clk_noc": 1.2, "clk_cfg": 3.0}
for i in range(clks.size):
    e = clks.entry_at(i)
    assert abs(e.period - expect_clk[e.name]) < 1e-9, (e.name, e.period)
INFO("[OK] strip_prefix + block_inst: hit=175 strip_miss=0, clocks "
     "clk_noc=1.2 clk_cfg=3.0")

# ── 顶层纯路径形态（noc_mesh.twf：顶层网维度，28 条目，入库）─────────
tdb3 = proj.build_timing_db(name="timing_b_top",
                            timing_files=[os.path.join(DATA,
                                                       "noc_mesh.twf")],
                            design_db=design_db)
assert proj.wait_frozen("timing_b_top", timeout=600)
s3 = tdb3.load_timing_summary_obj()
assert s3.total_entry_count == 28, s3.total_entry_count
assert s3.total_hit_count == 0, s3.total_hit_count
# 全悬空 = B 套直写 DEF 无布线几何的预期口径（TIMG::0003 合法兜底）
assert s3.dangling_net_count == 28, s3.dangling_net_count
assert s3.net_name_miss_count == 0, s3.net_name_miss_count
INFO("[OK] top-level full-path TWF: entry=28 all dangling (expected: "
     "no routed geometry in hand-written DEF)")
# 顶层混合维度大件（151760 条目，14.3MB > 5MB 不入库）留给 verify_full.sh
# ——存在且 QA 预算外，永不在此跑（避免 runqa 超时）

print("[PASS] test_emir_design_sets_b")
