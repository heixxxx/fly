"""E2E test: design_sets C 套（hybrid_soc 三级层级混合设计）建库全链。

C 套 = 计算阵列 + 互连 + 控制（2026-09-15 用户需求）：pe_core /
tile_router（叶 block）→ compute_tile（中间 block）→ hybrid_soc（顶层）
+ ctrl_block（中间 block）。QA 时间预算内用 --mini 同构子集全链全断言
（块定义输入件与全量通用）；全量（30×25 = 102192 展开实例）由
data/design_sets/verify_full.sh 手动跑。

本 case 覆盖（timing-db-plan §7 块绑定三形态 + 时钟合并）：
  - design db 层级展开：树节点数 18（root + 4 tile + 8 pe + 4 router +
    ctrl）+ 多级路径点查；
  - block_inst 块实例绑定（§7.1）：多级路径 "u_tile_1_0/u_pe_0" 局部名
    偏移换算；
  - block_cell 块定义绑定（§7.2）：tile_router 定义级时序对全部 4 实例
    复制（落库复制 ×4——hit 为条目级 bool 计数不乘实例数，dangling 按
    实例复制计数，2026-09-17 实测口径）；
  - strip_prefix 段级剥离（§7.4）：人为前缀包装形态（tb/u_dut 前缀的
    mixed TWF）+ 块实例绑定——剥后 pin 条目命中；
  - 四文件块 TWF 时钟表合并：clk_core/clk_noc/clk_cfg 三时钟同名同周期
    零冲突 + 时钟归属抽查（pe→clk_core / rt→clk_noc / ctrl→clk_cfg）；
  - primary 恰一：抽查实例 partition 与分区遍历一致；
  - 全量大件「存在则断言规模头、缺失 skip」策略（>5MB 件不入库）。

design db 语义口径：直写 DEF 无布线几何 → NETS 表不含网 → 网维度 TWF
条目计 TIMG::0003 悬空（合法兜底口径，非数据缺陷——见
data/timing/README.md「建库消费语义」节）。

锚点来源（2026-09-17 实测，gen_set_c.py 固定参数确定性生成）：四绑定
文件条目 219+151+219+565 = 1154；hit 787 = pe_mixed 164 + rt_cell 95 +
pe_strip 164 + ctrl 364；dangling 303 = 39 + (36×4) + 39 + 81；skip_pin
172 = 各文件 DFF 的 IQ/IQN 未登记 LEF pin（16+20+16+120）。
"""
import os
import shutil

from log import INFO, WARN

from fly import get_config, launch_workers
from fly.runtime import get_agent
from emir import EMIRProject

DATA = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data",
                    "design_sets", "set_c")

LOG_DIR = get_config().get_str("log_dir")
PROJ_PATH = os.path.join(LOG_DIR, "emir_design_sets_c")


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

lib_db = proj.build_lib_db(name="lib_c", lib_paths=[LIBERTY])
assert proj.wait_frozen("lib_c", timeout=300), "lib freeze"

# ── design db：mini 三级层级（块定义 DEF + mini 顶层 DEF）──────────────
LEVEL_DEFS = [os.path.join(DATA, "pe_core.def"),
              os.path.join(DATA, "tile_router.def"),
              os.path.join(DATA, "ctrl_block.def"),
              os.path.join(DATA, "compute_tile.def"),
              os.path.join(DATA, "hybrid_soc_mini.def")]
design_db = proj.build_design_db(
    name="design_c_mini", def_paths=LEVEL_DEFS,
    lef_paths=[TECH_LEF, MACRO_LEF], lib_db=lib_db)
assert proj.wait_frozen("design_c_mini", timeout=300), "design freeze"
INFO("[WAIT] design_c_mini frozen (mini 3-level hierarchy)")

design = design_db.load_design()
tree = design.get_hier_tree()
# 树节点：root + 4 tile + 8 pe + 4 router + 1 ctrl = 18
assert tree.node_count == 18, f"hier nodes={tree.node_count}"
root = tree.node(0)
assert root.block_cell_name == "hybrid_soc", root.block_cell_name
INFO(f"[OK] hierarchy tree: {tree.node_count} nodes "
     f"(root={root.block_cell_name})")

# 多级路径点查（块实例路径逐段解析）
gid_pe = design_db.convert_to_id("inst", "u_tile_1_0/u_pe_0")
gid_leaf = design_db.convert_to_id("inst", "u_tile_1_0/u_pe_0/dff0_0")
gid_ctrl = design_db.convert_to_id("inst", "u_ctrl")
assert gid_pe and gid_leaf and gid_ctrl, (gid_pe, gid_leaf, gid_ctrl)
INFO("[OK] hierarchy path lookup: u_tile_1_0/u_pe_0{,/dff0_0}, u_ctrl")

# ── timing db：四块绑定文件一次建库（两形态 + strip + 时钟合并）────────
from emir.timing import iter_timing_partition, load_partition_timing

tdb = proj.build_timing_db(
    name="timing_c_mini",
    timing_files=[
        {"file_name": os.path.join(DATA, "pe_core_mixed.twf"),
         "block_inst": "u_tile_1_0/u_pe_0"},
        {"file_name": os.path.join(DATA, "tile_router_mixed.twf"),
         "block_cell": "tile_router"},
        {"file_name": os.path.join(DATA, "pe_core_strip.twf"),
         "block_inst": "u_tile_0_1/u_pe_1", "strip_prefix": "tb/u_dut"},
        {"file_name": os.path.join(DATA, "ctrl_block_mixed.twf"),
         "block_inst": "u_ctrl"},
    ],
    design_db=design_db)
assert proj.wait_frozen("timing_c_mini", timeout=300), "timing freeze"
INFO("[WAIT] timing_c_mini frozen (4 bound files)")

s = tdb.load_timing_summary_obj()
assert s.total_entry_count == 1154, s.total_entry_count
assert s.total_hit_count == 787, s.total_hit_count
assert s.dangling_net_count == 303, s.dangling_net_count
assert s.skipped_pin_count == 172, s.skipped_pin_count
assert s.strip_miss_count == 0, s.strip_miss_count
assert s.net_name_miss_count == 0, s.net_name_miss_count
assert s.clock_conflict_count == 0, s.clock_conflict_count
assert s.missing_clock_count == 0, s.missing_clock_count
assert s.unplaced_instance_count == 0, s.unplaced_instance_count
assert len(s.files) == 4, len(s.files)
INFO("[OK] merged summary: entry=1154 hit=787 dangling=303 skip_pin=172 "
     "(2 binding forms + strip)")

# 时钟表：三块 TWF 三时钟同名同周期合并零冲突
clks = tdb.load_timing_clocks_obj()
assert clks.size == 3, clks.size
expect_clk = {"clk_core": 1.0, "clk_noc": 1.6, "clk_cfg": 4.0}
for i in range(clks.size):
    e = clks.entry_at(i)
    assert abs(e.period - expect_clk[e.name]) < 1e-9, (e.name, e.period)
INFO("[OK] merged clocks: clk_core=1.0 clk_noc=1.6 clk_cfg=4.0 "
     "(no conflict)")

# 时钟归属（clock id 域重映射后抽查）：pe→clk_core / rt→clk_noc /
# ctrl→clk_cfg；block_cell 复制语义 = 全部 4 个 rt 实例均有数据
for path, clock in (("u_tile_1_0/u_pe_0/dff0_0", "clk_core"),
                    ("u_tile_0_0/u_rt/rr_n0", "clk_noc"),
                    ("u_tile_0_1/u_rt/rr_n0", "clk_noc"),
                    ("u_tile_1_0/u_rt/rr_n0", "clk_noc"),
                    ("u_tile_1_1/u_rt/rr_n0", "clk_noc"),
                    ("u_ctrl/csr_0", "clk_cfg")):
    gid = design_db.convert_to_id("inst", path)
    info = tdb.get_timing(gid)
    assert info is not None, f"{path} must carry timing"
    assert info["clock"] == clock, (path, info["clock"])
INFO("[OK] clock ownership: pe→clk_core, all 4 rt copies→clk_noc "
     "(block_cell replication), ctrl→clk_cfg")

# strip 场景命中：tb/u_dut 前缀剥后 pin 条目落在 u_tile_0_1/u_pe_1
info_strip = tdb.get_timing("u_tile_0_1/u_pe_1/fa0_0")
assert info_strip is not None, "stripped entries must land on u_pe_1"
assert info_strip["clock"] is None  # 组合逻辑数据 pin → NULL 组（twf_gen
# 已知语义弱化形态，见 data/timing/README.md）
INFO("[OK] strip_prefix: entries land on u_tile_0_1/u_pe_1 after "
     "peeling tb/u_dut")

# primary 恰一：抽查实例 partition 与分区遍历一致 + 每分区非空
parts = iter_timing_partition(tdb)
assert len(parts) >= 1, f"partitions={parts}"
total = 0
for xp, yp in parts:
    part = load_partition_timing(tdb, xp, yp)
    assert part.size > 0, f"PART({xp},{yp}) unexpectedly empty"
    total += part.size
assert total == 300, f"partitioned instances={total} expect 300"
for path in ("u_tile_1_0/u_pe_0/dff0_0", "u_tile_0_0/u_rt/rr_n0",
             "u_ctrl/csr_0"):
    info = tdb.get_timing(design_db.convert_to_id("inst", path))
    assert info["partition"] in parts, (path, info["partition"])
INFO(f"[OK] primary exactly-one: {total} instances across {len(parts)} "
     f"partition(s), spot checks consistent")

# ── 全量大件：存在则断言规模头，缺失 skip（>5MB 件不入库策略）─────────
full_def = os.path.join(DATA, "hybrid_soc.def")
if os.path.isfile(full_def):
    n_comp = 0
    in_components = False
    with open(full_def, "r", encoding="utf-8") as f:
        for line in f:
            if "COMPONENTS" in line and ";" in line:
                in_components = True
                continue
            if in_components:
                if line.startswith("END"):
                    break
                if line.strip().startswith("-"):
                    n_comp += 1
    assert n_comp == 842, f"full top COMPONENTS={n_comp}"
    INFO("[OK] full-set presence check: 842 top instances in "
         "hybrid_soc.def (expanded total 102192 — verify_full.sh runs "
         "the full build)")
else:
    # WARN 而非 INFO：分档规则下大件应随源码就位，此分支理论上不可触发
    # ——万一触发即覆盖缩水，必须显式可见（PASS 语义不变，仅日志升级）。
    WARN("[SKIP] full-set large files not present — coverage shrunk "
         "(regenerate via gen_set_c.py; full build via verify_full.sh)")

print("[PASS] test_emir_design_sets_c")
