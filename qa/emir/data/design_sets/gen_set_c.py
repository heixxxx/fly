#!/usr/bin/env python3
"""C 套生成器：hybrid_soc 混合设计（≥3 级层级，目标 ~100k 实例）。

拓扑（2026-09-15 用户需求「计算阵列+互连+控制，≥3 级 top + 中间 block +
叶 block」）：
  pe_core（叶，clk_core 域）：3 级 ALU 流水（FA/XOR2/AND2/OR2/NAND2/MUX2）
  + 流水寄存器；
  tile_router（叶，clk_noc 域）：4+1 方路由寄存器 + 交叉开关 + 仲裁；
  compute_tile（中间）：pe_core×2 + tile_router×1 + 胶合寄存器；
  ctrl_block（中间，clk_cfg 域）：CSR + FSM + 解码；
  hybrid_soc（顶层）：GX×GY compute_tile 阵列 + u_ctrl + 三时钟缓冲树 +
  数据总线扇出缓冲。行尾/边界链路悬空（真实悬空网形态）。

--mini 生成同构小子集（GX×GY=2×2，其余块定义完全相同）供 QA 时间预算内
全链全断言；块定义输入件（v/sdc/def/twf）mini 与全量通用。

层级 DEF 直写（OpenROAD write_def 平铺层级——见 gen_common.py 头注）。

产物：set_c/{pe_core,tile_router,ctrl_block,compute_tile}.{v,sdc,def}
+ set_c/hybrid_soc.{v,sdc,def}（全量顶层）+ set_c/hybrid_soc_mini.{v,def}
+ set_c/stats.json。

用法：python3 gen_set_c.py [--mini]
"""

import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from gen_common import GRID_X, GRID_Y, Module, check_cells_available, \
    connect_ports  # noqa: E402

OUT_DIR = Path(__file__).resolve().parent / "set_c"
LIBERTY = Path(__file__).resolve().parent.parent / "timing" / \
    "NangateOpenCellLibrary_typical.lib"
BLOCK_CELLS = {"pe_core", "tile_router", "ctrl_block", "compute_tile"}
# 阵列规模单一来源（全量/mini 共用；stats.json 的 GX/GY 口径自此取——
# 终审 #5：原 stats 硬编码 40×26 与实际阵列 30×25 失实）
FULL_GRID = (30, 25)
MINI_GRID = (2, 2)

# ── pe_core（叶）：3 级 ALU 流水 ─────────────────────────────────────
pe = Module("pe_core", [("clk_core", "input")]
            + [(f"a{i}", "input") for i in range(8)]
            + [("y0", "output"), ("y1", "output")])
for i in range(8):
    pe.add_inst(f"ib_{i}", "INV_X1", A=f"a{i}", Z=f"na{i}")
# 流水线操作数：本地/邻近（扇出 ≤3，GPL-0305 教训同 A 套）
for lvl in range(3):
    for i in range(2):
        lane = f"reg{lvl}_{i}"
        acc_in = f"acc{lvl - 1}_{i}" if lvl > 0 else f"na{2 * i}"
        op_b = f"acc{lvl - 1}_{1 - i}" if lvl > 0 else f"na{2 * i + 1}"
        pe.add_inst(f"dff{lvl}_{i}", "DFF_X1", D=acc_in, CK="clk_core",
                    Q=f"r{lvl}_{i}")
        pe.add_inst(f"fa{lvl}_{i}", "FA_X1", A=f"r{lvl}_{i}", B=op_b,
                    CI=acc_in, S=f"s{lvl}_{i}", CO=f"co{lvl}_{i}")
        pe.add_inst(f"xor{lvl}_{i}", "XOR2_X1", A=f"s{lvl}_{i}",
                    B=f"r{lvl}_{i}", Z=f"x{lvl}_{i}")
        pe.add_inst(f"and{lvl}_{i}", "AND2_X1", A=f"x{lvl}_{i}",
                    B=f"s{lvl}_{i}", Z=f"d{lvl}_{i}")
        pe.add_inst(f"or{lvl}_{i}", "OR2_X1", A=f"d{lvl}_{i}",
                    B=f"co{lvl}_{i}", Z=f"z{lvl}_{i}")
        pe.add_inst(f"nand{lvl}_{i}", "NAND2_X1", A=f"z{lvl}_{i}",
                    B=f"r{lvl}_{i}", Z=f"acc{lvl}_{i}")
    pe.add_inst(f"sel{lvl}", "MUX2_X1", A=f"acc{lvl}_0", B=f"acc{lvl}_1",
                S=f"s{lvl}_0", Z=f"sel{lvl}_z")
pe.add_inst("y0_reg", "DFF_X1", D="sel2_z", CK="clk_core", Q="y0")
pe.add_inst("y1_reg", "DFF_X1", D="acc2_1", CK="clk_core", Q="ny1")
pe.add_inst("y1_b", "BUF_X1", A="ny1", Z="y1")

# ── tile_router（叶）：5 方路由 ──────────────────────────────────────
rt = Module("tile_router", [("clk_noc", "input")]
            + [(f"in_{d}{i}", "input") for d in ("n", "e", "s", "w", "l")
               for i in range(2)]
            + [(f"out_{d}{i}", "output") for d in ("n", "e", "s", "w", "l")
               for i in range(2)])
DIRS5 = ("n", "e", "s", "w", "l")
for d in DIRS5:
    for i in range(2):
        rt.add_inst(f"rr_{d}{i}", "DFF_X1", D=f"in_{d}{i}", CK="clk_noc",
                    Q=f"rq_{d}{i}")
for di, dout in enumerate(DIRS5):
    src_a = f"rq_{DIRS5[(di + 2) % 5]}0"
    src_b = f"rq_{DIRS5[(di + 4) % 5]}1"
    rt.add_inst(f"ra_{dout}", "XOR2_X1", A=src_a, B=src_b, Z=f"rax_{dout}")
    for i in range(2):
        rt.add_inst(f"rx_{dout}{i}", "MUX2_X1", A=src_a, B=src_b,
                    S=f"rax_{dout}", Z=f"out_{dout}{i}")

# ── compute_tile（中间）：2×pe + router + 胶合 ───────────────────────
ct = Module("compute_tile",
            [("clk_core", "input"), ("clk_noc", "input")]
            + [(f"a{i}", "input") for i in range(8)]
            + [("y0", "output"), ("y1", "output")]
            + [(f"noc_in_{d}{i}", "input") for d in DIRS5[:4]
               for i in range(2)]
            + [(f"noc_out_{d}{i}", "output") for d in DIRS5[:4]
               for i in range(2)])
for p in range(2):
    conns = {"clk_core": "clk_core"}
    for i in range(8):
        conns[f"a{i}"] = f"a{i}"
    ct.add_inst(f"u_pe_{p}", "pe_core", **dict(conns, y0=f"pe{p}_y0",
                                               y1=f"pe{p}_y1"))
ct.add_inst("u_rt", "tile_router", clk_noc="clk_noc",
            **{f"in_{d}{i}": f"noc_in_{d}{i}" for d in DIRS5[:4]
               for i in range(2)},
            **{f"in_l{i}": f"pe0_y{i}" for i in range(2)},
            **{f"out_{d}{i}": f"noc_out_{d}{i}" for d in DIRS5[:4]
               for i in range(2)},
            out_l0="ct_mux0", out_l1="ct_mux1")
ct.add_inst("g_dff0", "DFF_X1", D="ct_mux0", CK="clk_core", Q="g_q0")
ct.add_inst("g_dff1", "DFF_X1", D="ct_mux1", CK="clk_core", Q="g_q1")
ct.add_inst("g_mux0", "MUX2_X1", A="pe0_y0", B="g_q0", S="pe1_y1",
            Z="ct_mux0")
ct.add_inst("g_mux1", "MUX2_X1", A="pe1_y0", B="g_q1", S="pe0_y1",
            Z="ct_mux1")
ct.add_inst("g_and0", "AND2_X1", A="g_q0", B="pe1_y0", Z="ct_and0")
ct.add_inst("g_and1", "AND2_X1", A="g_q1", B="pe0_y1", Z="y1")
ct.add_inst("g_buf", "BUF_X1", A="ct_and0", Z="y0")

# ── ctrl_block（中间）：clk_cfg 域 CSR + FSM + 解码 ──────────────────
cb = Module("ctrl_block", [("clk_cfg", "input")]
            + [(f"cmd{i}", "input") for i in range(4)]
            + [(f"tile_irq{i}", "input") for i in range(4)]
            + [(f"status{i}", "output") for i in range(4)])
for i in range(48):
    src = f"cmd{i % 4}" if i < 8 else f"csr_{i - 8}"
    cb.add_inst(f"csr_{i}", "DFF_X1", D=src, CK="clk_cfg", Q=f"csr_{i}")
for i in range(8):
    cb.add_inst(f"fsm_{i}", "DFF_X1", D=f"fsm_d{i}", CK="clk_cfg",
                Q=f"fsm_q{i}")
    cb.add_inst(f"fdc_{i}", "NAND2_X1", A=f"fsm_q{i}",
                B=f"csr_{40 + i}", Z=f"fsm_d{i}")
for i in range(12):
    cb.add_inst(f"dec_a_{i}", "AND2_X1", A=f"csr_{i}", B=f"cmd{i % 4}",
                Z=f"da_{i}")
for i in range(8):
    cb.add_inst(f"dec_o_{i}", "OR2_X1", A=f"da_{i}", B=f"da_{i + 4}",
                Z=f"do_{i}")
for i in range(8):
    cb.add_inst(f"dec_m_{i}", "MUX2_X1", A=f"do_{i}", B=f"da_{i + 4}",
                S=f"irqq_{i % 4}", Z=f"dm_{i}")
for i in range(4):
    cb.add_inst(f"irq_{i}", "DFF_X1", D=f"tile_irq{i}", CK="clk_cfg",
                Q=f"irqq_{i}")
for i in range(4):
    cb.add_inst(f"st_{i}", "BUF_X1", A=f"dm_{2 * i}", Z=f"status{i}")

# ── hybrid_soc 顶层（GX×GY tile 阵列 + ctrl + 三时钟树）──────────────
def build_top(gx, gy):
    top = Module("hybrid_soc",
                 [("clk_core", "input"), ("clk_noc", "input"),
                  ("clk_cfg", "input")]
                 + [(f"a{i}", "input") for i in range(8)]
                 + [("y0", "output"), ("y1", "output")])
    top.add_inst("ccb_root", "CLKBUF_X3", A="clk_core", Z="ncc_root")
    for r in range(gy):
        top.add_inst(f"ccb_r_{r}", "CLKBUF_X2", A="ncc_root",
                     Z=f"ncc_r_{r}")
    top.add_inst("ncb_root", "CLKBUF_X3", A="clk_noc", Z="ncn_root")
    for r in range(gy):
        top.add_inst(f"ncb_r_{r}", "CLKBUF_X2", A="ncn_root",
                     Z=f"ncn_r_{r}")
    top.add_inst("kcb_root", "CLKBUF_X3", A="clk_cfg", Z="nck_root")
    for k in range(4):
        top.add_inst(f"kcb_m_{k}", "CLKBUF_X2", A="nck_root",
                     Z=f"nck_m_{k}")
    # 数据总线扇出缓冲（4 列组）
    for k in range(4):
        for i in range(8):
            top.add_inst(f"a_b_{k}_{i}", "BUF_X2", A=f"a{i}",
                         Z=f"na_{k}_{i}")
    for r in range(gy):
        for c in range(gx):
            conns = {"clk_core": f"ncc_r_{r}", "clk_noc": f"ncn_r_{r}"}
            for i in range(8):
                conns[f"a{i}"] = f"na_{c * 4 // gx}_{i}"
            # 东西链路逐列对接；行尾/边界悬空（真实悬空形态）
            for d, din in (("e", "w"), ("s", "n")):
                for i in range(2):
                    conns[f"noc_in_{din}{i}"] = \
                        f"n_{d}_{r}_{c}_{i}"
            for d in ("n", "w"):
                for i in range(2):
                    conns[f"noc_in_{d}{i}"] = f"n_in_{d}_{r}_{c}_{i}"
            for d in DIRS5[:4]:
                for i in range(2):
                    top.nets.add(f"n_{d}_{r}_{c}_{i}")
                    top.nets.add(f"n_in_{d}_{r}_{c}_{i}")
            top.add_inst(f"u_tile_{r}_{c}", "compute_tile", **conns)
    # 顶层输出：首个 tile 的 y + ctrl 链路
    top.add_inst("y_b0", "BUF_X2", A="pe_out_y0", Z="y0")
    top.add_inst("y_b1", "BUF_X2", A="pe_out_y1", Z="y1")
    conns = {"clk_cfg": "nck_m_0"}
    for i in range(4):
        conns[f"cmd{i}"] = f"n_in_w_0_{min(1, gx - 1)}_{i}"
    for i in range(4):
        # 末行 tile 的 s 向链路（r 循环已出作用域——取 gy-1 显式表达，
        # 终审 #6；gy == 1 时无 s 向链路，接独立悬空网）
        conns[f"tile_irq{i}"] = (f"n_in_s_{gy - 1}_0_{i}" if gy > 1
                                 else "n_irq0")
    for i in range(4):
        conns[f"status{i}"] = f"n_status_{i}"
    top.add_inst("u_ctrl", "ctrl_block", **conns)
    # pe_out_*：仅接输出缓冲输入的无驱动网（tile y0/y1 引脚悬空形态
    # ——真实设计的悬空网语义，终审 #7 注释如实化）
    top.nets.update({"pe_out_y0", "pe_out_y1"})
    return top

# —— 防呆（强制，写盘前）：标准单元 ⊆ Nangate45（block cells 豁免）——
all_refs = set()
for mod in (pe, rt, ct, cb):
    all_refs |= mod.cell_refs
check_cells_available(LIBERTY, all_refs, "set_c", whitelist=BLOCK_CELLS)
for mod in (pe, rt, ct, cb):
    connect_ports(mod)

OUT_DIR.mkdir(exist_ok=True)
block_files = {
    "pe_core": pe, "tile_router": rt, "ctrl_block": cb, "compute_tile": ct,
}
for name, mod in block_files.items():
    (OUT_DIR / f"{name}.v").write_text(
        mod.to_verilog(f"{name} C 套块定义（{mod.inst_count} 实例）——"
                       f"gen_set_c.py 生成"))
# 块 SDC（块端口时钟与顶层同名同周期——跨文件合并零冲突口径）
(OUT_DIR / "pe_core.sdc").write_text(
    "create_clock -name clk_core -period 1.000 [get_ports clk_core]\n"
    "set_input_delay 0.050 -clock clk_core [get_ports a0]\n"
    "set_output_delay 0.050 -clock clk_core [get_ports y0]\n")
(OUT_DIR / "tile_router.sdc").write_text(
    "create_clock -name clk_noc -period 1.600 [get_ports clk_noc]\n"
    "set_input_delay 0.050 -clock clk_noc [get_ports in_n0]\n"
    "set_output_delay 0.050 -clock clk_noc [get_ports out_n0]\n")
(OUT_DIR / "ctrl_block.sdc").write_text(
    "create_clock -name clk_cfg -period 4.000 [get_ports clk_cfg]\n"
    "set_input_delay 0.050 -clock clk_cfg [get_ports cmd0]\n"
    "set_output_delay 0.050 -clock clk_cfg [get_ports status0]\n")

# ── 块定义 DEF（局部网格摆放）───────────────────────────────────────
def block_def(mod, cols=8):
    placements = {}
    for k, (iname, _, _) in enumerate(mod.insts):
        placements[iname] = (1000 + (k % cols) * GRID_X,
                             2000 + (k // cols) * GRID_Y, "N")
    ports_xy = {pin: (500 + pi * GRID_X, 0)
                for pi, (pin, _) in enumerate(mod.ports)}
    die = (0, 0, 1000 + cols * GRID_X,
           2000 + (len(mod.insts) // cols + 1) * GRID_Y)
    return mod.to_def(die, placements, ports_xy)

(OUT_DIR / "pe_core.def").write_text(block_def(pe))
(OUT_DIR / "tile_router.def").write_text(block_def(rt))
(OUT_DIR / "ctrl_block.def").write_text(block_def(cb))
(OUT_DIR / "compute_tile.def").write_text(block_def(ct))

# ── 顶层（全量 40×26 + mini 2×2）────────────────────────────────────
def top_def(top, gx, gy):
    """顶层 DEF：tile 阵列 placed（每 tile 占 40×18 网格）+ 顶层单元。"""
    TILE_W, TILE_H = 40 * GRID_X, 18 * GRID_Y
    placements = {}
    for r in range(gy):
        for c in range(gx):
            placements[f"u_tile_{r}_{c}"] = (2000 + c * TILE_W,
                                             2000 + r * TILE_H, "N")
    for k, (iname, _, _) in enumerate(top.insts):
        if iname not in placements:
            placements[iname] = (2000 + k * GRID_X,
                                 2000 + gy * TILE_H + (k // 40) * GRID_Y,
                                 "N")
    ports_xy = {"clk_core": (1000, 1000), "clk_noc": (3000, 1000),
                "clk_cfg": (5000, 1000)}
    for i in range(8):
        ports_xy[f"a{i}"] = (7000 + i * GRID_X, 1000)
    ports_xy["y0"] = (1000, 2000)
    ports_xy["y1"] = (3000, 2000)
    die = (0, 0, 2000 + gx * TILE_W, 2000 + (gy + 3) * TILE_H)
    return top.to_def(die, placements, ports_xy)


top_sdc = (
    "create_clock -name clk_core -period 1.000 [get_ports clk_core]\n"
    "create_clock -name clk_noc -period 1.600 [get_ports clk_noc]\n"
    "create_clock -name clk_cfg -period 4.000 [get_ports clk_cfg]\n"
    "set_input_delay 0.050 -clock clk_core [get_ports a0]\n"
    "set_output_delay 0.050 -clock clk_core [get_ports y0]\n")
(OUT_DIR / "hybrid_soc.sdc").write_text(top_sdc)

stats = {"set": "c", "top": "hybrid_soc",
         "block_defs": {n: m.inst_count for n, m in block_files.items()},
         "clocks": ["clk_core", "clk_noc", "clk_cfg"],
         "cell_refs": sorted(all_refs),
         "params": {"FLIT": 2}}
# 展开口径总实例（design db 层级展开后的全局实例数）：
#   compute_tile 展开数 = 胶合 7 + 3 块实例 + 2×pe_core 展开 + router 展开
CT_EXPANDED = 7 + 3 + 2 * pe.inst_count + rt.inst_count
variants = {}
for tag, (gx, gy) in (("full", FULL_GRID), ("mini", MINI_GRID)):
    top = build_top(gx, gy)
    total_leaf = CT_EXPANDED * gx * gy + cb.inst_count
    suffix = "" if tag == "full" else "_mini"
    (OUT_DIR / f"hybrid_soc{suffix}.v").write_text(
        top.to_verilog(f"hybrid_soc C 套顶层[{tag}]：{gx}x{gy} tile "
                       f"（{top.inst_count} 实例）——gen_set_c.py 生成"))
    if tag == "full":
        (OUT_DIR / "hybrid_soc.def").write_text(top_def(top, gx, gy))
    else:
        (OUT_DIR / "hybrid_soc_mini.def").write_text(top_def(top, gx, gy))
    stats[f"instances_top_{tag}"] = top.inst_count
    stats[f"instances_total_{tag}"] = top.inst_count + total_leaf
    stats[f"nets_top_{tag}"] = len(top.nets)
    variants[tag] = top
    print(f"set_c[{tag}]: top_instances={top.inst_count} "
          f"total={top.inst_count + total_leaf} (tile "
          f"{ct.inst_count}x{gx * gy}) nets={len(top.nets)}")
stats["params"].update({"GX": FULL_GRID[0], "GY": FULL_GRID[1],
                        "GX_MINI": MINI_GRID[0], "GY_MINI": MINI_GRID[1]})
(OUT_DIR / "stats.json").write_text(json.dumps(stats, indent=1) + "\n")
