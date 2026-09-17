#!/usr/bin/env python3
"""design_sets 三套放大设计共用工具（qa/emir/data/design_sets 生成器族）。

职责（2026-09-15 用户裁定）：
  1. Liberty cell 存在性防呆（强制）：引用 cell 名集合与 Nangate45
     typical.lib 的 cell 集合做差集，非空立即 fail 并列出缺名——绝不生成
     引用不存在 cell 的网表（gen_pg_grid 幻影 cell 教训：X4/X16 引用后
     OpenROAD link 建 black box、时钟链全断，见 data/timing/README.md）；
  2. Module 统一模型：Verilog 与 DEF 同源渲染（网名/引脚名一致性的
     确定性来源，与 data/timing 途径二口径一致）；
  3. strip_prefix 前缀包装变体（人为前缀形态的块 TWF 文本变体）。

层级 DEF 直写说明：OpenROAD 预编译包 write_def 输出平铺网表（2026-09-15
实测 module 层级展开为 `u_sub/u_dff` 形态、层级丢失）——B/C 套层级 DEF
由本工具直写（floorplan 级语义：实例 PLACED 网格坐标 + 网连接，无布线
几何），物理分级相应为：A 套 OpenROAD 完整物理 / B 套 place 级 / C 套
floorplan 级（均无布线几何的套，design db NETS 不含网 → 网维度 TWF
条目计 TIMG::0003 悬空，属预期口径，QA 断言按此写并注明）。

全部生成纯算术确定性（无随机）——同环境重跑逐字节一致。
"""

import re
from pathlib import Path

# Nangate45 平台常量（tech lef DATABASE MICRONS 2000 → DEF 同口径）
LIBERTY_NAME = "NangateOpenCellLibrary_typical.lib"
DEF_UNITS = 2000
# 布局网格（DBU）：横向步长 0.2µm、行高 1.4µm（Nangate45 行高）
GRID_X = 400
GRID_Y = 2800


def load_liberty_cells(lib_path):
    """解析 Liberty 文件的 cell 名集合（`cell (NAME) {` 定义行）。"""
    cells = set()
    pat = re.compile(r"^\s*cell\s*\(([^)]+)\)")
    with open(lib_path, "r", encoding="utf-8") as f:
        for line in f:
            m = pat.match(line)
            if m:
                cells.add(m.group(1).strip())
    if not cells:
        raise SystemExit(f"gen_common: no cell parsed from {lib_path}")
    return cells


def check_cells_available(lib_path, refs, ctx, whitelist=()):
    """防呆（强制）：refs 中任一 cell 不在 Liberty 集合 → SystemExit 列缺名。

    whitelist：本批生成器自定义的 block module 名（块定义是生成器自身
    产物而非工艺单元，豁免检查）。
    """
    cells = load_liberty_cells(lib_path)
    missing = sorted(r for r in refs if r not in cells and r not in whitelist)
    if missing:
        raise SystemExit(
            f"gen_common[{ctx}]: {len(missing)} cell(s) not in "
            f"{Path(lib_path).name}: {missing} — refusing to emit netlist "
            f"(phantom-cell guard)")
    return len(cells)


class Module:
    """单 module 的实例/连接模型（Verilog 与 DEF 同源渲染）。"""

    def __init__(self, name, ports):
        # ports: [(pin_name, "input" | "output")]
        self.name = name
        self.ports = list(ports)
        self.port_names = {p for p, _ in ports}
        self.insts = []          # (inst_name, cell_name, {pin: net})
        self.nets = set()
        self.cell_refs = set()   # 引用 cell 名集合（防呆输入）

    def add_inst(self, inst_name, cell_name, **conns):
        for pin, net in conns.items():
            self.nets.add(net)
        self.insts.append((inst_name, cell_name, conns))
        self.cell_refs.add(cell_name)

    @property
    def inst_count(self):
        return len(self.insts)

    def to_verilog(self, header_comment=""):
        out = []
        if header_comment:
            out.append(f"// {header_comment}")
        plist = ", ".join(p for p, _ in self.ports)
        out.append(f"module {self.name} ({plist});")
        for pin, direction in self.ports:
            out.append(f"  {direction} {pin};")
        # 端口名即网名（Verilog input/output 隐式声明网，不再 wire）
        port_set = set(self.port_names)
        for net in sorted(self.nets):
            if net not in port_set:
                out.append(f"  wire {net};")
        out.append("")
        for inst_name, cell_name, conns in self.insts:
            pins = ", ".join(f".{pin}({net})" for pin, net in conns.items())
            out.append(f"  {cell_name} {inst_name} ({pins});")
        out.append("endmodule")
        return "\n".join(out) + "\n"

    def to_def(self, diearea, placements, ports_xy):
        """渲染 DEF（floorplan 级：PLACED 组件 + PINS + NETS，无布线几何）。

        placements: {inst_name: (x, y, orient)}（DBU；orient 如 "N"/"FS"）；
        ports_xy:   {port_name: (x, y)}（PINS 摆点，LAYER metal2 占位几何）。
        """
        o = []
        o.append("VERSION 5.8 ;")
        o.append('DIVIDERCHAR "/" ;')
        o.append('BUSBITCHARS "[]" ;')
        o.append(f"DESIGN {self.name} ;")
        o.append(f"UNITS DISTANCE MICRONS {DEF_UNITS} ;")
        x0, y0, x1, y1 = diearea
        o.append(f"DIEAREA ( {x0} {y0} ) ( {x1} {y1} ) ;")
        o.append("")
        o.append(f"COMPONENTS {len(self.insts)} ;")
        for inst_name, cell_name, _ in self.insts:
            x, y, orient = placements[inst_name]
            o.append(f"    - {inst_name} {cell_name} + PLACED ( {x} {y} ) "
                     f"{orient} ;")
        o.append("END COMPONENTS")
        o.append("")
        o.append(f"PINS {len(self.ports)} ;")
        for pin, direction in self.ports:
            x, y = ports_xy[pin]
            o.append(f"    - {pin} + NET {self._net_of_port(pin)}")
            o.append(f"      + DIRECTION {'INPUT' if direction == 'input' else 'OUTPUT'}")
            o.append("      + USE SIGNAL")
            o.append("      + PORT")
            o.append("        + LAYER metal2 ( -100 -100 ) ( 100 100 )")
            o.append(f"        + FIXED ( {x} {y} ) N ;")
        o.append("END PINS")
        o.append("")
        # 网连接：实例 pin（DEF 顺序）+ 端口位 ( PIN port )
        net_conns = {net: [] for net in sorted(self.nets)}
        port_net = {p: self._net_of_port(p) for p, _ in self.ports}
        # net → port 反表一次遍历预构建（端口名即网名，网与端口一一对应
        # ——逐网线性反查是 O(ports²)，纯打磨但反表更直白）。
        net_to_port = {n: p for p, n in port_net.items()}
        for net in port_net.values():
            if net in net_conns:
                net_conns[net].append(None)   # None = 端口位，渲染时回填名
        for inst_name, _, conns in self.insts:
            for pin, net in conns.items():
                net_conns[net].append((inst_name, pin))
        net_lines = []
        for net in sorted(net_conns):
            terms = []
            for c in net_conns[net]:
                if c is None:
                    terms.append(f"( PIN {net_to_port[net]} )")
                else:
                    terms.append(f"( {c[0]} {c[1]} )")
            net_lines.append(f"    - {net} " + " ".join(terms) + " ;")
        o.append(f"NETS {len(net_lines)} ;")
        o.extend(net_lines)
        o.append("END NETS")
        o.append("")
        o.append("END DESIGN")
        return "\n".join(o) + "\n"

    def _net_of_port(self, port):
        # 端口网约定：网名 = 端口名本身（业界惯例；Verilog 端口隐式声明
        # 网，DEF PINS 的 NET 字段直指同名网）——生成器连接时用端口名
        return port


def connect_ports(module):
    """校验每个端口的连接网存在（网名 = 端口名约定），返回端口→网表。"""
    result = {}
    for pin, _ in module.ports:
        if pin not in module.nets:
            raise SystemExit(
                f"gen_common[{module.name}]: port '{pin}' has no connecting "
                f"net — port must be driven/loaded by an instance "
                f"(connect via pin name = port name)")
        result[pin] = pin
    return result


def make_strip_variant(src_twf, dst_twf, prefix):
    """strip_prefix 场景的人为前缀包装变体：条目名（NET/PIN）统一插前缀段。

    仅改条目名字面（TWF 语法其余不变）；WAVEFORM/HEADER 原样保留——
    前缀包装的语义即「外层包装顶层仿真产出：名字 = 前缀 + 块内名字」。
    """
    pat = re.compile(r'^\((NET|PIN) "([^"]*)"')
    n_patched = 0
    with open(src_twf, "r", encoding="utf-8") as fin, \
            open(dst_twf, "w", encoding="utf-8") as fout:
        for line in fin:
            m = pat.match(line)
            if m:
                fout.write(f'({m.group(1)} "{prefix}/{m.group(2)}"'
                           + line[m.end():])
                n_patched += 1
            else:
                fout.write(line)
    if n_patched == 0:
        raise SystemExit(
            f"gen_common: strip variant of {src_twf} patched 0 entries — "
            f"source does not look like an entry-bearing TWF")
    return n_patched


def fmt_size(n):
    return f"{n / (1 << 20):.1f}MB" if n >= (1 << 20) else f"{n / 1024:.0f}KB"
