#!/usr/bin/env python3
"""A 套生成器：arith_chain 算术链（扁平单层，目标 ~5k 实例）。

拓扑（2026-09-15 用户需求「算术链：加法器/比较器组合逻辑链，多级流水」）：
  W 通道 × L 级流水，每级每通道一条完整算术级
  （DFF → FA → XOR2 → AND2 → OR2 → INV，全 Nangate45 实际单元）；
  末级每通道输出经 OR 归约树 + clk_aux 域输出寄存器到顶层 q——
  双时钟域（clk_main 1.0ns 数据链 / clk_aux 2.5ns 旁路与输出寄存器）。
  时钟缓冲树网表内显式（CLKBUF_X3/X2/X1，与 pg_grid 同构选型）。

产物（cwd = 本目录）：set_a/arith_chain.v + set_a/arith_chain.sdc +
set_a/stats.json。物理实现（floorplan+place+CTS+route）由 OpenROAD
（set_a_flow.tcl）产出；本脚本只产逻辑输入件。

用法：python3 gen_set_a.py   （固定参数，纯算术确定性）
"""

import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from gen_common import Module, check_cells_available  # noqa: E402

# —— 结构参数（固定；改动即新数据版本，须同步 README/QA 锚点）——
W = 36      # 位宽通道数
L = 22      # 流水级数
AUX = 64    # clk_aux 域旁路寄存器链长
OUT_DIR = Path(__file__).resolve().parent / "set_a"
LIBERTY = Path(__file__).resolve().parent.parent / "timing" / \
    "NangateOpenCellLibrary_typical.lib"

m = Module("arith_chain", [
    ("clk_main", "input"), ("clk_aux", "input"),
    ("d", "input"), ("q", "output"),
])

# —— 时钟缓冲树（clk_main：根→8 中间→每级一行缓冲；clk_aux：根→4 中间）——
m.add_inst("cb_root", "CLKBUF_X3", A="clk_main", Z="nclk_root")
for k in range(8):
    m.add_inst(f"cb_m_{k}", "CLKBUF_X2", A="nclk_root", Z=f"nclk_m_{k}")
for l in range(L):
    m.add_inst(f"cb_l_{l}", "CLKBUF_X1",
               A=f"nclk_m_{l * 8 // L}", Z=f"nclk_{l}")
m.add_inst("cb_aux_root", "CLKBUF_X3", A="clk_aux", Z="naux_root")
for k in range(4):
    m.add_inst(f"cb_aux_m_{k}", "CLKBUF_X2", A="naux_root", Z=f"naux_m_{k}")

# —— 输入 d 扇出缓冲（4 分组，供 FA 的 B/CI 操作数）——
for k in range(4):
    m.add_inst(f"d_b_{k}", "BUF_X1", A="d", Z=f"d_{k}")

# —— W×L 算术级主链（每级每通道 6 单元）——
# 操作数取本地/邻近通道网（扇出 ≤3）：大扇出会使 RePlAce 布局发散
# （GPL-0305，pg_grid 同款教训）——输入 d 仅经 4 缓冲供给首级
for w in range(W):
    for l in range(L):
        acc_in = f"acc_{l - 1}_{w}" if l > 0 else f"d_{w * 4 // W}"
        op_b = f"acc_{l - 1}_{(w + 1) % W}" if l > 0 else f"d_{w * 4 // W}"
        m.add_inst(f"dff_{l}_{w}", "DFF_X1",
                   D=acc_in, CK=f"nclk_{l}", Q=f"q_{l}_{w}")
        m.add_inst(f"fa_{l}_{w}", "FA_X1",
                   A=f"q_{l}_{w}", B=op_b, CI=acc_in,
                   S=f"s_{l}_{w}", CO=f"co_{l}_{w}")
        m.add_inst(f"xor_{l}_{w}", "XOR2_X1",
                   A=f"s_{l}_{w}", B=f"q_{l}_{w}", Z=f"x_{l}_{w}")
        m.add_inst(f"and_{l}_{w}", "AND2_X1",
                   A=f"x_{l}_{w}", B=f"s_{l}_{w}", Z=f"y_{l}_{w}")
        m.add_inst(f"or_{l}_{w}", "OR2_X1",
                   A=f"y_{l}_{w}", B=f"co_{l}_{w}", Z=f"z_{l}_{w}")
        m.add_inst(f"inv_{l}_{w}", "INV_X1",
                   A=f"z_{l}_{w}", Z=f"acc_{l}_{w}")

# —— 末级全通道 OR 归约链（比较器语义：36 通道末级归约 → 1）+
#    clk_aux 域输出寄存器 ——
prev = f"z_{L - 1}_0"
for t in range(1, W):
    m.add_inst(f"red_{t}", "OR2_X1", A=prev, B=f"z_{L - 1}_{t}",
               Z=f"red_{t}")
    prev = f"red_{t}"
m.add_inst("q_reg", "DFF_X1", D=f"red_{W - 1}", CK="naux_m_0",
           Q="q_out")
m.add_inst("q_b", "BUF_X1", A="q_out", Z="q")

# —— 旁路寄存器链（clk_aux 域，AUX 级）——
m.add_inst("aux_0", "DFF_X1", D=f"red_{W - 1}", CK="naux_m_0",
           Q="aux_q_0")
for k in range(1, AUX):
    m.add_inst(f"aux_{k}", "DFF_X1", D=f"aux_q_{k - 1}", CK="naux_m_0",
               Q=f"aux_q_{k}")

# —— Liberty 防呆（强制，写盘前）：引用 cell 全集 ⊆ Nangate45 ——
check_cells_available(LIBERTY, m.cell_refs, "set_a")

OUT_DIR.mkdir(exist_ok=True)
(OUT_DIR / "arith_chain.v").write_text(
    m.to_verilog(f"arith_chain A 套：{W}x{L} 算术链 + aux 域 "
                 f"（{m.inst_count} 实例）——gen_set_a.py 生成"))
(OUT_DIR / "arith_chain.sdc").write_text(
    "create_clock -name clk_main -period 1.000 [get_ports clk_main]\n"
    "create_clock -name clk_aux -period 2.500 [get_ports clk_aux]\n"
    "set_input_delay 0.050 -rise -clock clk_main [get_ports d]\n"
    "set_input_delay 0.050 -fall -clock clk_main [get_ports d]\n"
    "set_output_delay 0.050 -clock clk_aux [get_ports q]\n")
stats = {
    "set": "a", "top": "arith_chain", "instances": m.inst_count,
    "nets": len(m.nets), "clocks": ["clk_main", "clk_aux"],
    "params": {"W": W, "L": L, "AUX": AUX},
    "cell_refs": sorted(m.cell_refs),
}
(OUT_DIR / "stats.json").write_text(json.dumps(stats, indent=1) + "\n")
print(f"set_a: instances={m.inst_count} nets={len(m.nets)} "
      f"cells={len(m.cell_refs)} (guard passed vs "
      f"{LIBERTY.name})")
