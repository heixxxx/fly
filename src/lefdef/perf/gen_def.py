#!/usr/bin/env python3
"""大规模 DEF 测试文件生成器。

规格：
- COMPONENTS >= 50000
- NETS >= 100000，每个 net 2-5 个 connections + 2-4 条普通金属线
- SPECIALNETS 仅 VDD 与 VSS，各 100000 条特殊金属线（STRIPE）
坐标与引用均为语法合法的合成数据，parser 不做语义校验。
"""
import random
import sys

OUT = sys.argv[1] if len(sys.argv) > 1 else "big.def"
N_COMP = int(sys.argv[2]) if len(sys.argv) > 2 else 50000
N_NET = int(sys.argv[3]) if len(sys.argv) > 3 else 100000
N_SNET_WIRE = int(sys.argv[4]) if len(sys.argv) > 4 else 100000  # 每 specialnet 的金属线条数
DIE = 5000000

rng = random.Random(42)
LAYERS = [f"metal{i}" for i in range(1, 11)]
PINS = ["A", "B", "C", "D", "E"]

fh = open(OUT, "w", buffering=1 << 20)
buf = []


def emit(line):
    buf.append(line)
    if len(buf) >= 4096:
        fh.write("\n".join(buf) + "\n")
        buf.clear()


emit("VERSION 6.0 ;")
emit('DIVIDERCHAR "/" ;')
emit('BUSBITCHARS "[]" ;')
emit("DESIGN perf_test ;")
emit("UNITS DISTANCE MICRONS 1000 ;")
emit(f"DIEAREA ( 0 0 ) ( {DIE} {DIE} ) ;")
emit("")

comp_x = [0] * N_COMP
comp_y = [0] * N_COMP
emit(f"COMPONENTS {N_COMP} ;")
for i in range(N_COMP):
    comp_x[i] = rng.randrange(1000, DIE - 1000)
    comp_y[i] = rng.randrange(1000, DIE - 1000)
    emit(f"- u{i} CMP{i % 2000} + PLACED ( {comp_x[i]} {comp_y[i]} ) N ;")
emit("END COMPONENTS")
emit("")

total_conn = 0
total_net_wire = 0
emit(f"SPECIALNETS 2 ;")
for net_name, use_kw in (("VDD", "POWER"), ("VSS", "GROUND")):
    emit(f"- {net_name}")
    layer = rng.randrange(10)
    width = 2000
    x = rng.randrange(0, DIE)
    y = rng.randrange(0, DIE)
    x2 = min(x + rng.randrange(1000, 100000), DIE)
    emit(f"  + ROUTED {LAYERS[layer]} {width} + SHAPE STRIPE ( {x} {y} ) ( {x2} * )")
    for j in range(1, N_SNET_WIRE):
        if j % 2 == 0:  # 水平/垂直交替
            x = rng.randrange(0, DIE)
            x2 = min(x + rng.randrange(1000, 100000), DIE)
            emit(f"    NEW {LAYERS[(layer + j) % 10]} {width} + SHAPE STRIPE ( {x} {y} ) ( {x2} * )")
        else:
            y = rng.randrange(0, DIE)
            y2 = min(y + rng.randrange(1000, 100000), DIE)
            emit(f"    NEW {LAYERS[(layer + j) % 10]} {width} + SHAPE STRIPE ( {x} {y} ) ( * {y2} )")
    emit(f"  + USE {use_kw} ;")
emit("END SPECIALNETS")
emit("")

emit(f"NETS {N_NET} ;")
for i in range(N_NET):
    n_conn = rng.randrange(2, 6)  # 2-5 个 connections
    total_conn += n_conn
    conns = " ".join(
        f"( u{rng.randrange(N_COMP)} {PINS[rng.randrange(5)]} )" for _ in range(n_conn)
    )
    emit(f"- n{i} {conns}")
    n_wire = rng.randrange(2, 5)  # 2-4 条普通金属线
    total_net_wire += n_wire
    layer = rng.randrange(10)
    x = rng.randrange(0, DIE)
    y = rng.randrange(0, DIE)
    emit(f"  + ROUTED {LAYERS[layer]} ( {x} {y} ) ( {min(x + rng.randrange(1000, 50000), DIE)} * )")
    for j in range(1, n_wire):
        x = rng.randrange(0, DIE)
        y = rng.randrange(0, DIE)
        emit(f"    NEW {LAYERS[(layer + j) % 10]} ( {x} {y} ) ( * {min(y + rng.randrange(1000, 50000), DIE)} )")
    emit("  + USE SIGNAL ;")
emit("END NETS")
emit("")
emit("END DESIGN")

if buf:
    fh.write("\n".join(buf) + "\n")
fh.close()

print(f"written {OUT}")
print(f"components={N_COMP} nets={N_NET} net_connections={total_conn} net_wires={total_net_wire}")
print(f"specialnet_wires: VDD={N_SNET_WIRE} VSS={N_SNET_WIRE} total={2 * N_SNET_WIRE}")
