#!/usr/bin/env bash
# 可持续建连速率标定:固定 2000 连接,扫 ramp 窗口(= offered 建连速率),
# 看网关→chat 桥接握手(5s 预算)从哪一档开始超时。
# 判据:传输失败数(= 网关日志 handshake timed out 条数)与登录 RTT 分位。
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../.." || exit 1
OUT="${OUT_DIR:-/home/cui/bench_results_clean}"; mkdir -p "$OUT"
BIN=./build-rel
# 并发/端口守卫:与其他基准互斥;17870-17873 被外来进程占住时报错退出,不静默续跑。
. "$(dirname "${BASH_SOURCE[0]}")/bench_guard.sh"
bench_acquire_lock || exit 1
bench_preflight_ports 17870 17871 17872 17873 || exit 1
"$BIN/services/shared/chat/chirp_chat" --port 17870 --ws_port 17871 --gateway_service_secret bench-secret > "$OUT/est_chat.log" 2>&1 &
C=$!
"$BIN/services/game/sdk_gateway/chirp_game_sdk_gateway" --port 17872 --ws_port 17873 --chat_host 127.0.0.1 --chat_port 17870 --chat_service_secret bench-secret > "$OUT/est_gw.log" 2>&1 &
G=$!
trap 'kill $C $G 2>/dev/null' EXIT
sleep 3
echo "loadavg start: $(cut -d' ' -f1-3 /proc/loadavg)"
N=2000
# ramp 窗口越小 = 建连速率越高
for ramp in 20000 10000 6000 4000 3000; do
  : > "$OUT/est_gw.log"
  out=$("$BIN/tools/benchmark/chirp_load_client" --host 127.0.0.1 --port 17872 \
        --conns "$N" --rounds 0 --ramp-ms "$ramp" --prefix "est${ramp}_" 2>&1)
  online=$(echo "$out" | grep -oP '并发在线连接: \K[0-9]+')
  fails=$(echo "$out" | grep -oP '传输失败=\K[0-9]+' || echo 0)
  lp50=$(echo "$out" | grep -oP '登录RTT\(ms\): p50=\K[0-9]+')
  lp99=$(echo "$out" | grep -oP '登录RTT\(ms\): p50=[0-9]+ p90=[0-9]+ p99=\K[0-9]+')
  lmax=$(echo "$out" | grep -oP '登录RTT\(ms\): p50=[0-9]+ p90=[0-9]+ p99=[0-9]+ max=\K[0-9]+')
  hs=$(grep -c "handshake timed out" "$OUT/est_gw.log" 2>/dev/null || echo 0)
  rate=$(awk -v n="$N" -v r="$ramp" 'BEGIN{printf "%.0f", n*1000/r}')
  echo "rate=${rate}conn/s (ramp=${ramp}ms) 在线=${online}/${N} 失败=${fails} 握手超时=${hs} 登录RTT p50=${lp50} p99=${lp99} max=${lmax}"
done
echo "loadavg end: $(cut -d' ' -f1-3 /proc/loadavg)"
echo EST_RATE_DONE
