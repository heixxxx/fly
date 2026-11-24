#!/usr/bin/env python3
# waveform db 测试数据生成器：Verilog 网表 + 物理设计 DEF 同源产出。
#
# 全部 Verilog（rtl/*.v）与 DEF（design/*.def）从同一张网表数据结构生成，
# 保证「模块例化名 = DEF 实例名、线网名 = DEF 网名」逐字一致——这是 VCD
# 信号名可与 design db 匹配的确定性来源（同 timing db 先例 tm_design.v/def）。
#
# 产物布局（cwd = 本脚本所在目录）：
#   rtl/wf_small.v wf_small_a1.v wf_small_a2.v wf_blk_b1.v wf_blk_b2.v
#   rtl/wf_blk_instpin.v wf_xz.v wf_big.v
#   design/wf_small.def wf_small_a1.def wf_small_a2.def wf_blk_top.def
#   design/wf_blk_a.def wf_sub.def wf_blk_b.def wf_xz.def wf_big.def
#
# 确定性：纯文本模板 + 固定遍历序（行主序），重跑逐字节一致。
# 单元模型（cells_sim.v）为手写文件，不由本脚本产出。
# 单元与引脚名取自 Nangate45（qa/emir/data/timing/NangateOpenCellLibrary.macro.lef）。

import os

HERE = os.path.dirname(os.path.abspath(__file__))
RTL_DIR = os.path.join(HERE, "rtl")
DEF_DIR = os.path.join(HERE, "design")

# ── 单元引脚表（名字与 Nangate45 LEF PIN 一致；cell_sim 模型同款端口）──

CELL_PINS = {
    "INV_X1":   ["A", "ZN"],
    "BUF_X1":   ["A", "Z"],
    "CLKBUF_X1": ["A", "Z"],
    "XOR2_X1":  ["A", "B", "Z"],
    "AND2_X1":  ["A1", "A2", "ZN"],
    "DFF_X1":   ["D", "CK", "Q", "QN"],
    "DFFR_X1":  ["D", "CK", "RN", "Q", "QN"],
    "TBUF_X1":  ["A", "EN", "Z"],
}

# 层级块（非 LEF 单元）引脚——DEF 顶层 NETS 引用块实例端口名用
BLOCK_PINS = {
    "wf_sub":   ["clk", "rn", "n_in"],
    "wf_blk_a": ["clk", "rn", "n_out"],
    "wf_blk_b": ["clk", "rn", "n_out"],
}


class Design:
    """一个设计（顶层或块）的网表：实例 + 网连接 + 顶层 PIN + 激励/dump 文本。"""

    def __init__(self, name):
        self.name = name
        self.insts = []          # (cell, inst_name, {pin: net_name})
        self.net_conns = {}      # net_name -> [(inst_name, pin_name) | ("PIN", pin_name)]
        self.pins = []           # (pin_name, direction, use)  direction: INPUT/OUTPUT
        self.regs = []           # Verilog reg 声明（激励信号）
        self.wires = []          # Verilog wire 声明（含 "wire [3:0] count" 整行）
        self.stimulus = []       # Verilog initial/always 文本行
        self.dump = []           # Verilog dump initial 文本行

    def add_inst(self, cell, inst, conns):
        self.insts.append((cell, inst, conns))
        for pin, net in conns.items():
            if net is None:
                continue
            self.net_conns.setdefault(net, []).append((inst, pin))

    def add_pin(self, pin_name, net_name, direction, use="SIGNAL"):
        self.pins.append((pin_name, direction, use))
        self.net_conns.setdefault(net_name, []).append(("PIN", pin_name))

    def net_order(self):
        """确定性网序：net_conns 键序 = 插入序（首连接出现序）。"""
        return list(self.net_conns.keys())


# ── Verilog 生成 ──

def _verilog_insts(d):
    lines = []
    for cell, inst, conns in d.insts:
        pin_map = ", ".join(".%s(%s)" % (p, n) for p, n in conns.items()
                            if n is not None)
        lines.append("  %s %s (%s);" % (cell, inst, pin_map))
    return lines


def verilog_leaf_module(d, ports):
    """块设计（带端口的子模块）Verilog 文本。ports: [(name, dir)]"""
    lines = []
    plist = ", ".join(p for p, _ in ports)
    lines.append("module %s (%s);" % (d.name, plist))
    for p, direction in ports:
        lines.append("  %s %s;" % (direction.lower(), p))
    for w in d.wires:
        lines.append("  %s;" % w)
    lines += _verilog_insts(d)
    lines.append("endmodule")
    return "\n".join(lines)


def verilog_top_sim(d, comment):
    """自足仿真顶层（无端口、激励内嵌）Verilog 文本。

    顶层模块名 = 设计 DESIGN 名 = VCD 根 scope 名；设计模块内的激励
    reg 直接充当设计网（DEF PINS 声明顶层 I/O 边界，名字空间一致）。
    """
    lines = []
    lines.append("// %s —— waveform db 测试设计（gen_design.py 生成，重跑原样再生成，勿手改）" % d.name)
    lines.append("// %s" % comment)
    lines.append("// 网名/实例名与 design/%s.def 逐字一致（名字对齐的确定性来源）。" % d.name)
    lines.append("`timescale 1ns/1ns")
    lines.append("module %s;" % d.name)
    for r in d.regs:
        lines.append("  %s;" % r)
    for w in d.wires:
        lines.append("  %s;" % w)
    lines += _verilog_insts(d)
    lines.append("")
    for s in d.stimulus + d.dump:
        lines.append(s)
    lines.append("endmodule")
    return "\n".join(lines)
    return lines


def verilog_blk_sim(top, blocks, comment):
    """层级 blk 仿真文件：顶层（激励+dump）+ 块定义模块，单文件可独立编译。"""
    lines = [verilog_top_sim(top, comment)]
    for b in blocks:
        lines.append(verilog_leaf_module(b, block_ports(b.name)))
    return "\n".join(lines)


def block_ports(block_name):
    """层级块端口表（名, 方向）——wf_sub/wf_blk_a/wf_blk_b 的固定定义。"""
    table = {
        "wf_sub":   [("clk", "input"), ("rn", "input"), ("n_in", "input")],
        "wf_blk_a": [("clk", "input"), ("rn", "input"), ("n_out", "output")],
        "wf_blk_b": [("clk", "input"), ("rn", "input"), ("n_out", "output")],
    }
    return table[block_name]


# ── DEF 生成 ──

def def_text(d, die, place_origin_x=2000, place_dx=2000, place_y=2000):
    """设计 DEF 文本。die: (x1 y1 x2 y2)。

    实例按生成序沿 x 网格摆放（确定性坐标，design db 不做 DRC，仅求
    合法可解析 + 稳定字节序）。顶层 PIN 沿 die 上沿排列。
    """
    lines = []
    lines.append("VERSION 5.8 ;")
    lines.append('DIVIDERCHAR "/" ;')
    lines.append('BUSBITCHARS "[]" ;')
    lines.append("DESIGN %s ;" % d.name)
    lines.append("UNITS DISTANCE MICRONS 2000 ;")
    lines.append("DIEAREA ( %d %d ) ( %d %d ) ;" % die)
    lines.append("")
    lines.append("COMPONENTS %d ;" % len(d.insts))
    x = place_origin_x
    y = place_y
    for i, (cell, inst, _conns) in enumerate(d.insts):
        lines.append("    - %s %s + PLACED ( %d %d ) N ;" % (inst, cell, x, y))
        x += place_dx
        if x > die[2] - 4000:
            x = place_origin_x
            y += 4000
    lines.append("END COMPONENTS")
    lines.append("")
    lines.append("PINS %d ;" % len(d.pins))
    px = 2000
    py = die[3] - 1000
    for pin_name, direction, use in d.pins:
        lines.append("    - %s + NET %s" % (pin_name, pin_name))
        lines.append("      + DIRECTION %s + USE %s" % (direction, use))
        lines.append("      + LAYER metal1 ( -50 -50 ) ( 50 50 )")
        lines.append("      + FIXED ( %d %d ) N ;" % (px, py))
        px += 4000
        if px > die[2] - 2000:
            px = 2000
            py -= 2000
    lines.append("END PINS")
    lines.append("")
    nets = d.net_order()
    lines.append("NETS %d ;" % len(nets))
    for net in nets:
        terms = []
        for inst, pin in d.net_conns[net]:
            if inst == "PIN":
                terms.append("( PIN %s )" % pin)
            else:
                terms.append("( %s %s )" % (inst, pin))
        lines.append("    - %s %s ;" % (net, " ".join(terms)))
    lines.append("END NETS")
    lines.append("")
    lines.append("END DESIGN")
    return "\n".join(lines)


# ── 各设计构建 ──

def build_small(name, dump_file=None, finish_ns=201, rn_release_ns=23,
                seq_init=None, rn_init=0):
    """wf_small 家族：4bit 行波计数器 + 二分频时钟 + 组合观测。

    name        顶层/DESIGN 名（wf_small / wf_small_a1 / wf_small_a2）
    dump_file   VCD 文件名；None = 不 dump（DEF-only 场景不用，恒有值）
    seq_init    计数器初值（a2 分段续跑用——force DFF 内部 reg 注入初值）
    rn_init     rn 初值（a2 段复位早已释放，恒 1）
    """
    d = Design(name)
    d.regs = ["reg clk", "reg rn"]
    d.wires = ["wire [3:0] count", "wire clk_div2", "wire n_and, n_xor",
               "wire n_cnt0_d, n_cnt1_d, n_cnt2_d, n_cnt3_d",
               "wire n_b1, n_b2, n_div_d"]

    d.add_inst("INV_X1", "u_inv_c0", {"A": "count[0]", "ZN": "n_cnt0_d"})
    d.add_inst("XOR2_X1", "u_xor_c1", {"A": "count[0]", "B": "count[1]", "Z": "n_cnt1_d"})
    d.add_inst("AND2_X1", "u_and_c2", {"A1": "count[0]", "A2": "count[1]", "ZN": "n_b1"})
    d.add_inst("XOR2_X1", "u_xor_c2", {"A": "n_b1", "B": "count[2]", "Z": "n_cnt2_d"})
    d.add_inst("AND2_X1", "u_and_c3a", {"A1": "n_b1", "A2": "count[2]", "ZN": "n_b2"})
    d.add_inst("XOR2_X1", "u_xor_c3", {"A": "n_b2", "B": "count[3]", "Z": "n_cnt3_d"})
    for i in range(4):
        d.add_inst("DFFR_X1", "u_cnt%d" % i,
                   {"D": "n_cnt%d_d" % i, "CK": "clk", "RN": "rn",
                    "Q": "count[%d]" % i, "QN": None})
    d.add_inst("INV_X1", "u_inv_div", {"A": "clk_div2", "ZN": "n_div_d"})
    d.add_inst("DFFR_X1", "u_div", {"D": "n_div_d", "CK": "clk", "RN": "rn",
                                    "Q": "clk_div2", "QN": None})
    d.add_inst("AND2_X1", "u_and1", {"A1": "count[3]", "A2": "count[2]", "ZN": "n_and"})
    d.add_inst("XOR2_X1", "u_xor1", {"A": "count[1]", "B": "count[0]", "Z": "n_xor"})

    d.add_pin("clk", "clk", "INPUT", "CLOCK")
    d.add_pin("rn", "rn", "INPUT")
    for i in range(4):
        d.add_pin("count[%d]" % i, "count[%d]" % i, "OUTPUT")
    d.add_pin("clk_div2", "clk_div2", "OUTPUT")
    d.add_pin("n_and", "n_and", "OUTPUT")
    d.add_pin("n_xor", "n_xor", "OUTPUT")

    init_lines = ["  initial begin",
                  "    clk = 1'b0; rn = 1'b%d;" % rn_init]
    if seq_init is not None:
        # 分段续跑：把各 DFF 内部状态注入为 a1 末值（force 后立即 release，
        # reg 无过程赋值驱动源时保持 force 值，直到下一个时钟沿被 always 覆盖）
        for i in range(4):
            bit = (seq_init >> i) & 1
            init_lines.append("    force u_cnt%d.q_r = 1'b%d;" % (i, bit))
        init_lines.append("    force u_div.q_r = 1'b%d;" % (seq_init & 1))
        for i in range(4):
            init_lines.append("    release u_cnt%d.q_r;" % i)
        init_lines.append("    release u_div.q_r;")
    if rn_release_ns > 0:
        init_lines.append("    #%d rn = 1'b1;" % rn_release_ns)
    init_lines.append("    #%d $finish;" % (finish_ns - rn_release_ns))
    init_lines.append("  end")
    d.stimulus = init_lines
    d.stimulus.append("  always #5 clk = ~clk;")
    d.dump = ['  initial begin',
              '    $dumpfile("%s");' % dump_file,
              '    $dumpvars(0, %s);' % name,
              '  end']
    return d


def build_blk_sub():
    """wf_sub 块定义：DFF + XOR + INV（D = ~(Q ^ n_in)）。

    端口声明（clk/rn/n_in，与 block_ports() 同名同向）——块级 DEF 声明
    对外端口是层次化连接成立的前提：design db 的跨块并查集（net_union）
    只沿 port 相连网归并，无端口则跨层连接全部被丢弃。add_pin 在
    add_inst 之后调用（端口条目追加到网连接表尾部，网键插入序不变
    ——NETS 段网序与既有产物逐字节对齐）。
    """
    d = Design("wf_sub")
    d.wires = ["wire n1", "wire n2", "wire n3"]
    d.add_inst("DFFR_X1", "u_dff_s", {"D": "n2", "CK": "clk", "RN": "rn",
                                      "Q": "n1", "QN": None})
    d.add_inst("XOR2_X1", "u_xor_s", {"A": "n1", "B": "n_in", "Z": "n3"})
    d.add_inst("INV_X1", "u_inv_s", {"A": "n3", "ZN": "n2"})
    d.add_pin("clk", "clk", "INPUT", "CLOCK")
    d.add_pin("rn", "rn", "INPUT")
    d.add_pin("n_in", "n_in", "INPUT")
    return d


def build_blk_a():
    """wf_blk_a 块定义：T-FF + 输出反相 + 子块 wf_sub（端口声明同上）"""
    d = Design("wf_blk_a")
    d.wires = ["wire n_a1", "wire n_a2"]
    d.add_inst("DFFR_X1", "u_dff_a", {"D": "n_a2", "CK": "clk", "RN": "rn",
                                      "Q": "n_a1", "QN": None})
    d.add_inst("INV_X1", "u_inv1", {"A": "n_a1", "ZN": "n_a2"})
    d.add_inst("INV_X1", "u_inv_out", {"A": "n_a1", "ZN": "n_out"})
    d.add_inst("wf_sub", "u_sub", {"clk": "clk", "rn": "rn", "n_in": "n_a1"})
    d.add_pin("clk", "clk", "INPUT", "CLOCK")
    d.add_pin("rn", "rn", "INPUT")
    d.add_pin("n_out", "n_out", "OUTPUT")
    return d


def build_blk_b():
    """wf_blk_b 块定义：T-FF（BUF 反馈）+ 输出反相——与 A 结构刻意不同
    （端口声明同上）"""
    d = Design("wf_blk_b")
    d.wires = ["wire n_b1", "wire n_b2"]
    d.add_inst("DFFR_X1", "u_dff_b1", {"D": "n_b2", "CK": "clk", "RN": "rn",
                                       "Q": "n_b1", "QN": None})
    d.add_inst("BUF_X1", "u_buf_b", {"A": "n_b1", "Z": "n_b2"})
    d.add_inst("INV_X1", "u_inv_b", {"A": "n_b1", "ZN": "n_out"})
    d.add_pin("clk", "clk", "INPUT", "CLOCK")
    d.add_pin("rn", "rn", "INPUT")
    d.add_pin("n_out", "n_out", "OUTPUT")
    return d


def build_blk_top(name, dump_stmts, finish_ns=84):
    """wf_blk_top 顶层 + 块定义。dump_stmts: $dumpvars 文本行（决定 b1/b2/instpin 形态）

    finish_ns 默认 84（b1/instpin）；b2 传 74——b2 与 b1 激励时长刻意不同，
    使重合条目（n_a1）在两文件中统计值不同，覆盖语义（后文件替换）可观测。
    """
    top = Design(name)
    top.regs = ["reg clk", "reg rn"]
    top.wires = ["wire n_top", "wire n_top2"]
    top.add_inst("wf_blk_a", "u_blk_a", {"clk": "clk", "rn": "rn", "n_out": "n_top"})
    top.add_inst("wf_blk_b", "u_blk_b", {"clk": "clk", "rn": "rn", "n_out": "n_top2"})
    top.add_pin("clk", "clk", "INPUT", "CLOCK")
    top.add_pin("rn", "rn", "INPUT")
    top.add_pin("n_top", "n_top", "OUTPUT")
    top.add_pin("n_top2", "n_top2", "OUTPUT")
    top.stimulus = ["  initial begin",
                    "    clk = 1'b0; rn = 1'b0;",
                    "    #23 rn = 1'b1;",
                    "    #%d $finish;" % (finish_ns - 23),   # 避开 5 的倍数时钟沿
                    "  end",
                    "  always #5 clk = ~clk;"]
    top.dump = ['  initial begin'] + dump_stmts + ['  end']

    blk_a = build_blk_a()
    sub = build_blk_sub()
    blk_b = build_blk_b()
    return top, blk_a, sub, blk_b


def build_xz():
    """wf_xz：未初始化 DFF（x 段）+ 三态网（z 段）+ force x/1/z 构造"""
    d = Design("wf_xz")
    d.regs = ["reg clk", "reg en", "reg n_seed"]
    d.wires = ["wire n_q", "wire n_inv", "wire n_f", "wire n_tri"]
    d.add_inst("DFF_X1", "u_dff", {"D": "n_seed", "CK": "clk", "Q": "n_q", "QN": None})
    d.add_inst("INV_X1", "u_inv1", {"A": "n_q", "ZN": "n_inv"})
    d.add_inst("BUF_X1", "u_buf", {"A": "n_inv", "Z": "n_f"})
    d.add_inst("TBUF_X1", "u_tri", {"A": "n_inv", "EN": "en", "Z": "n_tri"})
    d.add_pin("clk", "clk", "INPUT", "CLOCK")
    d.add_pin("n_q", "n_q", "OUTPUT")
    d.add_pin("n_tri", "n_tri", "OUTPUT")
    d.add_pin("n_f", "n_f", "OUTPUT")
    d.stimulus = ["  initial begin",
                  "    clk = 1'b0; en = 1'b0;",   # n_seed 不赋初值——保持 x
                  "    #8  n_seed = 1'b0;",       # t=8
                  "    #12 n_seed = 1'b1;",       # t=20
                  "    #10 en = 1'b1;",           # t=30
                  "    #10 n_seed = 1'b0;",       # t=40
                  "    #20 en = 1'b0;",           # t=60
                  "    #10 n_seed = 1'b1;",       # t=70
                  "    #31 $finish;",             # t=101（避开 t=100 沿）
                  "  end",
                  "  always #5 clk = ~clk;",
                  # force 构造：x→1→z→release（全部经 force 注入 n_f）
                  "  initial begin",
                  "    #18 force n_f = 1'bx;",    # t=18
                  "    #20 force n_f = 1'b1;",    # t=38
                  "    #20 force n_f = 1'bz;",    # t=58
                  "    #20 release n_f;",         # t=78
                  "  end"]
    d.dump = ['  initial begin',
              '    $dumpfile("xz.vcd");',
              '    $dumpvars(0, wf_xz);',
              '  end']
    return d


def build_big(rows=100, cols=100):
    """wf_big：rows×cols 个 T-FF 单元（DFFR_X1+INV_X1），万级实例规模。

    规模与仿真时长取「≥1 万实例 + VCD/DEF 双双 <5MB 入库红线」的平衡点：
    100×100=20000 实例、3 个有效沿（每 Q 翻 3 次）。
    """
    d = Design("wf_big")
    d.regs = ["reg clk", "reg rn"]
    d.wires = []
    for i in range(rows):
        for j in range(cols):
            nq = "n_q_%d_%d" % (i, j)
            nd = "n_d_%d_%d" % (i, j)
            d.add_inst("DFFR_X1", "u_dff_%d_%d" % (i, j),
                       {"D": nd, "CK": "clk", "RN": "rn", "Q": nq, "QN": None})
            d.add_inst("INV_X1", "u_inv_%d_%d" % (i, j), {"A": nq, "ZN": nd})
    d.add_pin("clk", "clk", "INPUT", "CLOCK")
    d.add_pin("rn", "rn", "INPUT")
    d.stimulus = ["  initial begin",
                  "    clk = 1'b0; rn = 1'b0;",
                  "    #3 rn = 1'b1;",
                  "    #33 $finish;",   # t=36（有效沿 5/15/25 共 3 个）
                  "  end",
                  "  always #5 clk = ~clk;"]
    d.dump = ['  initial begin',
              '    $dumpfile("big.vcd");',
              '    $dumpvars(0, wf_big);',
              '  end']
    return d


def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        f.write(text + "\n")
    print("  write %s (%d bytes)" % (os.path.relpath(path, HERE), len(text) + 1))


def main():
    print("waveform db 测试设计生成（Verilog + DEF 同源）")

    # ── wf_small 家族：完整仿真 / 分段 a1 / 分段 a2（续跑初值注入）──
    small = build_small("wf_small", "small.vcd", finish_ns=201)
    write(os.path.join(RTL_DIR, "wf_small.v"), verilog_top_sim(
        small, "完整仿真 [0,201]ns；rn@23 释放，有效计数沿 25..195 共 18 个"))
    write(os.path.join(DEF_DIR, "wf_small.def"), def_text(small, (0, 0, 40000, 20000)))

    a1 = build_small("wf_small_a1", "part_a1.vcd", finish_ns=101)
    write(os.path.join(RTL_DIR, "wf_small_a1.v"), verilog_top_sim(
        a1, "分段 a1：全局 [0,100]ns（rn@23 释放，有效沿 25..95 共 8 个）"))
    write(os.path.join(DEF_DIR, "wf_small_a1.def"), def_text(a1, (0, 0, 40000, 20000)))

    a2 = build_small("wf_small_a2", "part_a2.vcd", finish_ns=101,
                     seq_init=0b1000, rn_init=1, rn_release_ns=0)
    write(os.path.join(RTL_DIR, "wf_small_a2.v"), verilog_top_sim(
        a2, "分段 a2：全局 [100,200]ns 平移到 0 起点（count 初值 8=a1 末值，rn 恒 1，"
            "有效沿 5..95 共 10 个；时间戳从 0 重新开始——场景 a 偏移对齐测试点）"))
    write(os.path.join(DEF_DIR, "wf_small_a2.def"), def_text(a2, (0, 0, 40000, 20000)))

    # ── blk 家族：b1 一层 dump / b2 子块 dump + 重合构造 / instpin 实例一层 ──
    b1 = build_blk_top("wf_blk_top", ['    $dumpfile("block_b1.vcd");',
                                      '    $dumpvars(1, u_blk_a);'])
    write(os.path.join(RTL_DIR, "wf_blk_b1.v"), verilog_blk_sim(
        b1[0], b1[1:], comment="场景 b 文件 1：$dumpvars(1, u_blk_a)——块 A 一层局部名"))
    write(os.path.join(DEF_DIR, "wf_blk_top.def"), def_text(b1[0], (0, 0, 40000, 20000)))
    write(os.path.join(DEF_DIR, "wf_blk_a.def"), def_text(b1[1], (0, 0, 20000, 12000)))
    write(os.path.join(DEF_DIR, "wf_sub.def"), def_text(b1[2], (0, 0, 12000, 8000)))
    write(os.path.join(DEF_DIR, "wf_blk_b.def"), def_text(b1[3], (0, 0, 16000, 8000)))

    b2 = build_blk_top("wf_blk_top", ['    $dumpfile("block_b2.vcd");',
                                      '    $dumpvars(0, u_blk_a.u_sub);',
                                      '    $dumpvars(1, u_blk_a.n_a1);'],
                       finish_ns=74)
    write(os.path.join(RTL_DIR, "wf_blk_b2.v"), verilog_blk_sim(
        b2[0], b2[1:], comment="场景 b 文件 2：子块 u_sub 全 dump + 刻意单条 u_blk_a/n_a1"
                               "（与 b1 同实体重合——后文件覆盖语义测试点）"))

    ip = build_blk_top("wf_blk_top", ['    $dumpfile("instpin.vcd");',
                                      '    $dumpvars(1, u_blk_a.u_inv1);'])
    write(os.path.join(RTL_DIR, "wf_blk_instpin.v"), verilog_blk_sim(
        ip[0], ip[1:], comment="形态②直配：$dumpvars(1, u_blk_a.u_inv1)——var 名=cell 端口名"
                               "（A/ZN=LEF pin 名），scope=实例层级路径；net 名未命中路径"))

    # ── xz：x 段 + 三态 z 段 + force 构造 ──
    xz = build_xz()
    write(os.path.join(RTL_DIR, "wf_xz.v"), verilog_top_sim(
        xz, "x/z 统计载体：无复位 DFF 初始 x、TBUF 三态 z、n_f 经 force x/1/z+release"))
    write(os.path.join(DEF_DIR, "wf_xz.def"), def_text(xz, (0, 0, 16000, 8000)))

    # ── big：100×100 单元 = 20000 实例 ──
    big = build_big()
    write(os.path.join(RTL_DIR, "wf_big.v"), verilog_top_sim(
        big, "大规模：100x100 个 T-FF 单元（DFFR_X1+INV_X1）= 20000 实例"))
    write(os.path.join(DEF_DIR, "wf_big.def"),
          def_text(big, (0, 0, 100 * 2000 + 4000, 110 * 4000 + 4000)))

    print("done.")


if __name__ == "__main__":
    main()
