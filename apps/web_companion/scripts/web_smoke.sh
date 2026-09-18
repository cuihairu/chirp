#!/bin/bash

# Web companion E2E smoke: start a real chirp_chat (in-memory mode) on free
# ports, then run the vitest integration suite against its websocket entry.
# Mirrors the orchestration conventions of the repo-root test_services.sh.

set -euo pipefail

cd "$(dirname "$0")/../../.."

CHAT_BIN="./build/services/chat/chirp_chat"
if [ ! -f "${CHAT_BIN}" ]; then
  echo "错误: chirp_chat 未构建 (先 cmake --build build)"
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

./build/services/chat/chirp_chat --port "${CHAT_PORT}" --ws_port "${CHAT_WS_PORT}" \
  > "${CHAT_LOG}" 2>&1 &
CHAT_PID=$!

cleanup() {
  stop_proc "${CHAT_PID}"
}
trap cleanup EXIT

wait_port "${CHAT_PORT}" chirp_chat "${CHAT_LOG}"
wait_port "${CHAT_WS_PORT}" chirp_chat_ws "${CHAT_LOG}"

echo "[web] vitest integration against ws://127.0.0.1:${CHAT_WS_PORT}"
CHIRP_WS_URL="ws://127.0.0.1:${CHAT_WS_PORT}" npm --prefix apps/web_companion run test:integration

echo ""
echo "--- chat log tail (${CHAT_LOG}) ---"
tail -n 12 "${CHAT_LOG}" || true
echo ""
echo "=== Web Smoke Done ==="
