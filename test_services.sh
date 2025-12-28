#!/bin/bash

# 测试服务启动脚本

set -euo pipefail

echo "=== Chirp 服务启动测试 ==="
echo ""

# 检查构建产物是否存在
if [ ! -f "./build/services/gateway/chirp_gateway" ]; then
  echo "错误: chirp_gateway 未构建"
  exit 1
fi

if [ ! -f "./build/services/auth/chirp_auth" ]; then
  echo "错误: chirp_auth 未构建"
  exit 1
fi

if [ ! -f "./build/services/chat/chirp_chat" ]; then
  echo "错误: chirp_chat 未构建"
  exit 1
fi

echo "✓ 所有服务已构建"
echo ""
echo "构建产物:"
ls -lh ./build/services/*/chirp_*
echo ""
echo "测试工具:"
ls -lh ./build/tools/benchmark/chirp_*
echo ""
echo "=== 测试完成 ==="
echo ""
echo "要运行服务，执行:"
echo "  ./build/services/gateway/chirp_gateway --port 5000 --ws_port 5001"
echo "  ./build/services/auth/chirp_auth --port 6000"
echo "  ./build/services/chat/chirp_chat --port 7000"

if [[ "${1:-}" != "--smoke" && "${1:-}" != "--smoke-chat" && "${1:-}" != "--smoke-redis" ]]; then
  exit 0
fi

echo ""
if [[ "${1:-}" == "--smoke" ]]; then
  echo "=== Smoke Test (auth + gateway + clients) ==="
elif [[ "${1:-}" == "--smoke-redis" ]]; then
  echo "=== Smoke Test (redis distributed sessions + cross-instance kick) ==="
else
  echo "=== Smoke Test (chat + clients) ==="
fi

pick_port() {
  python3 - <<'PY'
import socket
s = socket.socket()
s.bind(("127.0.0.1", 0))
print(s.getsockname()[1])
s.close()
PY
}

if [[ "${1:-}" == "--smoke" ]]; then
  AUTH_PORT="${AUTH_PORT:-$(pick_port)}"
  GW_PORT="${GW_PORT:-$(pick_port)}"
  WS_PORT="${WS_PORT:-$(pick_port)}"

  AUTH_LOG="${AUTH_LOG:-/tmp/chirp_auth_smoke.log}"
  GW_LOG="${GW_LOG:-/tmp/chirp_gateway_smoke.log}"

  ./build/services/auth/chirp_auth --port "${AUTH_PORT}" --jwt_secret dev_secret > "${AUTH_LOG}" 2>&1 &
  AUTH_PID=$!

  ./build/services/gateway/chirp_gateway --port "${GW_PORT}" --ws_port "${WS_PORT}" \
    --auth_host 127.0.0.1 --auth_port "${AUTH_PORT}" > "${GW_LOG}" 2>&1 &
  GW_PID=$!

  cleanup() {
    kill -TERM "${GW_PID}" "${AUTH_PID}" 2>/dev/null || true
    wait "${GW_PID}" 2>/dev/null || true
    wait "${AUTH_PID}" 2>/dev/null || true
  }
  trap cleanup EXIT

  sleep 0.3

  echo ""
  echo "[tcp] login -> ping"
  ./build/tools/benchmark/chirp_login_client --host 127.0.0.1 --port "${GW_PORT}" --token user_1 --device dev_a --platform pc

  echo ""
  echo "[ws] login -> ping"
  ./build/tools/benchmark/chirp_ws_login_client --host 127.0.0.1 --port "${WS_PORT}" --token user_1 --device dev_b --platform web

  echo ""
  echo "auth log: ${AUTH_LOG}"
  tail -n 5 "${AUTH_LOG}" || true
  echo ""
  echo "gateway log: ${GW_LOG}"
  tail -n 20 "${GW_LOG}" || true
elif [[ "${1:-}" == "--smoke-redis" ]]; then
  if ! command -v docker >/dev/null 2>&1; then
    echo "错误: 未找到 docker，无法运行 --smoke-redis"
    exit 1
  fi
  if ! docker info >/dev/null 2>&1; then
    echo "错误: 无法连接 Docker daemon，请先启动 Docker Desktop（或确保 docker daemon 在运行）"
    exit 1
  fi

  AUTH_PORT="${AUTH_PORT:-$(pick_port)}"
  REDIS_PORT="${REDIS_PORT:-$(pick_port)}"
  GW1_PORT="${GW1_PORT:-$(pick_port)}"
  GW2_PORT="${GW2_PORT:-$(pick_port)}"
  WS1_PORT="${WS1_PORT:-$(pick_port)}"
  WS2_PORT="${WS2_PORT:-$(pick_port)}"

  AUTH_LOG="${AUTH_LOG:-/tmp/chirp_auth_smoke_redis.log}"
  GW1_LOG="${GW1_LOG:-/tmp/chirp_gateway1_smoke_redis.log}"
  GW2_LOG="${GW2_LOG:-/tmp/chirp_gateway2_smoke_redis.log}"
  CLIENT1_LOG="${CLIENT1_LOG:-/tmp/chirp_client_hold_smoke_redis.log}"
  WS_CLIENT1_LOG="${WS_CLIENT1_LOG:-/tmp/chirp_ws_client_hold_smoke_redis.log}"

  REDIS_CONTAINER="${REDIS_CONTAINER:-chirp_redis_smoke_$$}"

  docker run --rm -d --name "${REDIS_CONTAINER}" -p "127.0.0.1:${REDIS_PORT}:6379" redis:7-alpine >/dev/null

  cleanup() {
    kill -TERM "${GW1_PID:-}" "${GW2_PID:-}" "${AUTH_PID:-}" 2>/dev/null || true
    wait "${GW1_PID:-}" 2>/dev/null || true
    wait "${GW2_PID:-}" 2>/dev/null || true
    wait "${AUTH_PID:-}" 2>/dev/null || true
    docker rm -f "${REDIS_CONTAINER}" >/dev/null 2>&1 || true
  }
  trap cleanup EXIT

  for _ in {1..50}; do
    if docker exec "${REDIS_CONTAINER}" redis-cli ping >/dev/null 2>&1; then
      break
    fi
    sleep 0.1
  done

  ./build/services/auth/chirp_auth --port "${AUTH_PORT}" --jwt_secret dev_secret > "${AUTH_LOG}" 2>&1 &
  AUTH_PID=$!

  ./build/services/gateway/chirp_gateway --port "${GW1_PORT}" --ws_port "${WS1_PORT}" \
    --auth_host 127.0.0.1 --auth_port "${AUTH_PORT}" \
    --redis_host 127.0.0.1 --redis_port "${REDIS_PORT}" --redis_ttl 3600 --instance_id gw_a > "${GW1_LOG}" 2>&1 &
  GW1_PID=$!

  ./build/services/gateway/chirp_gateway --port "${GW2_PORT}" --ws_port "${WS2_PORT}" \
    --auth_host 127.0.0.1 --auth_port "${AUTH_PORT}" \
    --redis_host 127.0.0.1 --redis_port "${REDIS_PORT}" --redis_ttl 3600 --instance_id gw_b > "${GW2_LOG}" 2>&1 &
  GW2_PID=$!

  sleep 0.4

  echo ""
  echo "[tcp] hold login on gw_a (expect kick)"
  ./build/tools/benchmark/chirp_login_client --host 127.0.0.1 --port "${GW1_PORT}" \
    --token user_1 --device dev_a --platform pc --wait_kick_ms 5000 > "${CLIENT1_LOG}" 2>&1 &
  CLIENT1_PID=$!

  sleep 0.4

  echo ""
  echo "[tcp] login on gw_b (should kick gw_a)"
  ./build/tools/benchmark/chirp_login_client --host 127.0.0.1 --port "${GW2_PORT}" --token user_1 --device dev_b --platform pc

  set +e
  wait "${CLIENT1_PID}"
  CLIENT1_RC=$?
  set -e
  if [[ "${CLIENT1_RC}" != "0" ]]; then
    echo ""
    echo "client hold did not observe kick (rc=${CLIENT1_RC})"
    cat "${CLIENT1_LOG}" || true
    exit 1
  fi

  echo ""
  echo "[ws] hold login on gw_a (expect kick)"
  ./build/tools/benchmark/chirp_ws_login_client --host 127.0.0.1 --port "${WS1_PORT}" \
    --token user_2 --device dev_a --platform web --wait_kick_ms 5000 > "${WS_CLIENT1_LOG}" 2>&1 &
  WS_CLIENT1_PID=$!

  sleep 0.4

  echo ""
  echo "[ws] login on gw_b (should kick gw_a)"
  ./build/tools/benchmark/chirp_ws_login_client --host 127.0.0.1 --port "${WS2_PORT}" --token user_2 --device dev_b --platform web

  set +e
  wait "${WS_CLIENT1_PID}"
  WS_CLIENT1_RC=$?
  set -e
  if [[ "${WS_CLIENT1_RC}" != "0" ]]; then
    echo ""
    echo "ws client hold did not observe kick (rc=${WS_CLIENT1_RC})"
    cat "${WS_CLIENT1_LOG}" || true
    exit 1
  fi

  echo ""
  echo "client hold log: ${CLIENT1_LOG}"
  tail -n 20 "${CLIENT1_LOG}" || true
  echo ""
  echo "ws client hold log: ${WS_CLIENT1_LOG}"
  tail -n 20 "${WS_CLIENT1_LOG}" || true
  echo ""
  echo "gateway1 log: ${GW1_LOG}"
  tail -n 30 "${GW1_LOG}" || true
  echo ""
  echo "gateway2 log: ${GW2_LOG}"
  tail -n 30 "${GW2_LOG}" || true
  echo ""
  echo "auth log: ${AUTH_LOG}"
  tail -n 10 "${AUTH_LOG}" || true
else
  CHAT_PORT="${CHAT_PORT:-$(pick_port)}"
  CHAT_LOG="${CHAT_LOG:-/tmp/chirp_chat_smoke.log}"
  LISTEN_LOG="${LISTEN_LOG:-/tmp/chirp_chat_listen_smoke.log}"

  ./build/services/chat/chirp_chat --port "${CHAT_PORT}" > "${CHAT_LOG}" 2>&1 &
  CHAT_PID=$!

  cleanup() {
    kill -TERM "${CHAT_PID}" 2>/dev/null || true
    wait "${CHAT_PID}" 2>/dev/null || true
  }
  trap cleanup EXIT

  sleep 0.3

  echo ""
  echo "[tcp] listen user_2 (1 msg)"
  ./build/tools/benchmark/chirp_chat_listen_client --host 127.0.0.1 --port "${CHAT_PORT}" --user user_2 --max 1 > "${LISTEN_LOG}" 2>&1 &
  LISTEN_PID=$!

  sleep 0.2

  echo ""
  echo "[tcp] send user_1 -> user_2"
  ./build/tools/benchmark/chirp_chat_send_client --host 127.0.0.1 --port "${CHAT_PORT}" --sender user_1 --receiver user_2 --text "hello"

  wait "${LISTEN_PID}"
  cat "${LISTEN_LOG}" || true

  echo ""
  echo "[tcp] history private (user_1|user_2)"
  ./build/tools/benchmark/chirp_chat_history_client --host 127.0.0.1 --port "${CHAT_PORT}" --user user_1 --channel_type 0 --channel_id "user_1|user_2" --limit 10

  echo ""
  echo "chat log: ${CHAT_LOG}"
  tail -n 20 "${CHAT_LOG}" || true
fi

echo ""
echo "=== Smoke Test Done ==="
