#!/usr/bin/env bash
# 判定 1000 并发下 25s RTT 的归属:采样 chat / gateway / load_client 三侧 CPU。
# 若服务端 CPU 远低于 100% 而 load_client 接近满核,则瓶颈在压测端(每连接一线程)。
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../.." || exit 1
OUT="${OUT_DIR:-/home/cui/bench_results_clean}"
mkdir -p "$OUT"
CONNS="${1:-1000}"
ROUNDS="${2:-3}"
INTERVAL="${3:-1100}"
RAMP="${4:-5000}"

CHAT_PORT=17810; GW_PORT=17812
BIN=./build-rel
# 并发/端口守卫:与其他基准互斥;17810-17813 被外来进程占住时报错退出,不静默续跑。
. "$(dirname "${BASH_SOURCE[0]}")/bench_guard.sh"
bench_acquire_lock || exit 1
bench_preflight_ports 17810 17811 17812 17813 || exit 1
"${BIN}/services/shared/chat/chirp_chat" --port ${CHAT_PORT} --ws_port 17811 \
  --gateway_service_secret bench-secret > "$OUT/cpu_chat.log" 2>&1 &
CHAT_PID=$!
"${BIN}/services/game/sdk_gateway/chirp_game_sdk_gateway" --port ${GW_PORT} \
  --ws_port 17813 --chat_host 127.0.0.1 --chat_port ${CHAT_PORT} \
  --chat_service_secret bench-secret > "$OUT/cpu_gw.log" 2>&1 &
GW_PID=$!
sleep 2

"${BIN}/tools/benchmark/chirp_load_client" --host 127.0.0.1 --port ${GW_PORT} \
  --conns "${CONNS}" --rounds "${ROUNDS}" --interval-ms "${INTERVAL}" \
  --ramp-ms "${RAMP}" --prefix "cpu${CONNS}_" > "$OUT/cpu_load.log" 2>&1 &
LOAD_PID=$!

# 采样 40s:每 2s 读一次 /proc/<pid>/stat 的 utime+stime,换算成核数占比
read -r _ _ _ _ _ _ _ _ _ _ _ _ _ _ u0 s0 _ < /proc/${CHAT_PID}/stat
CU0=$((u0+s0)); GU0=$(awk '{print $14+$15}' /proc/${GW_PID}/stat)
LU0=$(awk '{print $14+$15}' /proc/${LOAD_PID}/stat)
CLK=$(getconf CLK_TCK)
sleep 40
read -r _ _ _ _ _ _ _ _ _ _ _ _ _ _ u1 s1 _ < /proc/${CHAT_PID}/stat
CU1=$((u1+s1)); GU1=$(awk '{print $14+$15}' /proc/${GW_PID}/stat)
LU1=$(awk '{print $14+$15}' /proc/${LOAD_PID}/stat)
ELAPSED=40
echo "CPU 核数占比(采样 ${ELAPSED}s,CLK_TCK=${CLK}):"
awk -v c=$((CU1-CU0)) -v g=$((GU1-GU0)) -v l=$((LU1-LU0)) -v t=${ELAPSED} -v k=${CLK} \
  'BEGIN{printf "  chat=%.2f cores  gateway=%.2f cores  load_client=%.2f cores  (nproc=14)\n", c/t/k, g/t/k, l/t/k}'
wait ${LOAD_PID}
echo "--- load_client 结果 ---"
tail -n 14 "$OUT/cpu_load.log"
kill ${CHAT_PID} ${GW_PID} 2>/dev/null
echo CPU_PROBE_DONE
