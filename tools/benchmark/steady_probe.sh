#!/usr/bin/env bash
# 稳态发送延迟/吞吐:barrier=off(保持 ramp 相位,不制造惊群),环模式真实在线对端。
# 每档 5 轮 × interval 1100ms,报窗口吞吐与 RTT 分位。
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../.." || exit 1
OUT="${OUT_DIR:-/home/cui/bench_results_clean}"; mkdir -p "$OUT"
BIN=./build-rel
# 并发/端口守卫:与其他基准互斥;17880-17883 被占(尤其被别的会话的活体服务占住)
# 时报错退出,不静默抢端口——探测会连到对方进程上,数字作废。
. "$(dirname "${BASH_SOURCE[0]}")/bench_guard.sh"
bench_acquire_lock || exit 1
bench_preflight_ports 17880 17881 17882 17883 || exit 1
"$BIN/services/shared/chat/chirp_chat" --port 17880 --ws_port 17881 --gateway_service_secret bench-secret > "$OUT/ss_chat.log" 2>&1 &
C=$!
"$BIN/services/game/sdk_gateway/chirp_game_sdk_gateway" --port 17882 --ws_port 17883 --chat_host 127.0.0.1 --chat_port 17880 --chat_service_secret bench-secret > "$OUT/ss_gw.log" 2>&1 &
G=$!
trap 'kill $C $G 2>/dev/null' EXIT
sleep 3
echo "loadavg start: $(cut -d' ' -f1-3 /proc/loadavg)"
for n in 100 500 1000 2000; do
  # ramp 铺 2 倍发送窗口,保证各连接相位稳定错开
  echo "--- conns=${n} rounds=5 barrier=off ---"
  "$BIN/tools/benchmark/chirp_load_client" --host 127.0.0.1 --port 17882 \
    --conns "$n" --rounds 5 --interval-ms 1100 --ramp-ms $((n*11)) \
    --barrier off --prefix "ss${n}_" 2>&1 | grep -vE "^  \[conn\]|^最慢|^  conn#" | tail -14
  echo "    loadavg now: $(cut -d' ' -f1-3 /proc/loadavg)"
done
echo "loadavg end: $(cut -d' ' -f1-3 /proc/loadavg)"
echo STEADY_DONE
