#!/usr/bin/env bash
# 本地一次性阶梯驱动:逐档跑 run_capacity_bench.sh,把摘要汇总到一份文件。
# 互斥与端口预检不在此重复:每档内层 run_capacity_bench.sh 自带 bench_guard.sh
# 两件套(外层再拿同一把 flock 会自己锁死自己),档与档之间由内层串行化。
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../.." || exit 1
OUT="${OUT_DIR:-/home/cui/bench_results}"
mkdir -p "$OUT"
RESULT="$OUT/ladder_summary.txt"
: > "$RESULT"

run_level() {
  local conns="$1" rounds="$2" ramp="$3"
  echo "########## level conns=${conns} rounds=${rounds} ramp_ms=${ramp} ##########" | tee -a "$RESULT"
  echo "loadavg before: $(cut -d' ' -f1-3 /proc/loadavg)" >> "$RESULT"
  BIN_DIR=./build-rel OUT_DIR=/home/cui/bench_results \
    timeout 900 bash tools/benchmark/run_capacity_bench.sh \
    "$conns" "$rounds" 1100 "$ramp" >> "$RESULT" 2>&1
  echo "level_exit=$? loadavg after: $(cut -d' ' -f1-3 /proc/loadavg)" >> "$RESULT"
  echo "" >> "$RESULT"
}

run_level 256   4 2000
run_level 1000  3 5000
run_level 5000  3 20000
run_level 10000 3 30000
run_level 12000 3 30000
echo "LADDER_DONE"
