#!/bin/bash
# 全量 QA 多轮稳定性测试（取代原单进程 storage 冒烟简略版）。
#
# 每轮经 qa/runqa 跑完整 QA 套件（qa/ 下全部 test_*.py / *.pyt，约 147 个 case），
# 任一轮失败立即停止并保留失败现场 —— runqa 每轮会重建 qa/logs/qa.log、每个 case
# 跑前会清自己的历史日志，故绝不带病进入下一轮（否则失败现场被覆盖，无法定位）。
#
# 用法: ./stability_test.sh [轮数] [选项]
#   轮数                 总轮数（默认 50）
#   -j N                 每轮 runqa 并行度（默认 4 —— AGENTS.md 多轮稳定性配方）
#   -t SEC               单 case 超时，透传给 runqa（默认 30）
#   --no-build           跳过开头的 build+install，直接用当前 build/ 二进制
#   --fail-fast          轮内首个失败即中止该轮（默认跑完整轮保留失败全貌；稳定性
#                        主循环任一轮失败都会终止，现场不会被下一轮覆盖）
#   --mem-gate MB        内存闸门：MemAvailable 低于该值时等待后再开轮（默认 2560）
#   --round-timeout SEC  单轮外层超时（默认 3600；超时视为失败并清理 fly 残留进程）
#
# 产物目录（.work/ 已 gitignore）: .work/stability/<启动时间戳>/
#   round_NNN.log          每轮 runqa 完整输出
#   round_NNN.qa.log       每轮 qa/logs/qa.log 快照（qa/logs/qa.log 每轮被 runqa 重建）
#   failure_round_N/       失败现场：qa.log + 失败 case 的 qa/<category>/<case>/ 目录
set -u
cd "$(dirname "$0")"

rounds=50; j=4; t=30; do_build=1; fail_fast=""; mem_gate_mb=2560; round_timeout=3600
while [ $# -gt 0 ]; do
  case "$1" in
    -j) j="$2"; shift 2 ;;
    -t) t="$2"; shift 2 ;;
    -j[0-9]*) j="${1#-j}"; shift ;;
    -t[0-9]*) t="${1#-t}"; shift ;;
    --no-build) do_build=0; shift ;;
    --fail-fast) fail_fast="--fail-fast"; shift ;;
    --mem-gate) mem_gate_mb="$2"; shift 2 ;;
    --round-timeout) round_timeout="$2"; shift 2 ;;
    ''|*[!0-9]*) echo "stability_test.sh: 未知参数: $1" >&2; exit 2 ;;
    *) rounds="$1"; shift ;;
  esac
done

run_dir=".work/stability/$(date +%Y%m%d_%H%M%S)"
mkdir -p "$run_dir"

log() { echo "[$(date '+%m-%d %H:%M:%S')] $*"; }
mem_avail_mb() { awk '/^MemAvailable:/{print int($2/1024)}' /proc/meminfo; }
fmt_dur() { local s=$1; printf '%dh%02dm%02ds' $((s/3600)) $((s%3600/60)) $((s%60)); }

mem_gate_wait() {
  while :; do
    local avail; avail=$(mem_avail_mb)
    [ "$avail" -ge "$mem_gate_mb" ] && return 0
    log "[mem] MemAvailable ${avail}MB < ${mem_gate_mb}MB，等待 10s..."
    sleep 10
  done
}

trap 'echo; log "收到中断，已通过 ${passed_rounds}/${rounds} 轮后停止；产物: ${run_dir}/"; exit 130' INT TERM

log "=== Fly 全量 QA 稳定性测试: ${rounds} 轮 × runqa -j${j} -t${t} ==="
log "主机: $(nproc) 核 / 总内存 $(awk '/^MemTotal:/{print int($2/1024)}' /proc/meminfo)MB / 内存闸门 ${mem_gate_mb}MB / 单轮超时 ${round_timeout}s"
log "产物目录: ${run_dir}/"

if [ "$do_build" -eq 1 ]; then
  log "=== 预检: build + install（保证测的是当前代码）==="
  ./fly.sh build //src/main/cpp:fly || { log "build 失败，终止"; exit 1; }
  ./fly.sh install || { log "install 失败，终止"; exit 1; }
else
  log "=== 预检: --no-build，使用现有 build/ 二进制 ==="
  [ -x build/bin/fly ] || { log "build/bin/fly 不存在：先去掉 --no-build 或先 install"; exit 1; }
fi
# 释放 bazel server 常驻内存再进测试循环（压测资源受控配方，防 OOM）
bazel shutdown >/dev/null 2>&1

passed_rounds=0
total_start=$(date +%s)

for round in $(seq 1 "$rounds"); do
  mem_gate_wait
  log "=== Round ${round}/${rounds} 开始 (mem_avail $(mem_avail_mb)MB) ==="
  rtag=$(printf '%03d' "$round")
  rlog="${run_dir}/round_${rtag}.log"

  r0=$(date +%s)
  timeout -k 60 "$round_timeout" qa/runqa -j "$j" -t "$t" $fail_fast >"$rlog" 2>&1
  rc=$?
  dur=$(( $(date +%s) - r0 ))

  # 每轮快照 qa.log 留档（qa/logs/qa.log 会被下一轮 runqa 重建覆盖）
  [ -f qa/logs/qa.log ] && cp qa/logs/qa.log "${run_dir}/round_${rtag}.qa.log"

  if [ "$rc" -eq 124 ] || [ "$rc" -eq 137 ]; then
    log "!!! Round ${round} 外层超时（>${round_timeout}s）—— runqa 疑似挂死，清理 fly 残留进程"
    pkill -9 -f '[/]build/bin/fly' 2>/dev/null
  fi

  if [ "$rc" -ne 0 ]; then
    failed_n=$(awk '/^Failed:/{print $2}' "$rlog" | head -1)
    log "!!! Round ${round} FAILED (rc=${rc}, failed=${failed_n:-?}, 耗时 $(fmt_dur "$dur")) —— 立即停止，保留失败现场"
    scene="${run_dir}/failure_round_${round}"
    mkdir -p "$scene"
    [ -f qa/logs/qa.log ] && cp qa/logs/qa.log "$scene/qa.log"
    awk '/^Failed tests:/{f=1;next} f && /^  - /{sub(/^  - /,""); print}' "$rlog" |
      while read -r name; do
        [ -n "$name" ] || continue
        stem="${name%.py}"; stem="${stem%.pyt}"
        for d in qa/*/"$stem"; do
          if [ -d "$d" ]; then
            cp -r --parents "$d" "$scene/" && log "  现场已保留: ${d}/"
          fi
        done
      done
    log "--- 失败现场: ${scene}/（qa.log + 失败 case 日志目录）"
    log "--- 分析入口: ${scene}/qa.log（含失败详情与 fly.log 路径）；修复前勿重跑覆盖现场"
    log "=== 稳定性测试失败: ${passed_rounds}/${rounds} 轮通过 ==="
    exit 1
  fi

  case_n=$(awk -F': *' '/^Tests to run:/{print $2}' "$rlog" | head -1)
  pass_n=$(awk '/^Passed:/{print $2}' "$rlog" | head -1)
  fail_n=$(awk '/^Failed:/{print $2}' "$rlog" | head -1)
  passed_rounds=$((passed_rounds + 1))
  total_elapsed=$(( $(date +%s) - total_start ))
  remain=$(( (total_elapsed / passed_rounds) * (rounds - passed_rounds) ))
  log "=== Round ${round}/${rounds} PASSED (${pass_n:-?}/${case_n:-?} cases, failed=${fail_n:-0}, 耗时 $(fmt_dur "$dur")) | 累计 ${passed_rounds}/${rounds}，总耗时 $(fmt_dur "$total_elapsed")，预计剩余 ~$(fmt_dur "$remain") ==="

  [ "$round" -lt "$rounds" ] && sleep 20   # 轮间缓冲（压测资源受控配方）
done

log "=== ALL ${rounds} ROUNDS PASSED（每轮全量 QA 零失败）==="
log "总耗时 $(fmt_dur $(( $(date +%s) - total_start )))，产物: ${run_dir}/"
exit 0
