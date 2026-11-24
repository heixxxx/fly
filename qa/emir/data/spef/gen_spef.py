#!/usr/bin/env python3
"""spef db 端到端测试数据生成器（确定性——同输入逐字节一致）。

对齐既有设计 qa/emir/data/waveform/design/wf_small.def（14 个实例、16 个
网）手写规范样张。数值与推导表见同目录 README.md。

生成（在仓库根执行）：
    python3 qa/emir/data/spef/gen_spef.py [输出目录，缺省为脚本所在目录]

只依赖标准库。单元引脚表内嵌自 wf_small.def 的 COMPONENTS / NETS 段。
"""
import os
import sys

# ── wf_small 设计的实例与单元（DEF COMPONENTS 段）────────────────────
CELLS = {
    "u_inv_c0": "INV_X1", "u_xor_c1": "XOR2_X1", "u_and_c2": "AND2_X1",
    "u_xor_c2": "XOR2_X1", "u_and_c3a": "AND2_X1", "u_xor_c3": "XOR2_X1",
    "u_cnt0": "DFFR_X1", "u_cnt1": "DFFR_X1", "u_cnt2": "DFFR_X1",
    "u_cnt3": "DFFR_X1", "u_inv_div": "INV_X1", "u_div": "DFFR_X1",
    "u_and1": "AND2_X1", "u_xor1": "XOR2_X1",
}

# 输出引脚（其余引脚按输入处理）
OUTPUT_PINS = {"ZN", "Z", "Q"}

# ── wf_small 的网与连接（DEF NETS 段，顺序一致）──────────────────────
# 每项 = (网名, [(实例, 引脚), ...], 端口方向或 None)
NETS = [
    ("count[0]", [("u_cnt0", "Q"), ("u_inv_c0", "A"), ("u_xor_c1", "A"),
                  ("u_and_c2", "A1"), ("u_xor1", "B")], "I"),
    ("n_cnt0_d", [("u_inv_c0", "ZN"), ("u_cnt0", "D")], None),
    ("count[1]", [("u_cnt1", "Q"), ("u_xor_c1", "B"), ("u_and_c2", "A2"),
                  ("u_xor1", "A")], "I"),
    ("n_cnt1_d", [("u_xor_c1", "Z"), ("u_cnt1", "D")], None),
    ("n_b1", [("u_and_c2", "ZN"), ("u_xor_c2", "A"), ("u_and_c3a", "A1")],
     None),
    ("count[2]", [("u_cnt2", "Q"), ("u_xor_c2", "B"), ("u_and_c3a", "A2"),
                  ("u_and1", "A2")], "I"),
    ("n_cnt2_d", [("u_xor_c2", "Z"), ("u_cnt2", "D")], None),
    ("n_b2", [("u_and_c3a", "ZN"), ("u_xor_c3", "A")], None),
    ("count[3]", [("u_xor_c3", "B"), ("u_cnt3", "Q"), ("u_and1", "A1")], "I"),
    ("n_cnt3_d", [("u_xor_c3", "Z"), ("u_cnt3", "D")], None),
    ("clk", [("u_cnt0", "CK"), ("u_cnt1", "CK"), ("u_cnt2", "CK"),
             ("u_cnt3", "CK"), ("u_div", "CK")], "I"),
    ("rn", [("u_cnt0", "RN"), ("u_cnt1", "RN"), ("u_cnt2", "RN"),
            ("u_cnt3", "RN"), ("u_div", "RN")], "I"),
    ("clk_div2", [("u_inv_div", "A"), ("u_div", "Q")], "O"),
    ("n_div_d", [("u_inv_div", "ZN"), ("u_div", "D")], None),
    ("n_and", [("u_and1", "ZN")], "O"),
    ("n_xor", [("u_xor1", "Z")], "O"),
]
NET_BY_NAME = {n: (c, p) for n, c, p in NETS}


def node(inst_pin):
    return "%s:%s" % inst_pin


def conn_lines(net, with_cell=True):
    conns, port = NET_BY_NAME[net]
    out = ["*CONN"]
    for inst, pin in conns:
        d = "O" if pin in OUTPUT_PINS else "I"
        if with_cell:
            out.append("*I %s %s *D %s" % (node((inst, pin)), d, CELLS[inst]))
        else:
            out.append("*I %s %s" % (node((inst, pin)), d))
    if port is not None:
        out.append("*P %s %s" % (net, port))
    return out


def dnet(net, total, grounds, couplings, res=None):
    """分布式网段：对地记录若干条 + 耦合记录若干条（+ 可选电阻一条）。"""
    conns = NET_BY_NAME[net][0]
    n1 = node(conns[0])
    n2 = "%s:14" % net
    lines = ["*D_NET %s %s" % (net, fmt(total))]
    lines += conn_lines(net)
    if grounds or couplings or res is not None:
        lines.append("*CAP")
        idx = 1
        for nd, v in grounds:
            lines.append("%d %s %s" % (idx, nd, fmt(v)))
            idx += 1
        for a, b, v in couplings:
            lines.append("%d %s %s %s" % (idx, a, b, fmt(v)))
            idx += 1
    if res is not None:
        lines.append("*RES")
        lines.append("1 %s %s %s" % (n1, n2, fmt(res)))
    lines.append("*END")
    return lines


def fmt(v):
    """数值写法：整数不带小数点，其余原样。"""
    if isinstance(v, float) and v == int(v):
        return str(int(v))
    return str(v)


HEADER_FF = [
    '*SPEF "IEEE 1481-1998"',
    '*DESIGN "wf_small"',
    '*DATE "Tue Nov 24 00:00:00 2026"',
    '*VENDOR "fly QA"',
    '*PROGRAM "gen_spef.py"',
    '*VERSION "1.0"',
    '*DESIGN_FLOW "PIN_CAP NONE"',
    "*DIVIDER /",
    "*DELIMITER :",
    "*BUS_DELIMITER [ ]",
    "*T_UNIT 1 NS",
    "*C_UNIT 1 FF",
    "*R_UNIT 1 OHM",
    "*L_UNIT 1 HENRY",
    "",
]


# ── 样张 1：sp_small.spef（基础——对齐 wf_small 的 16 个网）────────────
# (网名, 行首总电容, 对地两条, 耦合一条, 电阻, 耦合对端引脚)
SMALL_TABLE = [
    ("count[0]", 3, (1, 1), 1, 0.3, ("u_inv_c0", "ZN")),
    ("n_cnt0_d", 6, (1, 3), 2, 0.6, ("u_cnt0", "Q")),
    ("count[1]", 9, (2, 4), 3, 0.9, ("u_xor_c1", "Z")),
    ("n_cnt1_d", 12, (3, 5), 4, 1.2, ("u_cnt1", "Q")),
    ("n_b1", 15, (4, 6), 5, 1.5, ("u_xor_c2", "Z")),
    ("count[2]", 18, (5, 7), 6, 1.8, ("u_xor_c2", "Z")),
    ("n_cnt2_d", 21, (6, 8), 7, 2.1, ("u_cnt2", "Q")),
    ("n_b2", 24, (7, 9), 8, 2.4, ("u_xor_c3", "Z")),
    ("count[3]", 27, (8, 10), 9, 2.7, ("u_xor_c3", "Z")),
    ("n_cnt3_d", 30, (9, 11), 10, 3.0, ("u_cnt3", "Q")),
    ("clk", 33, (10, 12), 11, 3.3, ("u_cnt0", "RN")),
]


def gen_small():
    out = list(HEADER_FF)
    out += [
        "// 基础样张：对齐 wf_small（16 个网里写 15 个，n_xor 不写）。",
        "// 单位 1 FF——数值即飞法，期望值可精确断言。",
        "// rn 是缩减网；clk_div2 / n_div_d 全零；n_and 行首值比明细合计大 1",
        "// （行首 46，对地 30 + 耦合 15 = 45）；ghost_net 是设计里没有的网。",
        "",
    ]
    for name, total, (g1, g2), c, r, partner in SMALL_TABLE:
        n1 = node(NET_BY_NAME[name][0][0])
        out += dnet(name, total, [(n1, g1), ("%s:14" % name, g2)],
                    [("%s:14" % name, node(partner), c)], r)
        out.append("")
    # 缩减网 rn：只收录行首总电容
    out += [
        "*R_NET rn 36.0",
        "*CONN",
        "*I u_cnt0:RN I *D DFFR_X1",
        "*I u_cnt1:RN I *D DFFR_X1",
        "*I u_cnt2:RN I *D DFFR_X1",
        "*I u_cnt3:RN I *D DFFR_X1",
        "*I u_div:RN I *D DFFR_X1",
        "*P rn I",
        "*DRV rn *CELL PAD_IN *C2_R1_C1 2.0 25.0 3.0",
        "*LOADS",
        "*RC u_cnt0:RN 0.010",
        "*RC u_cnt1:RN 0.011",
        "*RC u_cnt2:RN 0.012",
        "*RC u_cnt3:RN 0.013",
        "*RC u_div:RN 0.014",
        "*END",
        "",
        "// 零电容：clk_div2 没有 *CAP / *RES 段；n_div_d 的 *CAP 段只有一条",
        "// 零值记录。两者使 u_inv_div（只连这两个网）成为零电容实例。",
    ]
    out += ["*D_NET clk_div2 0"] + conn_lines("clk_div2") + ["*END", ""]
    out += ["*D_NET n_div_d 0"] + conn_lines("n_div_d") + [
        "*CAP", "1 u_div:D 0", "*RES", "1 u_div:D n_div_d:2 5.0", "*END", ""]
    # 行首值与明细合计不一致（差 1 飞法 > 容差）
    out += dnet("n_and", 46, [("u_and1:ZN", 10), ("n_and:3", 20)],
                [("n_and:3", "u_xor1:A", 15)], 4.6)
    out.append("")
    # 设计里没有的网
    out += [
        "*D_NET ghost_net 5",
        "*CAP",
        "1 ghost_net:1 2",
        "2 ghost_net:1 1",
        "3 ghost_net:1 n_xor:9 2",
        "*RES",
        "1 ghost_net:1 ghost_net:2 0.5",
        "*END",
        "",
    ]
    return "\n".join(out)


# ── 样张 2：sp_small_dialect.spef（方言与容忍）────────────────────────
def gen_dialect():
    p = lambda f: ("%.6f" % f).rstrip("0").rstrip(".")  # PF 写法
    out = [
        '*SPEF "ieee 1481-1999"',
        '*DESIGN "wf_small"',
        '*DATE "Tue Nov 24 00:00:00 2026"',
        '*DESIGN_FLOW "NAME_SCOPE LOCAL" "PIN_CAP NONE"',
        "*DIVIDER /",
        "*DELIMITER :",
        "*BUS_DELIMITER []",
        "*T_UNIT 1 NS",
        "*C_UNIT 1.0 PF",
        "*R_UNIT 1 KOHM",
        "*L_UNIT 1 HENRY",
        "",
        "*NAME_MAP",
        "*1 u_cnt3",
        "*2 u_xor_c3",
        "*3 u_and1",
        "*4 count[3]",
        "*5 n_cnt3_d",
        "",
        "*PORTS",
        "count[0] I",
        "count[1] I",
        "count[2] I",
        "count[3] I",
        "clk I",
        "rn I",
        "clk_div2 O",
        "n_and O",
        "n_xor O",
        "",
        "/* 布局布线工具的写出器形态：名字映射、电容记录不带编号、",
        "   小写版本串、带小数的单位刻度。下面 count[3] 全部走映射索引。 */",
        "",
        "*D_NET *4 0.027",
        "*CONN",
        "*I *2:B I *D XOR2_X1",
        "*I *1:Q O *D DFFR_X1",
        "*I *3:A1 I *D AND2_X1",
        "*P count[3] I",
        "*CAP",
        "*2:B 0.008",
        "*4:14 0.010",
        "*4:14 n_cnt3_d:9 0.009",
        "*RES",
        "1 *2:B *4:14 12.5",
        "*END",
        "",
        "// 多值形态（单位皮法）：三个值取第 1 个（n_b2：24 = 16 + 8），两个值",
        "// 取第 0 个（n_and：45 = 30 + 15）。",
        "*D_NET n_b2 0.014:0.024:0.030",
        "*CONN",
        "*I u_and_c3a:ZN O",
        "*I u_xor_c3:A I",
        "*CAP",
        "u_and_c3a:ZN 0.007:0.009:0.010",
        "n_b2:6 0.003:0.007:0.008",
        "n_b2:6 n_cnt3_d:5 0.004:0.008:0.009",
        "*END",
        "*D_NET n_and 0.045:0.050",
        "*CAP",
        "u_and1:ZN 0.030:0.032",
        "n_and:3 u_xor1:A 0.015:0.016",
        "*END",
        "",
        "// 零耦合：对地两条即全部。",
        "*D_NET clk_div2 0.002",
        "*CONN",
        "*I u_div:Q O *D DFFR_X1",
        "*I u_inv_div:A I *D INV_X1",
        "*P clk_div2 O",
        "*CAP",
        "u_div:Q 0.001",
        "u_inv_div:A 0.001",
        "*END",
        "",
        "orphan_token_between_nets",
        "",
        "// *V 是路由置信度（正整数），位置在网段头总电容之后、*CONN 之前。",
        "*D_NET n_div_d 0.004 *V 90 // 行尾注释",
        "*CONN",
        "*I u_inv_div:ZN O *D INV_X1 *L 0.0022 *C 22000 2000 *S 0.012:0.015:0.019 0.010:0.012:0.016",
        "*I u_div:D I *D DFFR_X1",
        "*N u_div:D *C 24000 2000",
        "*CAP",
        "u_inv_div:ZN 0.001",
        "n_div_d:5",
        "   0.001",
        "n_div_d:6 0.002",
        "*RES",
        "1 u_inv_div:ZN n_div_d:5 0.8",
        "*END",
        "",
        "// n_cnt0_d：10 = 对地 6 + 2 + 1 + 耦合 1；没有 *CONN 段；一条记录跨行；",
        "// 一行两条记录。",
        "*D_NET n_cnt0_d",
        "   0.010",
        "*CAP",
        "1 u_inv_c0:ZN",
        "  0.006",
        "2 n_cnt0_d:3 0.002 3 n_cnt0_d:4 0.001",
        "4 n_cnt0_d:4 count[0]:5 0.001",
        "*END",
        "",
        "// n_b1：没有 *CONN 段、没有耦合（10 = 对地 10）；网段头、*CAP、记录、",
        "// *END 写在一行；网段关键字出现在行中间（*END 之后紧接下一个网）。",
        "*D_NET n_b1 0.010 *CAP u_and_c2:ZN 0.010 *END",
        "*D_NET count[1] 0.009 *CAP u_cnt1:Q 0.006 u_cnt1:Q count[0]:7 0.003 *END",
        "",
        "// 引用了未登记的映射索引：整网跳过。",
        "*D_NET *999 0.001",
        "*CAP",
        "1 x:1 0.001",
        "*END",
        "",
        "// 段级破损：耦合记录缺值，紧跟 *END——该网跳过，其它网不受影响。",
        "*D_NET n_xor 0.010",
        "*CAP",
        "1 n_xor:1 u_xor1:Z",
        "*END",
        "",
        "// 物理网段：整段跳过并计数。",
        "*D_PNET VDD_SHAPE 0.100",
        "*CONN",
        "*P VPWR1 B *L 0",
        "*CAP",
        "1 VPWR1:1 0.100",
        "*END",
        "*R_PNET VSS_SHAPE 0.200",
        "*CONN",
        "*P VPWR0 B *L 0",
        "*END",
        "",
    ]
    return "\n".join(out)


# ── 样张 3：sp_small_rnet.spef（缩减网的几种形态）────────────────────
def gen_rnet():
    out = list(HEADER_FF)
    out += [
        "// 缩减网形态：clk 带完整构造；rn 只有网段头；n_and 带 *Q / *K；",
        "// n_b1 行首值是三值形态（取第 1 个 = 18）。同文件里混有分布式网。",
        "",
        "*R_NET clk 80.0",
        "*CONN",
        "*I u_cnt0:CK I *D DFFR_X1",
        "*I u_cnt1:CK I *D DFFR_X1",
        "*I u_cnt2:CK I *D DFFR_X1",
        "*I u_cnt3:CK I *D DFFR_X1",
        "*I u_div:CK I *D DFFR_X1",
        "*P clk I",
        "*DRV clk *CELL PAD_CLK *C2_R1_C1 4.0 30.0 6.0",
        "*LOADS",
        "*RC u_cnt0:CK 0.020",
        "*RC u_cnt1:CK 0.024",
        "*RC u_cnt2:CK 0.028",
        "*RC u_cnt3:CK 0.032",
        "*RC u_div:CK 0.036",
        "*END",
        "",
        "*R_NET rn 90.0",
        "*END",
        "",
        "*R_NET n_and 12.0",
        "*DRV n_and *CELL PAD_OUT *C2_R1_C1 1.0 10.0 1.0",
        "*LOADS",
        "*RC u_and1:ZN 0.008",
        "*Q 2",
        "*K 0.5 0.25",
        "*END",
        "",
        "*R_NET n_b1 15:18:20",
        "*END",
        "",
    ]
    out += dnet("count[0]", 3, [("u_cnt0:Q", 2)], [("u_cnt0:Q", "n_cnt0_d:3", 1)])
    out.append("")
    out += dnet("n_xor", 8, [("u_xor1:Z", 5)], [("u_xor1:Z", "count[1]:4", 3)])
    out.append("")
    out += ["*D_NET n_div_d 0", "*END", ""]
    out += dnet("clk_div2", 2, [("u_div:Q", 1), ("u_inv_div:A", 1)], [])
    out.append("")
    return "\n".join(out)


# ── 样张 4：bad_header.spef（文件级错误：缺 *C_UNIT）──────────────────
def gen_bad_header():
    out = [
        '*SPEF "IEEE 1481-1998"',
        '*DESIGN "wf_small"',
        "*DIVIDER /",
        "*T_UNIT 1 NS",
        "*R_UNIT 1 OHM",
        "",
        "*D_NET clk 33.0",
        "*CAP",
        "1 u_cnt0:CK 33.0",
        "*END",
        "",
    ]
    return "\n".join(out)


# ── 样张 5–10：块层级多文件（对齐 wf_sub / wf_blk_a / wf_blk_b / wf_blk_top）──
# 设计库里的网名是展平后的层次路径；块绑定文件用块内局部名。
HEADER_BLK_FF = [
    '*SPEF "IEEE 1481-1998"',
    '*DESIGN "wf_blk_top"',
    "*DIVIDER /",
    "*DELIMITER :",
    "*T_UNIT 1 NS",
    "*C_UNIT 1 FF",
    "*R_UNIT 1 OHM",
    "",
]


def blk_dnet(name, total, g, c, conn=None, res=False):
    """块层级样张的分布式网段：对地一条 + 耦合一条（耦合对端取另一张网的节点）。"""
    lines = ["*D_NET %s %s" % (name, fmt(total))]
    if conn:
        lines.append("*CONN")
        lines += conn
    lines.append("*CAP")
    lines.append("1 %s:1 %s" % (name, fmt(g)))
    lines.append("2 %s:1 %s:1 %s" % (name, c[0], fmt(c[1])))
    if res:
        lines.append("*RES")
        lines.append("1 %s:1 %s:2 %s" % (name, name, fmt(total / 10.0)))
    lines.append("*END")
    return lines


def gen_blk_top():
    out = list(HEADER_BLK_FF)
    out += [
        "// 顶层文件（纯路径形态）：网名是展平后的层次路径。",
        "",
    ]
    out += blk_dnet("u_blk_a/n_a1", 10, 6, ("u_blk_a/u_dff_a:D", 4), conn=[
        "*I u_blk_a/u_dff_a:Q O *D DFFR_X1",
        "*I u_blk_a/u_inv1:A I *D INV_X1",
        "*I u_blk_a/u_inv_out:A I *D INV_X1",
        "*I u_blk_a/u_sub/n_in B *D wf_sub",
    ], res=True)
    out.append("")
    out += blk_dnet("u_blk_a/n_a2", 20, 12, ("u_blk_a/clk", 8))
    out.append("")
    out += blk_dnet("u_blk_a/n_out", 30, 18, ("u_blk_a/n_a1", 12))
    out.append("")
    out += blk_dnet("u_blk_a/u_sub/n1", 40, 24, ("u_blk_a/u_sub/n3", 16))
    out.append("")
    out += blk_dnet("u_blk_b/n_b1", 50, 30, ("u_blk_b/n_b2", 20))
    out.append("")
    out += blk_dnet("n_top", 60, 36, ("n_top2", 24), conn=[
        "*I u_blk_a/n_out O *D wf_blk_a", "*P n_top O"])
    out.append("")
    out += blk_dnet("n_top2", 70, 42, ("n_top", 28))
    out.append("")
    out += blk_dnet("clk", 80, 48, ("rn", 32), conn=[
        "*I u_blk_a/clk B *D wf_blk_a", "*I u_blk_b/clk B *D wf_blk_b",
        "*P clk I"])
    out.append("")
    out += blk_dnet("rn", 90, 54, ("clk", 36))
    out.append("")
    out += [
        "// 设计里没有的网。",
        "*D_NET ghost_top 5.0",
        "*CAP",
        "1 ghost_top:1 3.0",
        "2 ghost_top:1 n_top:1 2.0",
        "*END",
        "",
    ]
    return "\n".join(out)


def gen_blk_a_inst():
    out = list(HEADER_BLK_FF)
    out += [
        "// 块实例绑定文件（block_inst = u_blk_a）：网名是块内局部名。",
        "// n_a1 / n_a2 / n_out 与顶层文件里的 u_blk_a/n_a1 等是同一张网——不同",
        "// 作用域的互补寄生，合并时相加。",
        "",
    ]
    out += blk_dnet("n_a1", 5, 3, ("n_out", 2))
    out.append("")
    out += blk_dnet("n_a2", 7, 4, ("n_a1", 3))
    out.append("")
    out += blk_dnet("n_out", 9, 5, ("n_a1", 4))
    out.append("")
    return "\n".join(out)


def gen_blk_top_overlap():
    out = list(HEADER_BLK_FF)
    out += [
        "// 第二个顶层文件：与第一个顶层文件同作用域。n_top 与 u_blk_a/n_a2 重合",
        "// （保留首份，本文件的值丢弃）；u_blk_b/n_out 是新贡献。",
        "",
    ]
    out += blk_dnet("n_top", 999, 600, ("n_top2", 399))
    out.append("")
    out += blk_dnet("u_blk_a/n_a2", 888, 500, ("u_blk_a/clk", 388))
    out.append("")
    out += blk_dnet("u_blk_b/n_out", 33, 20, ("u_blk_b/n_b1", 13))
    out.append("")
    return "\n".join(out)


def gen_blk_strip():
    out = list(HEADER_BLK_FF)
    out += [
        "// 块实例绑定 + 前缀剥离（block_inst = u_blk_a，strip_prefix = tb/u_dut）。",
        "// 前两条剥除后是块内局部名，与 blk_a_inst.spef 同作用域重合（丢弃）；",
        "// 后两条前缀未命中（含剥后余空），条目跳过并计数。",
        "",
    ]
    out += blk_dnet("tb/u_dut/n_a1", 77, 40, ("n_out", 37))
    out.append("")
    out += blk_dnet("tb/u_dut/n_out", 66, 30, ("n_a1", 36))
    out.append("")
    out += blk_dnet("other/n_a2", 55, 30, ("n_a1", 25))
    out.append("")
    out += ["*D_NET tb/u_dut/ 44.0", "*CAP", "1 tb/u_dut/:1 44.0", "*END", ""]
    return "\n".join(out)


def gen_blk_sub_cell():
    out = list(HEADER_BLK_FF)
    out += [
        "// 块定义绑定文件（block_cell = wf_sub）：网名是块定义内的局部名，对该",
        "// 定义的每个实例化各算一次（本设计只有一个 u_blk_a/u_sub）。n1 与顶层",
        "// 文件里的 u_blk_a/u_sub/n1 互补相加；n2 / n3 是新贡献。",
        "",
    ]
    out += blk_dnet("n1", 3, 2, ("n3", 1))
    out.append("")
    out += blk_dnet("n2", 8, 5, ("n3", 3))
    out.append("")
    out += blk_dnet("n3", 6, 4, ("n1", 2))
    out.append("")
    return "\n".join(out)


def gen_blk_bad_binding():
    out = list(HEADER_BLK_FF)
    out += ["*D_NET clk 123.0", "*CAP", "1 clk:1 100.0", "*END", ""]
    return "\n".join(out)


# ── 样张 11：blk_top_nameforms.spef（名字形态：非斜杠分隔符 + 转义）────
def gen_blk_nameforms():
    out = [
        '*SPEF "IEEE 1481-1998"',
        '*DESIGN "wf_blk_top"',
        "*DIVIDER .",
        "*DELIMITER :",
        "*T_UNIT 1 NS",
        "*C_UNIT 1 FF",
        "*R_UNIT 1 OHM",
        "",
        "// 文件头声明的层次分隔符是点：u_blk_a.n_a1 即设计库里的 u_blk_a/n_a1。",
        "// 第二张网用转义的斜杠写（部分工具不合规转义的宽容口径），去转义后命中。",
        "// 数值与 blk_top.spef 的同名网相同。",
        "",
    ]
    out += blk_dnet("u_blk_a.n_a1", 10, 6, ("u_blk_a.n_a2", 4))
    out.append("")
    out += blk_dnet("u_blk_a\\/n_a2", 20, 12, ("u_blk_a\\/n_a1", 8))
    out.append("")
    out += blk_dnet("u_blk_a.u_sub.n1", 40, 24, ("u_blk_a.u_sub.n3", 16))
    out.append("")
    return "\n".join(out)


# ── 样张 12：sp_pg.spef（电源网段跳过；配带电源网的派生设计）──────────
def gen_pg():
    out = list(HEADER_FF)
    out += [
        "// 配 wf_small 的带电源网派生设计（VDD / VSS 两个电源网）。电源网的网段",
        "// 跳过并计数；clk 是普通信号网，照常收录（33 = 对地 22 + 耦合 11）。",
        "",
        "*D_NET VDD 1000.0",
        "*CONN",
        "*P VDD B",
        "*CAP",
        "1 VDD:1 1000.0",
        "*END",
        "*D_NET VSS 800.0",
        "*CONN",
        "*P VSS B",
        "*CAP",
        "1 VSS:1 800.0",
        "*END",
    ]
    out += dnet("clk", 33, [("u_cnt0:CK", 22)], [("u_cnt0:CK", "u_cnt0:RN", 11)])
    out.append("")
    return "\n".join(out)


GENERATORS = {
    "sp_small.spef": gen_small,
    "sp_small_dialect.spef": gen_dialect,
    "sp_small_rnet.spef": gen_rnet,
    "bad_header.spef": gen_bad_header,
    "blk_top.spef": gen_blk_top,
    "blk_a_inst.spef": gen_blk_a_inst,
    "blk_top_overlap.spef": gen_blk_top_overlap,
    "blk_strip.spef": gen_blk_strip,
    "blk_sub_cell.spef": gen_blk_sub_cell,
    "blk_bad_binding.spef": gen_blk_bad_binding,
    "blk_top_nameforms.spef": gen_blk_nameforms,
    "sp_pg.spef": gen_pg,
}


def main():
    out_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(
        os.path.abspath(__file__))
    os.makedirs(out_dir, exist_ok=True)
    for name, fn in sorted(GENERATORS.items()):
        path = os.path.join(out_dir, name)
        with open(path, "w", newline="\n") as f:
            f.write(fn())
        print("wrote", path)


if __name__ == "__main__":
    main()
