#!/usr/bin/env bash
# 纯连接容量阶梯:rounds=0,只量「N 条连接同时在线」与建连速率。
# 互斥与端口预检由内层 run_capacity_bench.sh 的 bench_guard.sh 承担(外层重复
# 拿同一把 flock 会自锁)。
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../.." || exit 1
OUT="${OUT_DIR:-/home/cui/bench_results}"
RESULT="$OUT/capacity_ladder.txt"
: > "$RESULT"
for spec in "5000 25000" "10000 50000" "12000 60000"; do
  set -- $spec
  conns="$1"; ramp="$2"
  echo "########## capacity conns=${conns} ramp_ms=${ramp} rounds=0 ##########" >> "$RESULT"
  echo "loadavg before: $(cut -d' ' -f1-3 /proc/loadavg)" >> "$RESULT"
  BIN_DIR=./build-rel OUT_DIR="$OUT" \
    timeout 600 bash tools/benchmark/run_capacity_bench.sh \
    "$conns" 0 1100 "$ramp" >> "$RESULT" 2>&1
  echo "level_exit=$? loadavg after: $(cut -d' ' -f1-3 /proc/loadavg)" >> "$RESULT"
  echo "" >> "$RESULT"
done
echo CAPACITY_LADDER_DONE
