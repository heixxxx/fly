#!/usr/bin/env python3
"""pg_grid 确定性放大设计生成器（网表 + SDC）。

tm_design 的放大版（途径二数据链）：N×N 单元阵列，每格 DFF+INV+NAND
三单元、行内链式（每级两条路径：DFF→INV→NAND→DFF 与 DFF→NAND→DFF，
与 tm_design 同构）。行尾悬空（NO_TIMING_WINDOW/DANGLING 真实覆盖），
仅第 0 行末级接输出 q。物理实现（含电源网格）由 OpenROAD 流程产出
（pg_grid_flow.tcl），本脚本只产逻辑输入件。

用法：python3 gen_pg_grid.py [N]   （N 缺省 64 → 12288 实例）
产物：pg_grid.v + pg_grid.sdc（确定性：纯算术生成，无随机）
"""

import sys
from pathlib import Path

N = int(sys.argv[1]) if len(sys.argv) > 1 else 64
out_dir = Path(__file__).resolve().parent

verilog = []
nets = {"clk", "d", "q"}
inst_count = 3 * N * N
verilog.append(f"// pg_grid：确定性放大设计（N={N}，{3 * N * N} 单元 + 缓冲树）——gen_pg_grid.py 生成")
verilog.append("module pg_grid (clk, d, q);")
verilog.append("  input  clk;")
verilog.append("  input  d;")
verilog.append("  output q;")

# 时钟缓冲树（根/中层/行缓冲三级级联，两段扇出：根→8 中间、中间→行）——
# 未缓冲的 4096 扇出
# 会使 RePlAce 布局发散（GPL-0305 实测），真实流程综合后亦必有缓冲树。
# 单元选型：Nangate45 仅有 CLKBUF_X1/X2/X3——X4/X16 是其它平台的单元
# （2026-09-15 实测：引用后 OpenROAD link 建 black box、LEF master 缺失
# 丢弃实例，时钟根网零负载致 TritonCTS 空转不插树，时钟链全断）
M = 8
verilog.append("  CLKBUF_X3 cb_root (.A(clk), .Z(nclk_root));")
nets.add("nclk_root")
inst_count += 1
for m in range(M):
    verilog.append(f"  CLKBUF_X3  cb_m_{m} (.A(nclk_root), .Z(nclk_m_{m}));")
    nets.add(f"nclk_m_{m}")
    inst_count += 1
for r in range(N):
    verilog.append(
        f"  CLKBUF_X2  cb_r_{r} (.A(nclk_m_{r * M // N}), .Z(nclk_r_{r}));"
    )
    nets.add(f"nclk_r_{r}")
    inst_count += 1

# 输入 d 扇出缓冲（每缓冲驱动 16 行的行首 NAND）
D_BUF = 4
for k in range(D_BUF):
    verilog.append(f"  BUF_X1     d_b_{k} (.A(d), .Z(d_{k}));")
    nets.add(f"d_{k}")
    inst_count += 1

for r in range(N):
    for c in range(N):
        prev = f"q_{r}_{c - 1}" if c > 0 else f"d_{r * D_BUF // N}"
        # 行内链：本级 NAND 输出驱动下一级 DFF；末列悬空（第 0 行 = 输出 q）
        out = f"in_{r}_{c + 1}" if c + 1 < N else ("q" if r == 0 else f"tail_{r}")
        verilog.append(
            f"  DFF_X1   dff_{r}_{c}  (.D(in_{r}_{c}), .CK(nclk_r_{r}), .Q(q_{r}_{c}));"
        )
        verilog.append(f"  INV_X1   inv_{r}_{c}  (.A(q_{r}_{c}), .ZN(n1_{r}_{c}));")
        verilog.append(
            f"  NAND2_X1 nand_{r}_{c} (.A1(n1_{r}_{c}), .A2({prev}), .ZN({out}));"
        )
        if c > 0:
            nets.add(f"in_{r}_{c}")  # c=0 的 in_{r}_0 无驱动者，幻影名不计网数
        nets.update({f"q_{r}_{c}", f"n1_{r}_{c}"})
    if r != 0:
        nets.add(f"tail_{r}")
verilog.append("endmodule")

(out_dir / "pg_grid.v").write_text("\n".join(verilog) + "\n")
(out_dir / "pg_grid.sdc").write_text(
    "create_clock -name clk -period 1.000 [get_ports clk]\n"
    "set_input_delay 0.050 -rise -clock clk [get_ports d]\n"
    "set_input_delay 0.050 -fall -clock clk [get_ports d]\n"
    "set_output_delay 0.050 -clock clk [get_ports q]\n"
)
print(f"pg_grid: N={N} instances={inst_count} nets={len(nets)}")
