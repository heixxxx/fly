#!/usr/bin/env python3
"""B 套生成器：noc_mesh mesh 互连（两级层级，目标 ~30k 实例）。

拓扑（2026-09-15 用户需求「NoC 网格拓扑：路由节点+链路寄存器+仲裁逻辑，
两级 top + mesh 单元 block」）：
  mesh_cell（块定义，~50 实例）：5 方向（N/S/E/W/L）路由节点——每方向
  链路寄存器 2×DFF（clk_noc 域）+ 路由决策寄存器 2×DFF（clk_cfg 域）+
  交叉开关 2×MUX2 + 仲裁（XOR2/AND2/OR2）+ INV；
  noc_mesh（顶层）：GX×GY 个 mesh_cell 实例（东西链路逐列对接、行内）+
  顶层双时钟缓冲树。行尾/边界链路悬空（真实悬空网形态覆盖）。

层级 DEF 直写（OpenROAD write_def 平铺层级——2026-09-15 实测，见
gen_common.py 头注）：mesh_cell.def（块定义）+ noc_mesh.def（顶层，
floorplan+place 级：实例 PLACED 网格坐标、无布线几何）。

产物：set_b/{mesh_cell.v,mesh_cell.sdc,noc_mesh.v,noc_mesh.sdc,
mesh_cell.def,noc_mesh.def,stats.json}。

用法：python3 gen_set_b.py   （固定参数，纯算术确定性）
"""

import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from gen_common import GRID_X, GRID_Y, Module, check_cells_available, \
    connect_ports  # noqa: E402

# —— 结构参数（固定）——
GX, GY = 32, 20            # mesh 阵列：640 tile
DIRS = ("n", "e", "s", "w", "loc")
FLIT = 2                   # 每方向 flit 位宽
OUT_DIR = Path(__file__).resolve().parent / "set_b"
LIBERTY = Path(__file__).resolve().parent.parent / "timing" / \
    "NangateOpenCellLibrary_typical.lib"

# ── mesh_cell 块定义（块视角名字——per-block TWF 的输入网表）─────────
blk = Module("mesh_cell", [("clk_noc", "input"), ("clk_cfg", "input")])
for d in DIRS:
    for i in range(FLIT):
        blk.ports.append((f"flit_in_{d}{i}", "input"))
        blk.ports.append((f"flit_out_{d}{i}", "output"))
for d in DIRS:
    for i in range(FLIT):
        fin = f"flit_in_{d}{i}"
        # 链路寄存器（clk_noc）→ 路由决策寄存器（clk_cfg）→ crossbar
        blk.add_inst(f"lp_{d}{i}", "DFF_X1", D=fin, CK="clk_noc",
                     Q=f"lp_{d}{i}_q")
        blk.add_inst(f"rt_{d}{i}", "DFF_X1", D=f"lp_{d}{i}_q", CK="clk_cfg",
                     Q=f"rt_{d}{i}_q")
    # 仲裁链（每方向一组，比较相邻两路请求）
    blk.add_inst(f"arb_x_{d}", "XOR2_X1", A=f"rt_{d}0_q", B=f"rt_{d}1_q",
                 Z=f"arb_{d}_x")
    blk.add_inst(f"arb_a_{d}", "AND2_X1", A=f"rt_{d}0_q", B=f"arb_{d}_x",
                 Z=f"arb_{d}_a")
    blk.add_inst(f"arb_o_{d}", "OR2_X1", A=f"rt_{d}0_q", B=f"rt_{d}1_q",
                 Z=f"arb_{d}_o")
    blk.add_inst(f"arb_i_{d}", "INV_X1", A=f"arb_{d}_o", Z=f"arb_{d}_n")
for di, dout in enumerate(DIRS):
    src_a = f"rt_{DIRS[(di + 1) % 5]}0_q"      # 相邻方向源 0
    src_b = f"rt_{DIRS[(di + 3) % 5]}1_q"      # 相隔方向源 1
    sel = f"arb_{dout}_a"
    for i in range(FLIT):
        blk.add_inst(f"xb_{dout}{i}", "MUX2_X1", A=src_a, B=src_b, S=sel,
                     Z=f"flit_out_{dout}{i}")

# ── noc_mesh 顶层（640 tile + 双时钟缓冲树）──────────────────────────
top = Module("noc_mesh", [("clk_noc", "input"), ("clk_cfg", "input")])
top.add_inst("cb_root", "CLKBUF_X3", A="clk_noc", Z="nclk_root")
for r in range(GY):
    top.add_inst(f"cb_r_{r}", "CLKBUF_X2", A="nclk_root", Z=f"nclk_r_{r}")
top.add_inst("cb_cfg_root", "CLKBUF_X3", A="clk_cfg", Z="ncfg_root")
for k in range(4):
    top.add_inst(f"cb_cfg_m_{k}", "CLKBUF_X2", A="ncfg_root",
                 Z=f"ncfg_m_{k}")

for r in range(GY):
    for c in range(GX):
        conns = {"clk_noc": f"nclk_r_{r}", "clk_cfg": f"ncfg_m_{r * 4 // GY}"}
        # 东西链路：本 tile out_e → 东邻 in_w；行尾悬空（边界真实形态）
        for i in range(FLIT):
            conns[f"flit_in_w{i}"] = \
                f"n_e_{r}_{c - 1}_{i}" if c > 0 else f"n_west_{r}_{i}"
            conns[f"flit_in_e{i}"] = \
                f"n_e_{r}_{c}_{i}" if c + 1 < GX else f"n_east_{r}_{i}"
            # 南北向：列内对接（r 行出 → r+1 行入）；首末行悬空边界
            conns[f"flit_in_n{i}"] = \
                f"n_s_{r - 1}_{c}_{i}" if r > 0 else f"n_north_{c}_{i}"
            conns[f"flit_in_s{i}"] = \
                f"n_s_{r}_{c}_{i}" if r + 1 < GY else f"n_south_{c}_{i}"
            # 本地端口悬空输入（无驱动网——tile 本地接口未接顶层 IO）
            conns[f"flit_in_loc{i}"] = f"n_loc_{r}_{c}_{i}"
        top.add_inst(f"u_tile_{r}_{c}", "mesh_cell", **conns)
# flit_out 只声明网（悬空输出 = 无负载端，真实悬空形态另一半）
for r in range(GY):
    for c in range(GX):
        for d in DIRS:
            for i in range(FLIT):
                top.nets.add(f"n_{d}_{r}_{c}_{i}_o")

# —— Liberty 防呆（强制，写盘前）：标准单元全集 ⊆ Nangate45；
#    mesh_cell 是本生成器自定义块定义（whitelist 豁免）——
check_cells_available(LIBERTY, blk.cell_refs | top.cell_refs, "set_b",
                      whitelist={"mesh_cell"})
connect_ports(blk)
connect_ports(top)

# ── 渲染产物 ────────────────────────────────────────────────────────
OUT_DIR.mkdir(exist_ok=True)
(OUT_DIR / "mesh_cell.v").write_text(
    blk.to_verilog(f"mesh_cell B 套块定义（{blk.inst_count} 实例）——"
                   f"gen_set_b.py 生成"))
(OUT_DIR / "noc_mesh.v").write_text(
    top.to_verilog(f"noc_mesh B 套顶层：{GX}x{GY} mesh（{top.inst_count} "
                   f"实例）——gen_set_b.py 生成"))
(OUT_DIR / "mesh_cell.sdc").write_text(
    "create_clock -name clk_noc -period 1.200 [get_ports clk_noc]\n"
    "create_clock -name clk_cfg -period 3.000 [get_ports clk_cfg]\n"
    "set_input_delay 0.050 -clock clk_noc [get_ports flit_in_loc0]\n"
    "set_output_delay 0.050 -clock clk_noc [get_ports flit_out_loc0]\n")
(OUT_DIR / "noc_mesh.sdc").write_text(
    "create_clock -name clk_noc -period 1.200 [get_ports clk_noc]\n"
    "create_clock -name clk_cfg -period 3.000 [get_ports clk_cfg]\n")

# ── 层级 DEF（floorplan+place 级：PLACED 网格、无布线几何）──────────
# mesh_cell.def：块内实例局部网格 + 端口摆点
blk_ports = connect_ports(blk)
placements = {}
for k, (iname, _, _) in enumerate(blk.insts):
    col = k % 10
    row = k // 10
    placements[iname] = (1000 + col * GRID_X, 2000 + row * GRID_Y, "N")
ports_xy = {}
for pi, (pin, _) in enumerate(blk.ports):
    ports_xy[pin] = (500 + pi * GRID_X, 0)
blk_die = (0, 0, 1000 + 10 * GRID_X, 2000 + (len(blk.insts) // 10 + 1) * GRID_Y)
(OUT_DIR / "mesh_cell.def").write_text(
    blk.to_def(blk_die, placements, ports_xy))

# noc_mesh.def：tile 阵列 placed（每 tile 占 40×22 网格）+ 顶层单元
TILE_W, TILE_H = 40 * GRID_X, 22 * GRID_Y
placements = {}
for r in range(GY):
    for c in range(GX):
        placements[f"u_tile_{r}_{c}"] = (2000 + c * TILE_W, 2000 + r * TILE_H,
                                         "N")
for k, (iname, _, _) in enumerate(top.insts):
    if iname not in placements:
        placements[iname] = (2000 + k * GRID_X, 2000 + GY * TILE_H, "N")
ports_xy = {"clk_noc": (1000, 1000), "clk_cfg": (3000, 1000)}
top_die = (0, 0, 2000 + GX * TILE_W, 2000 + (GY + 2) * TILE_H)
(OUT_DIR / "noc_mesh.def").write_text(
    top.to_def(top_die, placements, ports_xy))

total_leaf = blk.inst_count * GX * GY
stats = {
    "set": "b", "top": "noc_mesh", "block": "mesh_cell",
    "instances_top": top.inst_count,
    "instances_block": blk.inst_count,
    "instances_total": top.inst_count + total_leaf,
    "nets_top": len(top.nets), "nets_block": len(blk.nets),
    "block_instances_count": GX * GY,
    "clocks": ["clk_noc", "clk_cfg"],
    "params": {"GX": GX, "GY": GY, "FLIT": FLIT},
    "cell_refs": sorted(blk.cell_refs | top.cell_refs),
}
(OUT_DIR / "stats.json").write_text(json.dumps(stats, indent=1) + "\n")
print(f"set_b: total_instances={stats['instances_total']} "
      f"(top {top.inst_count} + {blk.inst_count}x{GX * GY} tile) "
      f"nets_top={len(top.nets)} nets_block={len(blk.nets)}")
