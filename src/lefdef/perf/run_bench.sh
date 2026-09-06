#!/bin/bash
# lefdef 性能基准 + 正确性回归脚本
# 用法:
#   run_bench.sh baseline   —— 以当前工作树构建产物生成金标（仅在基线锁定时跑一次）
#   run_bench.sh verify     —— 构建 + 回归比对 + 5 轮基准（每项优化后跑）
# 金标与产物均在 .work/lefdef_perf/ 下（不入 git）。
set -e
PERF=/root/fly/.work/lefdef_perf
SRC=/root/fly/src/lefdef
DEF_SRC=$SRC/def/def
GOLD=$PERF/gold
TESTS="complete.5.8.def test_escape.def"

cd "$PERF"

build_all() {
    # parser 库 + perf 程序（-O3 与基准口径一致）
    g++ -O3 -std=c++17 -I "$DEF_SRC" defrw_perf_src.cpp "$DEF_SRC"/*.cpp -o defrw_perf_now 2>/dev/null
    # 回显工具 defrw（上游 Makefile 构建）
    # 注意：上游 .cpp.o 规则无头文件依赖跟踪，改 .hpp 后必须 clean 全量重建，
    # 否则新旧布局的 .o 混链会 ODR 违例（实测表现为 File 成员读位错乱段错误）。
    make -C "$SRC/def" clean > /dev/null 2>&1
    make -C "$SRC/def" -j1 > build_defrw.log 2>&1
}

case "$1" in
baseline)
    build_all
    mkdir -p "$GOLD"
    for t in $TESTS; do
        ( cd "$GOLD" && "$SRC/def/bin/defrw" "$SRC/def/TEST/$t" > "gold_$t.out" 2>/dev/null )
        echo "金标已生成: gold_$t.out ($(wc -l < "$GOLD/gold_$t.out") 行)"
    done
    ;;
verify)
    build_all
    fail=0
    for t in $TESTS; do
        ( cd "$PERF" && "$SRC/def/bin/defrw" "$SRC/def/TEST/$t" > "now_$t.out" 2>/dev/null || true )
        if diff -q "$GOLD/gold_$t.out" "now_$t.out" > /dev/null; then
            echo "[回显一致] $t"
        else
            echo "[回显差异!] $t"; diff "$GOLD/gold_$t.out" "now_$t.out" | head -10; fail=1
        fi
    done
    # Galaxy.def 计数校验（传统模式：路径挂 net，wirepaths 可统计）
    cnt=$(./bench_traditional Galaxy.def 2>&1 | tail -1)
    echo "[计数] $cnt"
    if [ -f "$GOLD/gold_counts.txt" ]; then
        cur=$(echo "$cnt" | grep -o "components=[0-9]* nets=[0-9]* specialnets=[0-9]* connections=[0-9]* net_wirepaths=[0-9]* snet_wirepaths=[0-9]*")
        if [ "$cur" == "$(cat "$GOLD/gold_counts.txt")" ]; then
            echo "[计数一致]"
        else
            echo "[计数差异!] 金标: $(cat "$GOLD/gold_counts.txt")"; fail=1
        fi
    fi
    [ $fail -eq 1 ] && exit 1
    # 5 轮基准
    total=0
    for i in 1 2 3 4 5; do
        s=$( { /usr/bin/time -f "%e" ./defrw_perf_now Galaxy.def; } 2>&1 | tail -1 )
        echo "run$i: ${s}s"
        total=$(python3 -c "print($total + $s)")
    done
    python3 -c "print(f'5 轮平均: {round($total/5, 2)}s')"
    ;;
*)
    echo "用法: run_bench.sh baseline|verify"; exit 2 ;;
esac
