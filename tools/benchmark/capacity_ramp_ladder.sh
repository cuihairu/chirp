#!/usr/bin/env bash
# 容量阶梯(带铺开建连):每档先纯连接(rounds=0)量「N 条同时在线」,
# 再带少量轮次量稳态 RTT/吞吐。ramp_ms 铺开建连,避免同瞬握手洪峰。
#
# 用法: BIN_DIR=./build-rel OUT_DIR=/home/cui/bench_results \
#         bash tools/benchmark/capacity_ramp_ladder.sh
# 互斥与端口预检由内层 run_capacity_bench.sh 的 bench_guard.sh 承担(外层重复
# 拿同一把 flock 会自锁);下面的忙闲检查是「档位级」的机器负载守卫,与锁互补。
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../.." || exit 1
OUT="${OUT_DIR:-/home/cui/bench_results}"
mkdir -p "$OUT"
RESULT="$OUT/ramp_ladder.txt"
: > "$RESULT"

run_level() {
  local conns="$1" rounds="$2" ramp="$3" ivl="$4"
  {
    echo "########## conns=${conns} rounds=${rounds} ramp_ms=${ramp} ##########"
    echo "loadavg before: $(cut -d' ' -f1-3 /proc/loadavg)"
  } >> "$RESULT"
  # 争用守卫:本机常有别的会话同时跑基准/构建。压测数字只有在 CPU 相对空闲时
  # 才可归因,否则宁可这一档作废重来,也不能把污染的数字写进文档。
  # 用法:SKIP_BUSY=1 时忙则跳过该档(默认);SKIP_BUSY=0 时只记录照跑。
  local load1 foreign
  load1=$(cut -d' ' -f1 /proc/loadavg)
  foreign=$(pgrep -f 'chirp_load_client|chirp_game_sdk_gateway|chirp_chat --port' |
            grep -v "^$$\$" | wc -l)
  {
    echo "争用检查: load1=${load1} 疑似外来压测进程=${foreign}"
    if [ "${load1%%.*}" -gt "${MAX_LOAD1:-20}" ] || [ "${foreign}" -gt 0 ]; then
      echo "判定: 机器繁忙,本档数字不作数"
    else
      echo "判定: 机器空闲,可归因"
    fi
  } >> "$RESULT"
  if [ "${SKIP_BUSY:-1}" = "1" ] &&
     { [ "${load1%%.*}" -gt "${MAX_LOAD1:-20}" ] || [ "${foreign}" -gt 0 ]; }; then
    echo "SKIPPED conns=${conns}(机器繁忙,load1=${load1} foreign=${foreign})" >> "$RESULT"
    echo "" >> "$RESULT"
    return 0
  fi
  BIN_DIR="${BIN_DIR:-./build-rel}" OUT_DIR="${OUT}" \
    timeout 900 bash tools/benchmark/run_capacity_bench.sh \
    "${conns}" "${rounds}" "${ivl}" "${ramp}" >> "$RESULT" 2>&1
  {
    echo "level_exit=$? loadavg after: $(cut -d' ' -f1-3 /proc/loadavg)"
    echo ""
  } >> "$RESULT"
}

# 纯连接容量档:5000 / 10000 / 12000
run_level 5000  0 25000 1100
run_level 10000 0 50000 1100
run_level 12000 0 60000 1100
# 稳态 RTT + 吞吐档
run_level 1000  3 5000  1100
run_level 5000  3 20000 1100
run_level 10000 3 40000 1100
# 同瞬建连档(ramp=0):定位握手墙——网关到 chat 每客户端一条管道且握手串行,
# 这几档量的是「一次性冲击能建立多少连接」,与上面的「已建立并发在线」是两个口径。
run_level 1000  0 0 1100
run_level 2000  0 0 1100
run_level 4000  0 0 1100
run_level 8000  0 0 1100
echo RAMP_LADDER_DONE
