#!/usr/bin/env bash
# 发送路径阻塞定位:跑 1000 并发发送,期间采样 chat 进程全部线程的
# wchan(内核阻塞点)与 syscall,统计聚集在哪。
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../.." || exit 1
OUT="${OUT_DIR:-/home/cui/bench_results_clean}"; mkdir -p "$OUT"
BIN=./build-rel
# 并发/端口守卫:与其他基准互斥;17830-17833 被外来进程占住时报错退出,不静默续跑。
. "$(dirname "${BASH_SOURCE[0]}")/bench_guard.sh"
bench_acquire_lock || exit 1
bench_preflight_ports 17830 17831 17832 17833 || exit 1
"$BIN/services/shared/chat/chirp_chat" --port 17830 --ws_port 17831 --gateway_service_secret bench-secret > "$OUT/st_chat.log" 2>&1 &
C=$!
"$BIN/services/game/sdk_gateway/chirp_game_sdk_gateway" --port 17832 --ws_port 17833 --chat_host 127.0.0.1 --chat_port 17830 --chat_service_secret bench-secret > "$OUT/st_gw.log" 2>&1 &
G=$!
sleep 2
"$BIN/tools/benchmark/chirp_load_client" --host 127.0.0.1 --port 17832 --conns 1000 --rounds 3 \
  --interval-ms 1100 --ramp-ms 3000 --peer-mode fixed --prefix st_ > "$OUT/st_load.log" 2>&1 &
L=$!
sleep 8
echo "=== chat 线程 wchan 聚集 (t=8s) ==="
for t in /proc/$C/task/*; do
  read -r w < "$t/wchan" 2>/dev/null && echo "${w:-running}"
done | sort | uniq -c | sort -rn | head -12
echo "=== chat 线程 syscall 聚集 ==="
for t in /proc/$C/task/*; do
  read -r s < "$t/syscall" 2>/dev/null && echo "${s%% *}"
done | sort | uniq -c | sort -rn | head -8
echo "=== 线程总数 ==="
ls -d /proc/$C/task/* | wc -l
echo "=== 进程状态 ==="
grep -E "^State|^Threads" /proc/$C/status
wait $L
kill $C $G 2>/dev/null
echo STACK_PROBE_DONE
