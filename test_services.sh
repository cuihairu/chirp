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

if [[ "${1:-}" != "--smoke" && "${1:-}" != "--smoke-chat" ]]; then
  exit 0
fi

echo ""
if [[ "${1:-}" == "--smoke" ]]; then
  echo "=== Smoke Test (auth + gateway + clients) ==="
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
