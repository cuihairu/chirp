#!/bin/bash

# 测试服务启动脚本

set -euo pipefail

echo "=== Chirp 服务启动测试 ==="
echo ""

source "$(dirname "${BASH_SOURCE[0]}")/scripts/test_services_lib.sh"
# 大体积 smoke 腿（redis/npc/edge/jwt/search）按函数拆在 legs 文件。
source "$(dirname "${BASH_SOURCE[0]}")/scripts/test_services_legs.sh"
require_bin "./build/services/game/sdk_gateway/chirp_game_sdk_gateway"
require_bin "./build/services/app/auth/chirp_app_auth"
require_bin "./build/services/shared/chat/chirp_chat"

echo "✓ 核心服务已构建"
echo ""
echo "构建产物:"
# services/ 下是多级嵌套新路径（game/sdk_gateway 等）；单层 glob 只会命中
# 重构前的旧扁平产物，漏掉全部当前目标。find 全深度扫描并跳过 CMakeFiles。
find ./build/services -type f -name 'chirp_*' -executable ! -path '*/CMakeFiles/*' \
  | sort | xargs -r ls -lh
echo ""
echo "测试工具:"
ls -lh ./build/tools/benchmark/chirp_*
echo ""
echo "=== 测试完成 ==="
echo ""
echo "要运行服务，执行:"
echo "  ./build/services/game/sdk_gateway/chirp_game_sdk_gateway --port 5000 --ws_port 5001"
echo "  ./build/services/app/auth/chirp_app_auth --port 6000"
echo "  ./build/services/shared/chat/chirp_chat --port 7000 --ws_port 7001"

SMOKE_ARGS="--smoke --smoke-chat --smoke-redis --smoke-npc --smoke-sdk --smoke-edge --smoke-jwt --smoke-game --smoke-voice --smoke-party --smoke-search"
is_smoke=0
for a in ${SMOKE_ARGS}; do
  if [[ "${1:-}" == "${a}" ]]; then
    is_smoke=1
    break
  fi
done
if [[ "${is_smoke}" != "1" ]]; then
  exit 0
fi

# smoke 模式各自的硬依赖预检：缺二进制在启动前就失败，而不是半路挂掉。
case "${1}" in
  --smoke)
    require_bin "./build/tools/benchmark/chirp_login_client"
    require_bin "./build/tools/benchmark/chirp_ws_login_client"
    ;;
  --smoke-redis)
    require_bin "./build/tools/benchmark/chirp_login_client"
    require_bin "./build/tools/benchmark/chirp_ws_login_client"
    ;;
  --smoke-jwt)
    require_bin "./build/tools/benchmark/chirp_login_client"
    ;;
  --smoke-edge)
    require_bin "./build/tools/benchmark/chirp_login_client"
    require_bin "./build/tools/benchmark/chirp_wp8_client"
    require_bin "./build/services/app/sdk_gateway/chirp_app_sdk_gateway"
    ;;
  --smoke-npc)
    require_bin "./build/services/game/server_gateway/chirp_game_server_gateway"
    require_bin "./build/services/game/npc_dialog/chirp_npc_dialog"
    require_bin "./build/tools/benchmark/chirp_chat_send_client"
    require_bin "./build/tools/benchmark/chirp_chat_listen_client"
    require_bin "./build/tools/benchmark/chirp_chat_history_client"
    ;;
  --smoke-sdk)
    require_bin "./build/sdks/core/sdk_example"
    ;;
  --smoke-game)
    require_bin "./build/tools/benchmark/chirp_login_client"
    ;;
  --smoke-chat)
    require_bin "./build/tools/benchmark/chirp_chat_send_client"
    require_bin "./build/tools/benchmark/chirp_chat_listen_client"
    require_bin "./build/tools/benchmark/chirp_chat_history_client"
    require_bin "./build/tools/benchmark/chirp_ws_login_client"
    ;;
  --smoke-voice)
    require_bin "./build/services/voice/chirp_voice"
    require_bin "./build/tools/benchmark/chirp_voice_smoke_client"
    ;;
  --smoke-party)
    require_bin "./build/services/party/chirp_party"
    require_bin "./build/tools/benchmark/chirp_party_smoke_client"
    ;;
  --smoke-search)
    require_bin "./build/services/search/chirp_search"
    require_bin "./build/tools/benchmark/chirp_chat_send_client"
    require_bin "./build/services/game/sdk_gateway/chirp_game_sdk_gateway"
    ;;
esac

echo ""
if [[ "${1:-}" == "--smoke" ]]; then
  echo "=== Smoke Test (auth + gateway + clients) ==="
elif [[ "${1:-}" == "--smoke-redis" ]]; then
  echo "=== Smoke Test (redis distributed sessions + cross-instance kick) ==="
elif [[ "${1:-}" == "--smoke-npc" ]]; then
  echo "=== Smoke Test (server plane + NPC dialog loop) ==="
elif [[ "${1:-}" == "--smoke-sdk" ]]; then
  echo "=== Smoke Test (game client SDK + chat) ==="
elif [[ "${1:-}" == "--smoke-edge" ]]; then
  echo "=== Smoke Test (gateway absorbs the chat entry: trusted bridge + relay) ==="
elif [[ "${1:-}" == "--smoke-jwt" ]]; then
  echo "=== Smoke Test (unified login: signed JWT end to end, scaffold rejected) ==="
elif [[ "${1:-}" == "--smoke-game" ]]; then
  echo "=== Smoke Test (pure game plane: no app_auth, gateway scaffold + chat bridge) ==="
elif [[ "${1:-}" == "--smoke-voice" ]]; then
  echo "=== Smoke Test (voice plane: room lifecycle over real chirp_voice) ==="
elif [[ "${1:-}" == "--smoke-party" ]]; then
  echo "=== Smoke Test (party plane: snapshot lifecycle over real chirp_party) ==="
elif [[ "${1:-}" == "--smoke-search" ]]; then
  echo "=== Smoke Test (search plane: SQLite FTS5 index + per-client pipe, 2248/2249) ==="
else
  echo "=== Smoke Test (chat + clients) ==="
fi


# 可选：enhanced auth/chat 默认连 127.0.0.1:3306（chirp/chirp_password）。
# CI 用 GitHub service 提供该实例；本地若把 MySQL 放在别的端口，设
# MYSQL_HOST/MYSQL_PORT/MYSQL_USER/MYSQL_PASSWORD/MYSQL_DATABASE 再跑 smoke。
# 全部未设时数组为空，命令行与二进制默认完全一致，零行为变化。
MYSQL_ARGS=()
if [[ -n "${MYSQL_HOST:-}" ]]; then MYSQL_ARGS+=(--mysql_host "${MYSQL_HOST}"); fi
if [[ -n "${MYSQL_PORT:-}" ]]; then MYSQL_ARGS+=(--mysql_port "${MYSQL_PORT}"); fi
if [[ -n "${MYSQL_USER:-}" ]]; then MYSQL_ARGS+=(--mysql_user "${MYSQL_USER}"); fi
if [[ -n "${MYSQL_PASSWORD:-}" ]]; then MYSQL_ARGS+=(--mysql_password "${MYSQL_PASSWORD}"); fi
if [[ -n "${MYSQL_DATABASE:-}" ]]; then MYSQL_ARGS+=(--mysql_database "${MYSQL_DATABASE}"); fi

if [[ "${1:-}" == "--smoke-game" ]]; then
  # 纯游戏平面端到端（无 app_auth）：game_sdk_gateway 不配 --auth_host
  # 走 scaffold（token 即 user_id + BindAuthenticatedSession），ServiceBridge
  # 把 2xxx 转给 game_chat；chat 开 --gateway_service_secret 信任管道，
  # 不配 --token_secret 走本地 scaffold。离线补投递证明全管道双向。
  CHAT_BIN="${CHAT_BIN:-./build/services/shared/chat/chirp_chat}"
  if [ ! -f "${CHAT_BIN}" ]; then
    echo "错误: 未找到 ${CHAT_BIN}"
    exit 1
  fi

  CHAT_PORT="${CHAT_PORT:-$(pick_port)}"
  CHAT_WS_PORT="${CHAT_WS_PORT:-$(pick_port)}"
  GW_PORT="${GW_PORT:-$(pick_port)}"
  GW_WS_PORT="${GW_WS_PORT:-$(pick_port)}"

  CHAT_LOG="${CHAT_LOG:-/tmp/chirp_chat_smoke_game.log}"
  GW_LOG="${GW_LOG:-/tmp/chirp_game_sdk_gateway_smoke_game.log}"
  A_LOG="${A_LOG:-/tmp/chirp_game_a_send.log}"
  B_LOG="${B_LOG:-/tmp/chirp_game_b_refill.log}"

  "${CHAT_BIN}" --port "${CHAT_PORT}" --ws_port "${CHAT_WS_PORT}" \
    --gateway_service_secret game-secret "${MYSQL_ARGS[@]+"${MYSQL_ARGS[@]}"}" > "${CHAT_LOG}" 2>&1 &
  CHAT_PID=$!

  ./build/services/game/sdk_gateway/chirp_game_sdk_gateway --port "${GW_PORT}" --ws_port "${GW_WS_PORT}" \
    --chat_host 127.0.0.1 --chat_port "${CHAT_PORT}" --chat_service_secret game-secret > "${GW_LOG}" 2>&1 &
  GW_PID=$!

  cleanup() {
    stop_proc "${GW_PID}" "${CHAT_PID}"
  }
  trap cleanup EXIT

  wait_port "${CHAT_PORT}" chirp_chat "${CHAT_LOG}"
  wait_port "${GW_PORT}" chirp_game_sdk_gateway "${GW_LOG}"

  echo ""
  echo "[game] A login via gateway (scaffold, no auth_host) + send to offline B"
  timeout 30 ./build/tools/benchmark/chirp_login_client --host 127.0.0.1 --port "${GW_PORT}" \
    --token user_a --device dev_a --platform pc \
    --send_text "game-offline-hello" --peer_user user_b > "${A_LOG}" 2>&1
  grep -q "code=0" "${A_LOG}"
  # B is offline by design: basic answers TARGET_OFFLINE(6, queued),
  # enhanced answers OK; the refill assertion below is the real proof.
  grep -Eq "send code=(0|6)" "${A_LOG}"

  echo ""
  echo "[game] B login via gateway (offline refill via ServiceBridge)"
  set +e
  timeout 30 ./build/tools/benchmark/chirp_login_client --host 127.0.0.1 --port "${GW_PORT}" \
    --token user_b --device dev_b --platform pc --expect_notify_ms 15000 > "${B_LOG}" 2>&1
  B_RC=$?
  set -e
  if [[ "${B_RC}" != "0" ]] || ! grep -q "notify from=user_a" "${B_LOG}" || ! grep -q "content=game-offline-hello" "${B_LOG}"; then
    echo "错误: B 经 gateway 登录后未收到离线补投递 (rc=${B_RC},纯游戏平面管道未生效)"
    cat "${B_LOG}" || true
    echo "gateway log: ${GW_LOG}"
    tail -n 30 "${GW_LOG}" || true
    echo "chat log: ${CHAT_LOG}"
    tail -n 30 "${CHAT_LOG}" || true
    exit 1
  fi

  echo ""
  echo "gateway log: ${GW_LOG}"
  tail -n 20 "${GW_LOG}" || true
  echo ""
  echo "chat log: ${CHAT_LOG}"
  tail -n 20 "${CHAT_LOG}" || true
elif [[ "${1:-}" == "--smoke" ]]; then
  AUTH_PORT="${AUTH_PORT:-$(pick_port)}"
  GW_PORT="${GW_PORT:-$(pick_port)}"
  WS_PORT="${WS_PORT:-$(pick_port)}"

  AUTH_LOG="${AUTH_LOG:-/tmp/chirp_app_auth_smoke.log}"
  GW_LOG="${GW_LOG:-/tmp/chirp_game_sdk_gateway_smoke.log}"

  ./build/services/app/auth/chirp_app_auth --port "${AUTH_PORT}" --jwt_secret dev_secret --allow_scaffold_login 1 "${MYSQL_ARGS[@]+"${MYSQL_ARGS[@]}"}" > "${AUTH_LOG}" 2>&1 &
  AUTH_PID=$!

  ./build/services/game/sdk_gateway/chirp_game_sdk_gateway --port "${GW_PORT}" --ws_port "${WS_PORT}" \
    --auth_host 127.0.0.1 --auth_port "${AUTH_PORT}" > "${GW_LOG}" 2>&1 &
  GW_PID=$!

  cleanup() {
    stop_proc "${GW_PID}" "${AUTH_PID}"
  }
  trap cleanup EXIT

  wait_port "${AUTH_PORT}" chirp_app_auth "${AUTH_LOG}"
  wait_port "${GW_PORT}" chirp_game_sdk_gateway "${GW_LOG}"

  echo ""
  echo "[tcp] login -> ping"
  timeout 30 ./build/tools/benchmark/chirp_login_client --host 127.0.0.1 --port "${GW_PORT}" --token user_1 --device dev_a --platform pc

  echo ""
  echo "[ws] login -> ping"
  timeout 30 ./build/tools/benchmark/chirp_ws_login_client --host 127.0.0.1 --port "${WS_PORT}" --token user_1 --device dev_b --platform web

  echo ""
  echo "auth log: ${AUTH_LOG}"
  tail -n 5 "${AUTH_LOG}" || true
  echo ""
  echo "gateway log: ${GW_LOG}"
  tail -n 20 "${GW_LOG}" || true
elif [[ "${1:-}" == "--smoke-redis" ]]; then
  run_smoke_redis
elif [[ "${1:-}" == "--smoke-npc" ]]; then
  run_smoke_npc
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

  ./build/services/shared/chat/chirp_chat --port "${CHAT_PORT}" --ws_port "${CHAT_WS_PORT}" \
    "${MYSQL_ARGS[@]+"${MYSQL_ARGS[@]}"}" > "${CHAT_LOG}" 2>&1 &
  CHAT_PID=$!

  cleanup() {
    stop_proc "${CHAT_PID}"
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
elif [[ "${1:-}" == "--smoke-edge" ]]; then
  run_smoke_edge
elif [[ "${1:-}" == "--smoke-jwt" ]]; then
  run_smoke_jwt
elif [[ "${1:-}" == "--smoke-voice" ]]; then
  # Voice plane over a real chirp_voice: scaffold login (no secret) and the
  # full room lifecycle asserted inside the client. No Redis/MySQL deps.
  VOICE_PORT="${VOICE_PORT:-$(pick_port)}"
  VOICE_WS_PORT="${VOICE_WS_PORT:-$(pick_port)}"
  VOICE_LOG="${VOICE_LOG:-/tmp/chirp_voice_smoke.log}"
  VOICE_CLIENT_LOG="${VOICE_CLIENT_LOG:-/tmp/chirp_voice_smoke_client.log}"

  ./build/services/voice/chirp_voice --port "${VOICE_PORT}" --ws_port "${VOICE_WS_PORT}" \
    > "${VOICE_LOG}" 2>&1 &
  VOICE_PID=$!

  cleanup() {
    stop_proc "${VOICE_PID}"
  }
  trap cleanup EXIT

  wait_port "${VOICE_PORT}" chirp_voice "${VOICE_LOG}"

  if ! timeout 30 ./build/tools/benchmark/chirp_voice_smoke_client \
    --host 127.0.0.1 --port "${VOICE_PORT}" --user voice_smoke_user \
    > "${VOICE_CLIENT_LOG}" 2>&1; then
    echo "错误: voice smoke 客户端断言失败"
    echo "---- voice client log tail (${VOICE_CLIENT_LOG}) ----"
    tail -n 30 "${VOICE_CLIENT_LOG}" 2>/dev/null || true
    echo "---- voice log tail (${VOICE_LOG}) ----"
    tail -n 30 "${VOICE_LOG}" 2>/dev/null || true
    exit 1
  fi
  cat "${VOICE_CLIENT_LOG}"
  echo ""
  echo "voice log: ${VOICE_LOG}"
  tail -n 10 "${VOICE_LOG}" || true

elif [[ "${1:-}" == "--smoke-party" ]]; then
  # Party plane over a real chirp_party: scaffold login, create/invite/leave
  # round-trip asserted inside the client. In-memory state, no Redis/MySQL.
  PARTY_PORT="${PARTY_PORT:-$(pick_port)}"
  PARTY_WS_PORT="${PARTY_WS_PORT:-$(pick_port)}"
  PARTY_LOG="${PARTY_LOG:-/tmp/chirp_party_smoke.log}"
  PARTY_CLIENT_LOG="${PARTY_CLIENT_LOG:-/tmp/chirp_party_smoke_client.log}"

  ./build/services/party/chirp_party --port "${PARTY_PORT}" --ws_port "${PARTY_WS_PORT}" \
    > "${PARTY_LOG}" 2>&1 &
  PARTY_PID=$!

  cleanup() {
    stop_proc "${PARTY_PID}"
  }
  trap cleanup EXIT

  wait_port "${PARTY_PORT}" chirp_party "${PARTY_LOG}"

  if ! timeout 30 ./build/tools/benchmark/chirp_party_smoke_client \
    --host 127.0.0.1 --port "${PARTY_PORT}" --user party_smoke_user \
    > "${PARTY_CLIENT_LOG}" 2>&1; then
    echo "错误: party smoke 客户端断言失败"
    echo "---- party client log tail (${PARTY_CLIENT_LOG}) ----"
    tail -n 30 "${PARTY_CLIENT_LOG}" 2>/dev/null || true
    echo "---- party log tail (${PARTY_LOG}) ----"
    tail -n 30 "${PARTY_LOG}" 2>/dev/null || true
    exit 1
  fi
  cat "${PARTY_CLIENT_LOG}"
  echo ""
  echo "party log: ${PARTY_LOG}"
  tail -n 10 "${PARTY_LOG}" || true

elif [[ "${1:-}" == "--smoke-search" ]]; then
  run_smoke_search
else
  CHAT_PORT="${CHAT_PORT:-$(pick_port)}"
  CHAT_WS_PORT="${CHAT_WS_PORT:-$(pick_port)}"
  CHAT_LOG="${CHAT_LOG:-/tmp/chirp_chat_smoke.log}"
  LISTEN_LOG="${LISTEN_LOG:-/tmp/chirp_chat_listen_smoke.log}"
  OFFLINE_LISTEN_LOG="${OFFLINE_LISTEN_LOG:-/tmp/chirp_chat_offline_listen_smoke.log}"
  ACK_LISTEN_LOG="${ACK_LISTEN_LOG:-/tmp/chirp_chat_ack_listen_smoke.log}"
  SKIP_ACK_LISTEN_LOG="${SKIP_ACK_LISTEN_LOG:-/tmp/chirp_chat_skip_ack_listen_smoke.log}"
  REQUEUE_LISTEN_LOG="${REQUEUE_LISTEN_LOG:-/tmp/chirp_chat_requeue_listen_smoke.log}"
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

  # --ack_timeout_ms 1000: 投递 ACK 链路的超时窗口压到 1s,让"静默客户端
  # 转离线"的 smoke 段不用等默认 10s。
  ./build/services/shared/chat/chirp_chat --port "${CHAT_PORT}" --ws_port "${CHAT_WS_PORT}" --redis_host 127.0.0.1 --redis_port "${REDIS_PORT}" --ack_timeout_ms 1000 "${MYSQL_ARGS[@]+"${MYSQL_ARGS[@]}"}" > "${CHAT_LOG}" 2>&1 &
  CHAT_PID=$!

  cleanup() {
    stop_proc "${CHAT_PID}"
    stop_proc "${REDIS_PID:-}"
    rm -rf "${REDIS_DIR}"
  }
  trap cleanup EXIT

  wait_port "${CHAT_PORT}" chirp_chat "${CHAT_LOG}"

  echo ""
  echo "[tcp] listen user_2 (1 msg)"
  timeout 30 ./build/tools/benchmark/chirp_chat_listen_client --host 127.0.0.1 --port "${CHAT_PORT}" --user user_2 --max 1 --timeout-ms 8000 > "${LISTEN_LOG}" 2>&1 &
  LISTEN_PID=$!

  sleep 0.2

  echo ""
  echo "[tcp] send user_1 -> user_2"
  timeout 30 ./build/tools/benchmark/chirp_chat_send_client --host 127.0.0.1 --port "${CHAT_PORT}" --sender user_1 --receiver user_2 --text "hello"

  wait "${LISTEN_PID}" || true
  cat "${LISTEN_LOG}" || true

  echo ""
  echo "[tcp] history private (user_1|user_2)"
  timeout 30 ./build/tools/benchmark/chirp_chat_history_client --host 127.0.0.1 --port "${CHAT_PORT}" --user user_1 --channel_type 0 --channel_id "user_1|user_2" --limit 10

  echo ""
  echo "[tcp] send user_1 -> offline user_3"
  # user_1 私聊节奏限流是 1s 一条(game_chat_features P0),同用户连发需要
  # 拉开间隔,否则第二条回 code=8(RATE_LIMITED)。
  sleep 1.1
  OFFLINE_SEND_OUTPUT=$(timeout 30 ./build/tools/benchmark/chirp_chat_send_client --host 127.0.0.1 --port "${CHAT_PORT}" --sender user_1 --receiver user_3 --text "offline hello")
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
  timeout 30 ./build/tools/benchmark/chirp_chat_listen_client --host 127.0.0.1 --port "${CHAT_PORT}" --user user_3 --max 1 --timeout-ms 8000 > "${OFFLINE_LISTEN_LOG}" 2>&1 &
  OFFLINE_LISTEN_PID=$!

  wait "${OFFLINE_LISTEN_PID}" || true
  cat "${OFFLINE_LISTEN_LOG}" || true

  if ! grep -q "notify ts=.*user_1 -> user_3" "${OFFLINE_LISTEN_LOG}"; then
    echo "错误: 离线消息未在 user_3 登录后补投递"
    exit 1
  fi

  echo ""
  echo "[tcp] history private (user_1|user_3)"
  timeout 30 ./build/tools/benchmark/chirp_chat_history_client --host 127.0.0.1 --port "${CHAT_PORT}" --user user_1 --channel_type 0 --channel_id "user_1|user_3" --limit 10

  echo ""
  echo "[ws] login -> ping on chat"
  timeout 30 ./build/tools/benchmark/chirp_ws_login_client --host 127.0.0.1 --port "${CHAT_WS_PORT}" --token user_4 --device dev_c --platform web

  echo ""
  echo "[tcp][ack] listen user_5 (acks notifies) + send user_1 -> user_5"
  timeout 30 ./build/tools/benchmark/chirp_chat_listen_client --host 127.0.0.1 --port "${CHAT_PORT}" --user user_5 --max 1 --timeout-ms 8000 > "${ACK_LISTEN_LOG}" 2>&1 &
  ACK_LISTEN_PID=$!

  sleep 0.2

  # 同上:user_1 私聊 1s 节奏,与上一条发送拉开间隔。
  sleep 1.1
  timeout 30 ./build/tools/benchmark/chirp_chat_send_client --host 127.0.0.1 --port "${CHAT_PORT}" --sender user_1 --receiver user_5 --text "ack hello"

  wait "${ACK_LISTEN_PID}" || true
  cat "${ACK_LISTEN_LOG}" || true

  if ! grep -q "notify ts=.*user_1 -> user_5" "${ACK_LISTEN_LOG}"; then
    echo "错误: user_5 未收到实时通知"
    exit 1
  fi
  if ! grep -q "message acked .*user_5" "${CHAT_LOG}"; then
    echo "错误: 服务端未记录 user_5 的 message ack"
    exit 1
  fi

  echo ""
  echo "[tcp][ack] silent user_6 (--skip-ack) -> ack timeout requeue"
  # user_6 声明了 supports_message_ack 但 --skip-ack 静默:投递成功也必须
  # 在 --ack_timeout_ms 后转回离线队列,这是僵尸连接丢消息的根治路径。
  timeout 30 ./build/tools/benchmark/chirp_chat_listen_client --host 127.0.0.1 --port "${CHAT_PORT}" --user user_6 --max 1 --skip-ack --timeout-ms 8000 > "${SKIP_ACK_LISTEN_LOG}" 2>&1 &
  SKIP_ACK_LISTEN_PID=$!

  sleep 0.2

  # 同上:user_1 私聊 1s 节奏,与上一条发送拉开间隔。
  sleep 1.1
  timeout 30 ./build/tools/benchmark/chirp_chat_send_client --host 127.0.0.1 --port "${CHAT_PORT}" --sender user_1 --receiver user_6 --text "ack timeout hello"

  for _ in {1..50}; do
    grep -q "message ack timeout, requeued offline" "${CHAT_LOG}" 2>/dev/null && break
    sleep 0.1
  done
  if ! grep -q "message ack timeout, requeued offline" "${CHAT_LOG}"; then
    echo "错误: 静默客户端未触发 ack 超时转离线"
    exit 1
  fi

  wait "${SKIP_ACK_LISTEN_PID}" || true
  cat "${SKIP_ACK_LISTEN_LOG}" || true

  echo ""
  echo "[tcp][ack] relogin user_6 (expect requeued notify)"
  timeout 30 ./build/tools/benchmark/chirp_chat_listen_client --host 127.0.0.1 --port "${CHAT_PORT}" --user user_6 --max 1 --timeout-ms 8000 > "${REQUEUE_LISTEN_LOG}" 2>&1 &
  REQUEUE_LISTEN_PID=$!

  wait "${REQUEUE_LISTEN_PID}" || true
  cat "${REQUEUE_LISTEN_LOG}" || true

  if ! grep -q "notify ts=.*user_1 -> user_6" "${REQUEUE_LISTEN_LOG}"; then
    echo "错误: 超时转离线消息未在 user_6 重登录后补投递"
    exit 1
  fi

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
