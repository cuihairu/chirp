#!/bin/bash

# Web companion E2E smoke: start a real chirp_chat and chirp_social (both
# in-memory mode) on free ports, then run the vitest integration suites
# against their websocket entries. Mirrors the orchestration conventions of
# the repo-root test_services.sh.

set -euo pipefail

cd "$(dirname "$0")/../../.."

CHAT_BIN="${CHAT_BIN:-./build/services/chat/chirp_chat}"
SOCIAL_BIN="${SOCIAL_BIN:-./build/services/social/chirp_social}"
# CHAT_BIN/SOCIAL_BIN are overridable: in a tree where vcpkg provided MySQL the
# default chirp_chat is the enhanced build and dies without a real MySQL
# server. Point them at a basic-form build (configure another tree with
# -DCMAKE_DISABLE_FIND_PACKAGE_MySQL=TRUE) to run fully in-memory.
if [ ! -f "${CHAT_BIN}" ]; then
  echo "错误: chirp_chat 未构建 (先 cmake --build build)"
  exit 1
fi
if [ ! -f "${SOCIAL_BIN}" ]; then
  echo "错误: chirp_social 未构建 (先 cmake --build build)"
  exit 1
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

wait_port() {
  local port="$1" name="$2" log="$3" timeout_s="${4:-15}"
  local deadline=$((SECONDS + timeout_s))
  while (( SECONDS < deadline )); do
    if (exec 3<>"/dev/tcp/127.0.0.1/${port}") 2>/dev/null; then
      return 0
    fi
    sleep 0.1
  done
  echo "错误: ${name} 端口 ${port} 在 ${timeout_s}s 内未就绪"
  if [[ -f "${log}" ]]; then
    echo "--- ${name} log: ${log} ---"
    tail -n 40 "${log}" || true
  fi
  return 1
}

stop_proc() {
  local pid
  for pid in "$@"; do
    [[ -z "${pid}" ]] && continue
    if kill -TERM "${pid}" 2>/dev/null; then
      sleep 2
      kill -KILL "${pid}" 2>/dev/null || true
    fi
  done
}

CHAT_PORT="${CHAT_PORT:-$(pick_port)}"
CHAT_WS_PORT="${CHAT_WS_PORT:-$(pick_port)}"
CHAT_LOG="${CHAT_LOG:-/tmp/chirp_web_smoke_chat.log}"
SOCIAL_PORT="${SOCIAL_PORT:-$(pick_port)}"
SOCIAL_WS_PORT="${SOCIAL_WS_PORT:-$(pick_port)}"
SOCIAL_LOG="${SOCIAL_LOG:-/tmp/chirp_web_smoke_social.log}"

"${CHAT_BIN}" --port "${CHAT_PORT}" --ws_port "${CHAT_WS_PORT}" \
  > "${CHAT_LOG}" 2>&1 &
CHAT_PID=$!

# Defaults its tcp port to ws-1; pass both explicitly to keep them off the
# chat service's picked ports.
"${SOCIAL_BIN}" --port "${SOCIAL_PORT}" --ws_port "${SOCIAL_WS_PORT}" \
  > "${SOCIAL_LOG}" 2>&1 &
SOCIAL_PID=$!

cleanup() {
  stop_proc "${CHAT_PID}" "${SOCIAL_PID}"
}
trap cleanup EXIT

wait_port "${CHAT_PORT}" chirp_chat "${CHAT_LOG}"
wait_port "${CHAT_WS_PORT}" chirp_chat_ws "${CHAT_LOG}"
wait_port "${SOCIAL_PORT}" chirp_social "${SOCIAL_LOG}"
wait_port "${SOCIAL_WS_PORT}" chirp_social_ws "${SOCIAL_LOG}"

echo "[web] vitest integration against ws://127.0.0.1:${CHAT_WS_PORT} + ws://127.0.0.1:${SOCIAL_WS_PORT}"
CHIRP_WS_URL="ws://127.0.0.1:${CHAT_WS_PORT}" \
CHIRP_SOCIAL_WS_URL="ws://127.0.0.1:${SOCIAL_WS_PORT}" \
npm --prefix apps/web_companion run test:integration

echo ""
echo "--- chat log tail (${CHAT_LOG}) ---"
tail -n 12 "${CHAT_LOG}" || true
echo ""
echo "--- social log tail (${SOCIAL_LOG}) ---"
tail -n 12 "${SOCIAL_LOG}" || true
echo ""
echo "=== Web Smoke Done ==="
