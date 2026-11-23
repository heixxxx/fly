#!/usr/bin/env bash
# waveform db 测试 VCD 一键再生成：gen_design.py 产 Verilog/DEF →
# Icarus Verilog（iverilog + vvp）仿真产出全部 VCD。
#
# 确定性：全部激励固定、无随机源、无系统时间依赖（$date 头除外——
# 解析器不消费），重跑除 $date 外逐字节一致。
#
# 用法：bash gen.sh   （cwd 任意；产物落本目录）
# 依赖：iverilog / vvp（Icarus Verilog ≥ 10）

set -euo pipefail
cd "$(dirname "$0")"

if ! command -v iverilog >/dev/null 2>&1; then
    echo "ERROR: iverilog 未安装（apt-get install iverilog）" >&2
    exit 1
fi

echo "== gen_design.py: 生成 Verilog 网表与 DEF =="
python3 gen_design.py

BUILD="$PWD/.build"
rm -rf "$BUILD"
mkdir -p "$BUILD"
trap 'rm -rf "$BUILD"' EXIT

# sim <仿真名> <vcd 文件名> <源 .v...>：编译并在本目录运行（$dumpfile 相对 cwd）
sim() {
    local sim_name="$1" vcd="$2"
    shift 2
    echo "== iverilog: $sim_name =="
    iverilog -o "$BUILD/$sim_name" "$@"
    vvp "$BUILD/$sim_name" >/dev/null
    test -s "$vcd" || { echo "ERROR: $vcd 未产出" >&2; exit 1; }
    echo "   -> $vcd ($(wc -c <"$vcd") bytes)"
}

sim small    small.vcd     rtl/cells_sim.v rtl/wf_small.v
sim part_a1  part_a1.vcd   rtl/cells_sim.v rtl/wf_small_a1.v
sim part_a2  part_a2.vcd   rtl/cells_sim.v rtl/wf_small_a2.v
sim blk_b1   block_b1.vcd  rtl/cells_sim.v rtl/wf_blk_b1.v
sim blk_b2   block_b2.vcd  rtl/cells_sim.v rtl/wf_blk_b2.v
sim instpin  instpin.vcd   rtl/cells_sim.v rtl/wf_blk_instpin.v
sim xz       xz.vcd        rtl/cells_sim.v rtl/wf_xz.v
sim big      big.vcd       rtl/cells_sim.v rtl/wf_big.v

echo "== 全部 VCD 再生成完成 =="
