#!/usr/bin/env bash
# verify_full.sh —— design_sets 三套全量验证（手动跑；QA 用例只覆盖
# mini/存在性断言，见 ../test_emir_design_sets_{a,b,c}.py）
#
# 产物分档规则：单文件 >5MB 不 commit（README 记录再生成命令）——本脚本
# 可在 fresh clone 上重建全部大数据件并跑全量 design db + timing db 验证。
#
# 用法（仓库根）：
#   bash qa/emir/data/design_sets/verify_full.sh [a|b|c|all]
#
# 工具（/root/project，2026-09-15 安装，不随仓库分发）：
#   OpenROAD：/root/project/openroad_env/bin/openroad（仅 A 套物理流程）
#   OpenSTA：/root/project/opensta/build/sta（TWF 权威路径）

set -uo pipefail
DS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$DS_DIR/../../.." && pwd)"
WORK="$REPO_ROOT/.work/design_sets_verify"
STA=/root/project/opensta/build/sta
OR=/root/project/openroad_env/bin/openroad
FLY="$REPO_ROOT/build/bin/fly"
TARGET="${1:-all}"
mkdir -p "$WORK"
cd "$DS_DIR"

run_stage() {  # run_stage <名> <超时秒> <命令...>
    local name="$1" tmo="$2"; shift 2
    echo "=== [$name] start ($(date +%H:%M:%S)) ==="
    if ! timeout "$tmo" "$@" > "$WORK/$name.log" 2>&1; then
        echo "=== [$name] FAILED (log: $WORK/$name.log, tail:) ==="
        tail -20 "$WORK/$name.log"
        exit 1
    fi
    echo "=== [$name] done ==="
}

gen_a() {
    run_stage gen_a 300 python3 gen_set_a.py
    # 物理分级：A 套 = OpenROAD 完整物理（floorplan+place+CTS+route）
    run_stage a_flow 3600 $OR -no_init -exit set_a_flow.tcl
    run_stage a_twf 1800 env TWF_TOP=arith_chain \
        TWF_V_FILES="set_a/arith_chain_routed.v" \
        TWF_SDC=set_a/arith_chain.sdc TWF_OUT_BASE=set_a/arith_chain \
        $STA -no_init -exit twf_gen_hier.tcl
}

gen_b() {
    run_stage gen_b 600 python3 gen_set_b.py
    # 物理分级：B 套 = place 级（生成器直写层级 DEF，PLACED 网格坐标）
    run_stage b_twf_block 600 env TWF_TOP=mesh_cell \
        TWF_V_FILES="set_b/mesh_cell.v" TWF_SDC=set_b/mesh_cell.sdc \
        TWF_OUT_BASE=set_b/mesh_cell $STA -no_init -exit twf_gen_hier.tcl
    run_stage b_strip 60 python3 -c "
import sys; sys.path.insert(0, '.')
from gen_common import make_strip_variant
n = make_strip_variant('set_b/mesh_cell_mixed.twf', 'set_b/mesh_cell_strip.twf', 'tb/u_dut')
print(f'strip variant: {n} entries')"
    # 顶层 TWF（~26k 条目，分钟级；层级引脚枚举用 -hierarchical）
    run_stage b_twf_top 3600 env TWF_TOP=noc_mesh \
        TWF_V_FILES="set_b/mesh_cell.v set_b/noc_mesh.v" \
        TWF_SDC=set_b/noc_mesh.sdc TWF_OUT_BASE=set_b/noc_mesh \
        TWF_PIN_HIER=1 $STA -no_init -exit twf_gen_hier.tcl
}

gen_c() {
    run_stage gen_c 1200 python3 gen_set_c.py
    # 物理分级：C 套 = floorplan 级（生成器直写层级 DEF）
    run_stage c_twf_blocks 900 bash -c '
for blk in pe_core tile_router ctrl_block; do
  env TWF_TOP=$blk TWF_V_FILES="set_c/$blk.v" TWF_SDC="set_c/$blk.sdc" \
      TWF_OUT_BASE="set_c/$blk" '"$STA"' -no_init -exit twf_gen_hier.tcl || exit 1
done'
    run_stage c_strip 60 python3 -c "
import sys; sys.path.insert(0, '.')
from gen_common import make_strip_variant
n = make_strip_variant('set_c/pe_core_mixed.twf', 'set_c/pe_core_strip.twf', 'tb/u_dut')
print(f'strip variant: {n} entries')"
    run_stage c_twf_mini 600 env TWF_TOP=hybrid_soc \
        TWF_V_FILES="set_c/pe_core.v set_c/tile_router.v set_c/ctrl_block.v set_c/compute_tile.v set_c/hybrid_soc_mini.v" \
        TWF_SDC=set_c/hybrid_soc.sdc TWF_OUT_BASE=set_c/hybrid_soc_mini \
        TWF_PIN_HIER=1 $STA -no_init -exit twf_gen_hier.tcl
    # 顶层全量 TWF（102k 实例，~分钟级）
    run_stage c_twf_top 3600 env TWF_TOP=hybrid_soc \
        TWF_V_FILES="set_c/pe_core.v set_c/tile_router.v set_c/ctrl_block.v set_c/compute_tile.v set_c/hybrid_soc.v" \
        TWF_SDC=set_c/hybrid_soc.sdc TWF_OUT_BASE=set_c/hybrid_soc \
        TWF_PIN_HIER=1 $STA -no_init -exit twf_gen_hier.tcl
}

# 全量建库验证（fly 脚本：design db + timing db 大套断言）
verify_py() {
    run_stage verify_full 3600 $FLY --log-dir "$WORK/verify_log" \
        "$DS_DIR/verify_full_impl.py"
}

case "$TARGET" in
    a) gen_a ;;
    b) gen_b ;;
    c) gen_c ;;
    all)
        gen_a; gen_b; gen_c; verify_py ;;
    *) echo "usage: $0 [a|b|c|all]"; exit 2 ;;
esac
echo "ALL DONE — logs in $WORK"
