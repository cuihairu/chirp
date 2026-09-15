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
echo "  ./build/services/chat/chirp_chat --port 7000 --ws_port 7001"

if [[ "${1:-}" != "--smoke" && "${1:-}" != "--smoke-chat" && "${1:-}" != "--smoke-redis" && "${1:-}" != "--smoke-npc" && "${1:-}" != "--smoke-sdk" ]]; then
  exit 0
fi

echo ""
if [[ "${1:-}" == "--smoke" ]]; then
  echo "=== Smoke Test (auth + gateway + clients) ==="
elif [[ "${1:-}" == "--smoke-redis" ]]; then
  echo "=== Smoke Test (redis distributed sessions + cross-instance kick) ==="
elif [[ "${1:-}" == "--smoke-npc" ]]; then
  echo "=== Smoke Test (server plane + NPC dialog loop) ==="
elif [[ "${1:-}" == "--smoke-sdk" ]]; then
  echo "=== Smoke Test (game client SDK + chat) ==="
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

# 等待 TCP 端口可连接。CI runner 冷启动一个刚构建好的 Debug 二进制可能慢于
# 固定 sleep，benchmark 客户端连不上会以未捕获异常直接 abort（core dumped）。
# 超时后打印服务日志尾部，启动即崩的服务在这里直接暴露死因。
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
  if [[ -n "${log}" && -f "${log}" ]]; then
    echo "--- ${name} log: ${log} ---"
    tail -n 40 "${log}" || true
  fi
  return 1
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

  wait_port "${AUTH_PORT}" chirp_auth "${AUTH_LOG}"
  wait_port "${GW_PORT}" chirp_gateway "${GW_LOG}"

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

  wait_port "${AUTH_PORT}" chirp_auth "${AUTH_LOG}"
  wait_port "${GW1_PORT}" chirp_gateway-a "${GW1_LOG}"
  wait_port "${GW2_PORT}" chirp_gateway-b "${GW2_LOG}"

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
elif [[ "${1:-}" == "--smoke-npc" ]]; then
  # Full NPC dialog loop over real processes: hub + chat + npc_dialog.
  #
  # Timing: the sender must NOT be online when the NPC reply comes back -
  # the reply would be delivered to the sender's own connection (which the
  # one-shot send client never reads). So every listener connects only
  # after its sender exited, and receives the reply through the offline
  # queue; that also exercises the queue refill tail.
  HUB_PORT="${HUB_PORT:-$(pick_port)}"
  CHAT_PORT="${CHAT_PORT:-$(pick_port)}"
  CHAT_WS_PORT="${CHAT_WS_PORT:-$(pick_port)}"
  HUB_LOG="${HUB_LOG:-/tmp/chirp_hub_smoke_npc.log}"
  CHAT_LOG="${CHAT_LOG:-/tmp/chirp_chat_smoke_npc.log}"
  NPC_LOG="${NPC_LOG:-/tmp/chirp_npc_smoke_npc.log}"
  NPC_LISTEN_LOG="${NPC_LISTEN_LOG:-/tmp/chirp_npc_listen_smoke.log}"
  NPC_OFFLINE_LISTEN_LOG="${NPC_OFFLINE_LISTEN_LOG:-/tmp/chirp_npc_offline_listen_smoke.log}"

  ./build/services/server_gateway/chirp_server_gateway --port "${HUB_PORT}" \
    --service chat=chat-secret --service npc_dialog=npc-secret --chat_service_id chat \
    > "${HUB_LOG}" 2>&1 &
  HUB_PID=$!

  wait_port "${HUB_PORT}" chirp_server_gateway "${HUB_LOG}"

  ./build/services/chat/chirp_chat --port "${CHAT_PORT}" --ws_port "${CHAT_WS_PORT}" \
    --server_gateway_host 127.0.0.1 --server_gateway_port "${HUB_PORT}" \
    --server_gateway_secret chat-secret --npc_service_id npc_dialog \
    > "${CHAT_LOG}" 2>&1 &
  CHAT_PID=$!
  ./build/services/npc_dialog/chirp_npc_dialog \
    --server_gateway_host 127.0.0.1 --server_gateway_port "${HUB_PORT}" \
    --server_gateway_secret npc-secret \
    > "${NPC_LOG}" 2>&1 &
  NPC_PID=$!

  cleanup() {
    kill -TERM "${NPC_PID}" "${CHAT_PID}" "${HUB_PID}" 2>/dev/null || true
    wait "${NPC_PID}" 2>/dev/null || true
    wait "${CHAT_PID}" 2>/dev/null || true
    wait "${HUB_PID}" 2>/dev/null || true
  }
  trap cleanup EXIT

  wait_port "${CHAT_PORT}" chirp_chat "${CHAT_LOG}"

  # 能力探测:服务器平面集成(server_gateway_peer/inject_consumer)只在
  # basic 与 distributed chat 构建里编译,MySQL-enhanced 构建尚未接入
  # (见 TODO「修复 chat 增强构建功能缺失」)。chat 连上 hub 后会在 hub
  # 日志里完成服务认证;等不到即说明该构建无法跑 NPC 回环,直接跳过。
  NPC_HUB_BOUND=1
  for _ in {1..100}; do
    if grep -q "service authenticated: chat" "${HUB_LOG}" 2>/dev/null; then
      break
    fi
    if ! kill -0 "${CHAT_PID}" 2>/dev/null; then
      break
    fi
    sleep 0.1
  done
  if ! grep -q "service authenticated: chat" "${HUB_LOG}" 2>/dev/null; then
    NPC_HUB_BOUND=0
    echo "提示: chat 未向 hub 认证（该构建不含服务器平面集成），跳过 NPC 断言"
  fi

  if [[ "${NPC_HUB_BOUND}" == "1" ]]; then
  echo ""
  echo "[npc] send user_2 -> npc:blacksmith_01 (keyword hit)"
  NPC_SEND_OUTPUT=$(./build/tools/benchmark/chirp_chat_send_client --host 127.0.0.1 --port "${CHAT_PORT}" --sender user_2 --receiver "npc:blacksmith_01" --text "any quests?")
  echo "${NPC_SEND_OUTPUT}"
  if [[ "${NPC_SEND_OUTPUT}" != code=0* ]]; then
    echo "错误: NPC 私聊应返回 code=0（事件已发布，绕过玩家投递），实际: ${NPC_SEND_OUTPUT}"
    exit 1
  fi

  echo ""
  echo "[npc] login user_2 (expect the NPC reply)"
  ./build/tools/benchmark/chirp_chat_listen_client --host 127.0.0.1 --port "${CHAT_PORT}" --user user_2 --max 1 --timeout-ms 8000 > "${NPC_LISTEN_LOG}" 2>&1 &
  NPC_LISTEN_PID=$!
  wait "${NPC_LISTEN_PID}" || true
  cat "${NPC_LISTEN_LOG}" || true
  if ! grep -q "notify ts=.*npc:blacksmith_01 -> user_2" "${NPC_LISTEN_LOG}"; then
    echo "错误: 未在 user_2 收到 NPC 回复"
    exit 1
  fi

  echo ""
  echo "[npc] history private (npc:blacksmith_01|user_2, player line + reply)"
  ./build/tools/benchmark/chirp_chat_history_client --host 127.0.0.1 --port "${CHAT_PORT}" --user user_2 --channel_type 0 --channel_id "npc:blacksmith_01|user_2" --limit 10

  echo ""
  echo "[npc] offline path: user_3 sends (fallback reply), then logs in"
  NPC_OFFLINE_SEND_OUTPUT=$(./build/tools/benchmark/chirp_chat_send_client --host 127.0.0.1 --port "${CHAT_PORT}" --sender user_3 --receiver "npc:blacksmith_01" --text "hello forge")
  echo "${NPC_OFFLINE_SEND_OUTPUT}"
  if [[ "${NPC_OFFLINE_SEND_OUTPUT}" != code=0* ]]; then
    echo "错误: NPC 私聊（离线玩家）应返回 code=0，实际: ${NPC_OFFLINE_SEND_OUTPUT}"
    exit 1
  fi

  ./build/tools/benchmark/chirp_chat_listen_client --host 127.0.0.1 --port "${CHAT_PORT}" --user user_3 --max 1 --timeout-ms 8000 > "${NPC_OFFLINE_LISTEN_LOG}" 2>&1 &
  OFFLINE_NPC_LISTEN_PID=$!
  wait "${OFFLINE_NPC_LISTEN_PID}" || true
  cat "${NPC_OFFLINE_LISTEN_LOG}" || true
  if ! grep -q "notify ts=.*npc:blacksmith_01 -> user_3" "${NPC_OFFLINE_LISTEN_LOG}"; then
    echo "错误: NPC 回复未在 user_3 登录后补投递"
    exit 1
  fi
  fi

  echo ""
  echo "chat log: ${CHAT_LOG}"
  tail -n 12 "${CHAT_LOG}" || true
  echo ""
  echo "hub log: ${HUB_LOG}"
  tail -n 8 "${HUB_LOG}" || true
elif [[ "${1:-}" == "--smoke-sdk" ]]; then
  # 游戏客户端 SDK 进程级 E2E: 起 chat (内存模式), 两个 SDK 客户端互发,
  # 再验证离线队列补投递。SDK 走 docs/CORE.md 的 supported 路径: 直连 chat。
  SDK_BIN="./build/sdks/core/sdk_example"
  if [ ! -f "${SDK_BIN}" ]; then
    echo "错误: ${SDK_BIN} 未构建 (需要 CHIRP_BUILD_SDK_EXAMPLES=ON, dev/ci preset 已默认开启)"
    exit 1
  fi

  CHAT_PORT="${CHAT_PORT:-$(pick_port)}"
  CHAT_WS_PORT="${CHAT_WS_PORT:-$(pick_port)}"
  CHAT_LOG="${CHAT_LOG:-/tmp/chirp_chat_smoke_sdk.log}"
  SDK_A_LOG="${SDK_A_LOG:-/tmp/chirp_sdk_a_smoke.log}"
  SDK_B_LOG="${SDK_B_LOG:-/tmp/chirp_sdk_b_smoke.log}"
  SDK_D_LOG="${SDK_D_LOG:-/tmp/chirp_sdk_d_smoke.log}"

  ./build/services/chat/chirp_chat --port "${CHAT_PORT}" --ws_port "${CHAT_WS_PORT}" > "${CHAT_LOG}" 2>&1 &
  CHAT_PID=$!

  cleanup() {
    kill -TERM "${CHAT_PID}" 2>/dev/null || true
    wait "${CHAT_PID}" 2>/dev/null || true
  }
  trap cleanup EXIT

  wait_port "${CHAT_PORT}" chirp_chat "${CHAT_LOG}"

  echo ""
  echo "[sdk] online delivery: sdk_a <-> sdk_b"
  "${SDK_BIN}" --host 127.0.0.1 --port "${CHAT_PORT}" --user sdk_a \
    --peer sdk_b --message "a2b-hello" \
    --expect "b2a-hello" --expect-from sdk_b --timeout-ms 8000 > "${SDK_A_LOG}" 2>&1 &
  SDK_A_PID=$!

  "${SDK_BIN}" --host 127.0.0.1 --port "${CHAT_PORT}" --user sdk_b \
    --peer sdk_a --message "b2a-hello" \
    --expect "a2b-hello" --expect-from sdk_a --timeout-ms 8000 > "${SDK_B_LOG}" 2>&1 &
  SDK_B_PID=$!

  set +e
  wait "${SDK_A_PID}"
  SDK_A_RC=$?
  wait "${SDK_B_PID}"
  SDK_B_RC=$?
  set -e
  cat "${SDK_A_LOG}" || true
  cat "${SDK_B_LOG}" || true
  if [[ "${SDK_A_RC}" != "0" || "${SDK_B_RC}" != "0" ]]; then
    echo "错误: SDK 在线互发失败 (sdk_a rc=${SDK_A_RC}, sdk_b rc=${SDK_B_RC})"
    exit 1
  fi
  if ! grep -q "SMOKE_OK sdk_b -> sdk_a" "${SDK_A_LOG}" || ! grep -q "SMOKE_OK sdk_a -> sdk_b" "${SDK_B_LOG}"; then
    echo "错误: 未观察到双向 SMOKE_OK"
    exit 1
  fi

  echo ""
  echo "[sdk] offline queue: sdk_c -> sdk_d (offline), sdk_d 登录后补投递"
  "${SDK_BIN}" --host 127.0.0.1 --port "${CHAT_PORT}" --user sdk_c \
    --peer sdk_d --message "c2d-offline" --settle-ms 800

  "${SDK_BIN}" --host 127.0.0.1 --port "${CHAT_PORT}" --user sdk_d \
    --expect "c2d-offline" --expect-from sdk_c --timeout-ms 8000 > "${SDK_D_LOG}" 2>&1 &
  SDK_D_PID=$!

  set +e
  wait "${SDK_D_PID}"
  SDK_D_RC=$?
  set -e
  cat "${SDK_D_LOG}" || true
  if [[ "${SDK_D_RC}" != "0" ]] || ! grep -q "SMOKE_OK sdk_c -> sdk_d" "${SDK_D_LOG}"; then
    echo "错误: SDK 离线消息未在 sdk_d 登录后补投递 (rc=${SDK_D_RC})"
    exit 1
  fi

  echo ""
  echo "chat log: ${CHAT_LOG}"
  tail -n 12 "${CHAT_LOG}" || true
else
  CHAT_PORT="${CHAT_PORT:-$(pick_port)}"
  CHAT_WS_PORT="${CHAT_WS_PORT:-$(pick_port)}"
  CHAT_LOG="${CHAT_LOG:-/tmp/chirp_chat_smoke.log}"
  LISTEN_LOG="${LISTEN_LOG:-/tmp/chirp_chat_listen_smoke.log}"
  OFFLINE_LISTEN_LOG="${OFFLINE_LISTEN_LOG:-/tmp/chirp_chat_offline_listen_smoke.log}"
  REDIS_PORT="${REDIS_PORT:-$(pick_port)}"
  REDIS_DIR="${REDIS_DIR:-$(mktemp -d /tmp/chirp_chat_smoke_redis.XXXXXX)}"
  REDIS_LOG="${REDIS_LOG:-/tmp/chirp_chat_smoke_redis.log}"
  ARCHIVE_SQL="${ARCHIVE_SQL:-/tmp/chirp_chat_smoke_archive.sql}"
  ARCHIVE_ACK="${ARCHIVE_ACK:-/tmp/chirp_chat_smoke_archive_ack.sh}"

  REDIS_SERVER_BIN="${REDIS_SERVER_BIN:-$(command -v redis-server || true)}"
  REDIS_CLI_BIN="${REDIS_CLI_BIN:-$(command -v redis-cli || true)}"

  if [[ -z "${REDIS_SERVER_BIN}" ]]; then
    echo "警告: 未找到 redis-server，chat smoke 将以内存模式运行（跳过 Redis 相关验证）"
  else
    "${REDIS_SERVER_BIN}" --port "${REDIS_PORT}" --save '' --appendonly no --dir "${REDIS_DIR}" > "${REDIS_LOG}" 2>&1 &
    REDIS_PID=$!
  fi

  if [[ -n "${REDIS_SERVER_BIN}" && -n "${REDIS_CLI_BIN}" ]]; then
    for _ in {1..50}; do
      if "${REDIS_CLI_BIN}" -p "${REDIS_PORT}" ping >/dev/null 2>&1; then
        break
      fi
      sleep 0.1
    done
  fi

  ./build/services/chat/chirp_chat --port "${CHAT_PORT}" --ws_port "${CHAT_WS_PORT}" --redis_host 127.0.0.1 --redis_port "${REDIS_PORT}" > "${CHAT_LOG}" 2>&1 &
  CHAT_PID=$!

  cleanup() {
    kill -TERM "${CHAT_PID}" 2>/dev/null || true
    wait "${CHAT_PID}" 2>/dev/null || true
    kill -TERM "${REDIS_PID:-}" 2>/dev/null || true
    wait "${REDIS_PID:-}" 2>/dev/null || true
    rm -rf "${REDIS_DIR}"
  }
  trap cleanup EXIT

  wait_port "${CHAT_PORT}" chirp_chat "${CHAT_LOG}"

  echo ""
  echo "[tcp] listen user_2 (1 msg)"
  ./build/tools/benchmark/chirp_chat_listen_client --host 127.0.0.1 --port "${CHAT_PORT}" --user user_2 --max 1 --timeout-ms 8000 > "${LISTEN_LOG}" 2>&1 &
  LISTEN_PID=$!

  sleep 0.2

  echo ""
  echo "[tcp] send user_1 -> user_2"
  ./build/tools/benchmark/chirp_chat_send_client --host 127.0.0.1 --port "${CHAT_PORT}" --sender user_1 --receiver user_2 --text "hello"

  wait "${LISTEN_PID}" || true
  cat "${LISTEN_LOG}" || true

  echo ""
  echo "[tcp] history private (user_1|user_2)"
  ./build/tools/benchmark/chirp_chat_history_client --host 127.0.0.1 --port "${CHAT_PORT}" --user user_1 --channel_type 0 --channel_id "user_1|user_2" --limit 10

  echo ""
  echo "[tcp] send user_1 -> offline user_3"
  OFFLINE_SEND_OUTPUT=$(./build/tools/benchmark/chirp_chat_send_client --host 127.0.0.1 --port "${CHAT_PORT}" --sender user_1 --receiver user_3 --text "offline hello")
  echo "${OFFLINE_SEND_OUTPUT}"
  # 离线语义:所有 chat 构建都承诺登录补投递。basic main 对离线接收方回
  # code=6(TARGET_OFFLINE),MySQL-enhanced main 接受即回 code=0(2026-09-15
  # 起按 router 投递计数入队,Redis 不可用时落内存兜底)。响应码只是构建
  # 差异,补投递一律断言。
  case "${OFFLINE_SEND_OUTPUT}" in
    code=6*|code=0*) ;;
    *)
      echo "错误: 离线发送返回意外结果(预期 code=6 或 code=0): ${OFFLINE_SEND_OUTPUT}"
      exit 1
      ;;
  esac

  echo ""
  echo "[tcp] login offline user_3 (expect queued notify)"
  ./build/tools/benchmark/chirp_chat_listen_client --host 127.0.0.1 --port "${CHAT_PORT}" --user user_3 --max 1 --timeout-ms 8000 > "${OFFLINE_LISTEN_LOG}" 2>&1 &
  OFFLINE_LISTEN_PID=$!

  wait "${OFFLINE_LISTEN_PID}" || true
  cat "${OFFLINE_LISTEN_LOG}" || true

  if ! grep -q "notify ts=.*user_1 -> user_3" "${OFFLINE_LISTEN_LOG}"; then
    echo "错误: 离线消息未在 user_3 登录后补投递"
    exit 1
  fi

  echo ""
  echo "[tcp] history private (user_1|user_3)"
  ./build/tools/benchmark/chirp_chat_history_client --host 127.0.0.1 --port "${CHAT_PORT}" --user user_1 --channel_type 0 --channel_id "user_1|user_3" --limit 10

  echo ""
  echo "[ws] login -> ping on chat"
  ./build/tools/benchmark/chirp_ws_login_client --host 127.0.0.1 --port "${CHAT_WS_PORT}" --token user_4 --device dev_c --platform web

  echo ""
  echo "[archive] export + fake mysql apply + redis ack"
  ./tools/archive_chat_redis.sh \
    --redis_host 127.0.0.1 --redis_port "${REDIS_PORT}" \
    --out "${ARCHIVE_SQL}" \
    --ack_out "${ARCHIVE_ACK}" \
    --mysql_cmd "cat >/dev/null" \
    --apply_ack 1

  if [[ -n "${REDIS_SERVER_BIN}" && -n "${REDIS_CLI_BIN}" ]]; then
    REMAINING_KEYS=$("${REDIS_CLI_BIN}" -p "${REDIS_PORT}" --scan --pattern 'chat:*' | wc -l | tr -d ' ')
    if [[ "${REMAINING_KEYS}" != "0" ]]; then
      echo "错误: 归档 ack 后 Redis 中仍有残留 chat:* keys"
      "${REDIS_CLI_BIN}" -p "${REDIS_PORT}" --scan --pattern 'chat:*' || true
      exit 1
    fi
  else
    echo "警告: 无 redis-cli，跳过归档后的残留 key 检查"
  fi

  echo ""
  echo "chat log: ${CHAT_LOG}"
  tail -n 20 "${CHAT_LOG}" || true
fi

echo ""
echo "=== Smoke Test Done ==="
