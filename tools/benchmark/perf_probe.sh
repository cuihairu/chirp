#!/usr/bin/env bash
# perf 采样 chat 单线程在 1000 并发发送下的 CPU 热点。
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../.." || exit 1
OUT="${OUT_DIR:-/home/cui/bench_results_clean}"; mkdir -p "$OUT"
BIN=./build-rel
# 并发/端口守卫;本脚本自己不留 trap(中途退出会漏下 chat/gateway 孤儿)——守卫的
# 端口预检因此比别的脚本更关键:孤儿占住 17840-17843 时,只有 FORCE=1 且确认是
# 本用户基准二进制才允许清理。
. "$(dirname "${BASH_SOURCE[0]}")/bench_guard.sh"
bench_acquire_lock || exit 1
bench_preflight_ports 17840 17841 17842 17843 || exit 1
"$BIN/services/shared/chat/chirp_chat" --port 17840 --ws_port 17841 --gateway_service_secret bench-secret > "$OUT/pf2_chat.log" 2>&1 &
C=$!
"$BIN/services/game/sdk_gateway/chirp_game_sdk_gateway" --port 17842 --ws_port 17843 --chat_host 127.0.0.1 --chat_port 17840 --chat_service_secret bench-secret > "$OUT/pf2_gw.log" 2>&1 &
G=$!
sleep 2
perf record -F 999 -g -p ${C} -o "$OUT/chat.perf" -- sleep 40 >/dev/null 2>&1 &
P=$!
"$BIN/tools/benchmark/chirp_load_client" --host 127.0.0.1 --port 17842 --conns 800 --rounds 3 \
  --interval-ms 1100 --ramp-ms 3000 --peer-mode fixed --prefix pf2_ > "$OUT/pf2_load.log" 2>&1 &
L=$!
wait $P
wait $L
kill $C $G 2>/dev/null
echo "=== perf report (self, top 25) ==="
perf report -i "$OUT/chat.perf" --stdio --no-children -g none 2>/dev/null | head -40
echo PERF_DONE
