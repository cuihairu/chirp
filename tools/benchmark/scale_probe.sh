#!/usr/bin/env bash
# 发送路径扩展性:每档 rounds=1(全连接在栅栏后同时首发一次),
# peer-mode=fixed 解开收发放耦,扫连接数看 r0 RTT 随 N 的增长形状。
# 线性 => 每消息 O(N) 扇出;平坦 => 与连接数无关的服务端固定成本。
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../.." || exit 1
OUT="${OUT_DIR:-/home/cui/bench_results_clean}"; mkdir -p "$OUT"
BIN=./build-rel
# 并发/端口守卫(同 bench_guard.sh 头注):与其他基准互斥;17850-17853 被外来进程
# 占住时报错退出——连通性探测会连到对方的服务,数字作废还踩别人。
. "$(dirname "${BASH_SOURCE[0]}")/bench_guard.sh"
bench_acquire_lock || exit 1
bench_preflight_ports 17850 17851 17852 17853 || exit 1
"$BIN/services/shared/chat/chirp_chat" --port 17850 --ws_port 17851 --gateway_service_secret bench-secret > "$OUT/sc_chat.log" 2>&1 &
C=$!
"$BIN/services/game/sdk_gateway/chirp_game_sdk_gateway" --port 17852 --ws_port 17853 --chat_host 127.0.0.1 --chat_port 17850 --chat_service_secret bench-secret > "$OUT/sc_gw.log" 2>&1 &
G=$!
trap 'kill $C $G 2>/dev/null' EXIT
sleep 2
echo "loadavg: $(cut -d' ' -f1-3 /proc/loadavg)"
for n in 100 250 500 1000 2000; do
  out=$("$BIN/tools/benchmark/chirp_load_client" --host 127.0.0.1 --port 17852 \
        --conns "$n" --rounds 1 --interval-ms 1100 --ramp-ms 3000 \
        --peer-mode fixed --prefix "sc${n}_" 2>&1)
  online=$(echo "$out" | grep -oP '并发在线连接: \K[0-9]+' || echo "?")
  r0=$(echo "$out" | grep -oP 'round r0 RTT\(ms\): n=\S+ p50=\K[0-9]+' || echo "-")
  n0=$(echo "$out" | grep -oP 'round r0 RTT\(ms\): n=\K[0-9]+' || echo "-")
  p99=$(echo "$out" | grep -oP 'RTT\(ms\): p50=[0-9]+ p90=[0-9]+ p99=\K[0-9]+' || echo "-")
  echo "conns=${n} 在线=${online} round0: n=${n0} p50=${r0}ms p99=${p99}ms"
done
echo SCALE_DONE
