#!/bin/bash

# 测试服务启动脚本

set -euo pipefail

echo "=== Chirp 服务启动测试 ==="
echo ""

# 检查构建产物是否存在
require_bin() {
  if [ ! -f "$1" ]; then
    echo "错误: $1 未构建"
    exit 1
  fi
}

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

SMOKE_ARGS="--smoke --smoke-chat --smoke-redis --smoke-npc --smoke-sdk --smoke-edge --smoke-jwt --smoke-game"
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

# 等待日志文件出现指定模式（0.1s 轮询）。用于 hold 型客户端与脚本的登录
# 同步：客户端打出 `pong msg_id=` 即已完成登录并进入 kick 等待窗口，此时
# 再触发第二个登录，断言窗口不因慢机启动延迟被吃掉。
wait_log() {
  local file="$1" pattern="$2" timeout_s="${3:-10}"
  local deadline=$((SECONDS + timeout_s))
  while (( SECONDS < deadline )); do
    if [[ -f "${file}" ]] && grep -q "${pattern}" "${file}"; then
      return 0
    fi
    sleep 0.1
  done
  echo "错误: 日志 ${file} 在 ${timeout_s}s 内未出现模式 '${pattern}'"
  if [[ -f "${file}" ]]; then
    tail -n 20 "${file}" || true
  fi
  return 1
}

# 等待 redis 中出现 claim key（仅 --smoke-redis 使用，依赖 REDIS_CONTAINER）。
# 这是 hold 型客户端唯一可信的「已登录且存活」信号：客户端进程的 stdout 在
# 重定向到文件时是全缓冲，`pong` 那行要等进程退出才落盘，wait_log 因此只在
# hold 超时退出后才命中——此时断连已把 claim 释放（DEL），第二个登录 GET
# miss，互踢永远不触发（本机真进程链路用 redis MONITOR 抓到过完整证据）。
# claim key 的 SETEX 完成于 LOGIN_RESP 发出之前，key 出现即 hold 就绪。
wait_key() {
  local key="$1" timeout_s="${2:-15}"
  local deadline=$((SECONDS + timeout_s))
  while (( SECONDS < deadline )); do
    if [[ "$(docker exec "${REDIS_CONTAINER}" redis-cli --raw EXISTS "${key}")" == "1" ]]; then
      return 0
    fi
    sleep 0.1
  done
  echo "错误: redis claim key '${key}' 在 ${timeout_s}s 内未出现"
  docker exec "${REDIS_CONTAINER}" redis-cli --raw KEYS 'chirp:sess:*' || true
  return 1
}

# TERM a process, give it a fixed grace period, then KILL. Never blocks:
# a bare `wait` on a process stuck outside its signal handler's reach is
# how a smoke turns into a hung CI job. KILL on an already-exited pid is
# a harmless no-op.
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
  # 走 scaffold（token 即 user_id + BindAuthenticatedSession），ChatBridge
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
  echo "[game] B login via gateway (offline refill via ChatBridge)"
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
  if ! command -v docker >/dev/null 2>&1; then
    echo "错误: 未找到 docker，无法运行 --smoke-redis"
    exit 1
  fi
  if ! timeout 30 docker info >/dev/null 2>&1; then
    echo "错误: 无法连接 Docker daemon，请先启动 Docker Desktop（或确保 docker daemon 在运行）"
    exit 1
  fi

  AUTH_PORT="${AUTH_PORT:-$(pick_port)}"
  REDIS_PORT="${REDIS_PORT:-$(pick_port)}"
  GW1_PORT="${GW1_PORT:-$(pick_port)}"
  GW2_PORT="${GW2_PORT:-$(pick_port)}"
  WS1_PORT="${WS1_PORT:-$(pick_port)}"
  WS2_PORT="${WS2_PORT:-$(pick_port)}"

  AUTH_LOG="${AUTH_LOG:-/tmp/chirp_app_auth_smoke_redis.log}"
  GW1_LOG="${GW1_LOG:-/tmp/chirp_game_sdk_gateway1_smoke_redis.log}"
  GW2_LOG="${GW2_LOG:-/tmp/chirp_game_sdk_gateway2_smoke_redis.log}"
  CLIENT1_LOG="${CLIENT1_LOG:-/tmp/chirp_client_hold_smoke_redis.log}"
  CLIENT3_LOG="${CLIENT3_LOG:-/tmp/chirp_client_cohold_smoke_redis.log}"
  WS_CLIENT1_LOG="${WS_CLIENT1_LOG:-/tmp/chirp_ws_client_hold_smoke_redis.log}"

  REDIS_CONTAINER="${REDIS_CONTAINER:-chirp_redis_smoke_$$}"

  # 与 redis_session_manager.cc 的 kIdSep 一致：claim key 用 \x1F 连接 user 与 device。
  DEV_SEP=$'\x1f'

  # Bounded: an image pull (or a wedged daemon) must fail the smoke with a
  # message instead of hanging the job.
  if ! timeout 180 docker run --rm -d --name "${REDIS_CONTAINER}" -p "127.0.0.1:${REDIS_PORT}:6379" redis:7-alpine >/dev/null; then
    echo "错误: docker 启动 redis:7-alpine 失败（180s 内未就绪或镜像拉取失败）"
    exit 1
  fi

  cleanup() {
    stop_proc "${GW1_PID:-}" "${GW2_PID:-}" "${AUTH_PID:-}"
    docker rm -f "${REDIS_CONTAINER}" >/dev/null 2>&1 || true
  }
  trap cleanup EXIT

  for _ in {1..50}; do
    if docker exec "${REDIS_CONTAINER}" redis-cli ping >/dev/null 2>&1; then
      break
    fi
    sleep 0.1
  done

  ./build/services/app/auth/chirp_app_auth --port "${AUTH_PORT}" --jwt_secret dev_secret --allow_scaffold_login 1 "${MYSQL_ARGS[@]+"${MYSQL_ARGS[@]}"}" > "${AUTH_LOG}" 2>&1 &
  AUTH_PID=$!

  ./build/services/game/sdk_gateway/chirp_game_sdk_gateway --port "${GW1_PORT}" --ws_port "${WS1_PORT}" \
    --auth_host 127.0.0.1 --auth_port "${AUTH_PORT}" \
    --redis_host 127.0.0.1 --redis_port "${REDIS_PORT}" --redis_ttl 3600 --instance_id gw_a > "${GW1_LOG}" 2>&1 &
  GW1_PID=$!

  ./build/services/game/sdk_gateway/chirp_game_sdk_gateway --port "${GW2_PORT}" --ws_port "${WS2_PORT}" \
    --auth_host 127.0.0.1 --auth_port "${AUTH_PORT}" \
    --redis_host 127.0.0.1 --redis_port "${REDIS_PORT}" --redis_ttl 3600 --instance_id gw_b > "${GW2_LOG}" 2>&1 &
  GW2_PID=$!

  wait_port "${AUTH_PORT}" chirp_app_auth "${AUTH_LOG}"
  wait_port "${GW1_PORT}" chirp_game_sdk_gateway-a "${GW1_LOG}"
  wait_port "${GW2_PORT}" chirp_game_sdk_gateway-b "${GW2_LOG}"

  echo ""
  echo "[tcp] hold login on gw_a (expect kick: same user+device via redis claim)"
  # Kick window: 15s. On CI a cold 49M binary needs seconds just to start,
  # and the whole gw_b chain (cold start + login + claim + publish + kick
  # frame) ran ~5s locally but >5s there - the window must dwarf that chain
  # since the gw_b login below blocks until it is done anyway.
  timeout 60 ./build/tools/benchmark/chirp_login_client --host 127.0.0.1 --port "${GW1_PORT}" \
    --token user_1 --device dev_a --platform pc --wait_kick_ms 15000 > "${CLIENT1_LOG}" 2>&1 &
  CLIENT1_PID=$!

  # The kick window starts when the hold client finishes logging in; the
  # reliable "logged in and alive" signal is its claim key in redis (see
  # wait_key - client stdout is fully buffered when redirected, so waiting
  # for the pong log line only fires after the hold process has exited and
  # released its own claim, which would make the kick below impossible).
  wait_key "chirp:sess:user_1${DEV_SEP}dev_a" 15

  echo ""
  echo "[tcp] login on gw_b same device (should kick gw_a)"
  timeout 30 ./build/tools/benchmark/chirp_login_client --host 127.0.0.1 --port "${GW2_PORT}" \
    --token user_1 --device dev_a --platform pc

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
  echo "[tcp] coexistence: hold on gw_a device dev_a, login gw_b device dev_b (no kick)"
  timeout 60 ./build/tools/benchmark/chirp_login_client --host 127.0.0.1 --port "${GW1_PORT}" \
    --token user_3 --device dev_a --platform pc --wait_kick_ms 15000 > "${CLIENT3_LOG}" 2>&1 &
  CLIENT3_PID=$!
  wait_key "chirp:sess:user_3${DEV_SEP}dev_a" 15

  # Same user, different device on the other instance: rc must be 0 (login
  # OK, the tool prints `code=0`) and the hold client must survive its whole
  # kick window (rc=2 = "no kick within Nms"). rc=0 on the hold would mean a
  # device-level claim still kicks across devices; rc=3 means the connection
  # was closed some other way.
  timeout 30 ./build/tools/benchmark/chirp_login_client --host 127.0.0.1 --port "${GW2_PORT}" \
    --token user_3 --device dev_b --platform pc > "${CLIENT3_LOG}.login_b" 2>&1
  grep -q "code=0" "${CLIENT3_LOG}.login_b"

  set +e
  wait "${CLIENT3_PID}"
  CLIENT3_RC=$?
  set -e
  if [[ "${CLIENT3_RC}" == "0" ]]; then
    echo ""
    echo "device-level claim incorrectly kicked the other device (rc=0)"
    cat "${CLIENT3_LOG}" || true
    exit 1
  elif [[ "${CLIENT3_RC}" != "2" ]]; then
    echo ""
    echo "coexistence hold exited unexpectedly (rc=${CLIENT3_RC}, want 2=no kick)"
    cat "${CLIENT3_LOG}" || true
    exit 1
  fi

  echo ""
  echo "[ws] hold login on gw_a (expect kick: same user+device via redis claim)"
  timeout 60 ./build/tools/benchmark/chirp_ws_login_client --host 127.0.0.1 --port "${WS1_PORT}" \
    --token user_2 --device dev_a --platform web --wait_kick_ms 15000 > "${WS_CLIENT1_LOG}" 2>&1 &
  WS_CLIENT1_PID=$!

  wait_key "chirp:sess:user_2${DEV_SEP}dev_a" 15

  echo ""
  echo "[ws] login on gw_b same device (should kick gw_a)"
  timeout 30 ./build/tools/benchmark/chirp_ws_login_client --host 127.0.0.1 --port "${WS2_PORT}" --token user_2 --device dev_a --platform web

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
  echo "client coexistence log: ${CLIENT3_LOG}"
  tail -n 20 "${CLIENT3_LOG}" || true
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

  # CHAT_BIN is overridable: the enhanced build hard-depends on MySQL at
  # startup, so a MySQL-less tree (ENABLE_TESTS off, basic main.cc) can cover
  # this smoke by pointing CHAT_BIN at its own chirp_chat.
  CHAT_BIN="${CHAT_BIN:-./build/services/shared/chat/chirp_chat}"
  if [ ! -f "${CHAT_BIN}" ]; then
    echo "错误: 未找到 ${CHAT_BIN}"
    exit 1
  fi

  ./build/services/game/server_gateway/chirp_game_server_gateway --port "${HUB_PORT}" \
    --service chat=chat-secret --service npc_dialog=npc-secret --chat_service_id chat \
    > "${HUB_LOG}" 2>&1 &
  HUB_PID=$!

  wait_port "${HUB_PORT}" chirp_game_server_gateway "${HUB_LOG}"

  "${CHAT_BIN}" --port "${CHAT_PORT}" --ws_port "${CHAT_WS_PORT}" \
    --server_gateway_host 127.0.0.1 --server_gateway_port "${HUB_PORT}" \
    --server_gateway_secret chat-secret --npc_service_id npc_dialog \
    "${MYSQL_ARGS[@]+"${MYSQL_ARGS[@]}"}" > "${CHAT_LOG}" 2>&1 &
  CHAT_PID=$!
  ./build/services/game/npc_dialog/chirp_npc_dialog \
    --server_gateway_host 127.0.0.1 --server_gateway_port "${HUB_PORT}" \
    --server_gateway_secret npc-secret \
    > "${NPC_LOG}" 2>&1 &
  NPC_PID=$!

  cleanup() {
    stop_proc "${NPC_PID}" "${CHAT_PID}" "${HUB_PID}"
  }
  trap cleanup EXIT

  wait_port "${CHAT_PORT}" chirp_chat "${CHAT_LOG}"

  # 失败时把三个服务的日志尾部倒出来，否则断言挂掉时无任何现场可查。
  dump_npc_logs() {
    echo "---- chat log tail (${CHAT_LOG}) ----"
    tail -n 40 "${CHAT_LOG}" 2>/dev/null || true
    echo "---- hub log tail (${HUB_LOG}) ----"
    tail -n 30 "${HUB_LOG}" 2>/dev/null || true
    echo "---- npc_dialog log tail (${NPC_LOG}) ----"
    tail -n 20 "${NPC_LOG}" 2>/dev/null || true
  }

  # 等发送方的断连真正被 chat 处理完：NPC 回复可能快过 FIN 的处理，
  # 若在处理前到达，会被"实时投递"给发送端自己的连接（发送端不读帧），
  # 消息就此丢失。chat 日志出现 User disconnected 后，监听端才登录。
  # 最多等 10s，超时按尽力而为继续（不让 smoke 卡死）。
  wait_disconnect_logged() {
    local user="$1" i
    for i in $(seq 1 100); do
      grep -q "User disconnected: ${user}" "${CHAT_LOG}" 2>/dev/null && return 0
      sleep 0.1
    done
    echo "提示: 10s 内未在 chat 日志看到 User disconnected: ${user}，继续执行"
  }

  # 能力探测:chat 连上 hub 后会在 hub 日志里完成服务认证
  # (basic/distributed/enhanced 构建均含服务器平面集成);等不到即说明
  # 该构建无法跑 NPC 回环,直接跳过。
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
  NPC_SEND_OUTPUT=$(timeout 30 ./build/tools/benchmark/chirp_chat_send_client --host 127.0.0.1 --port "${CHAT_PORT}" --sender user_2 --receiver "npc:blacksmith_01" --text "any quests?")
  echo "${NPC_SEND_OUTPUT}"
  if [[ "${NPC_SEND_OUTPUT}" != code=0* ]]; then
    echo "错误: NPC 私聊应返回 code=0（事件已发布，绕过玩家投递），实际: ${NPC_SEND_OUTPUT}"
    dump_npc_logs
    exit 1
  fi

  echo ""
  echo "[npc] login user_2 (expect the NPC reply)"
  wait_disconnect_logged user_2
  timeout 30 ./build/tools/benchmark/chirp_chat_listen_client --host 127.0.0.1 --port "${CHAT_PORT}" --user user_2 --max 1 --timeout-ms 8000 > "${NPC_LISTEN_LOG}" 2>&1 &
  NPC_LISTEN_PID=$!
  wait "${NPC_LISTEN_PID}" || true
  cat "${NPC_LISTEN_LOG}" || true
  if ! grep -q "notify ts=.*npc:blacksmith_01 -> user_2" "${NPC_LISTEN_LOG}"; then
    echo "错误: 未在 user_2 收到 NPC 回复"
    dump_npc_logs
    exit 1
  fi

  echo ""
  echo "[npc] history private (npc:blacksmith_01|user_2, player line + reply)"
  timeout 30 ./build/tools/benchmark/chirp_chat_history_client --host 127.0.0.1 --port "${CHAT_PORT}" --user user_2 --channel_type 0 --channel_id "npc:blacksmith_01|user_2" --limit 10

  echo ""
  echo "[npc] offline path: user_3 sends (fallback reply), then logs in"
  NPC_OFFLINE_SEND_OUTPUT=$(timeout 30 ./build/tools/benchmark/chirp_chat_send_client --host 127.0.0.1 --port "${CHAT_PORT}" --sender user_3 --receiver "npc:blacksmith_01" --text "hello forge")
  echo "${NPC_OFFLINE_SEND_OUTPUT}"
  if [[ "${NPC_OFFLINE_SEND_OUTPUT}" != code=0* ]]; then
    echo "错误: NPC 私聊（离线玩家）应返回 code=0，实际: ${NPC_OFFLINE_SEND_OUTPUT}"
    dump_npc_logs
    exit 1
  fi

  wait_disconnect_logged user_3
  timeout 30 ./build/tools/benchmark/chirp_chat_listen_client --host 127.0.0.1 --port "${CHAT_PORT}" --user user_3 --max 1 --timeout-ms 8000 > "${NPC_OFFLINE_LISTEN_LOG}" 2>&1 &
  OFFLINE_NPC_LISTEN_PID=$!
  wait "${OFFLINE_NPC_LISTEN_PID}" || true
  cat "${NPC_OFFLINE_LISTEN_LOG}" || true
  if ! grep -q "notify ts=.*npc:blacksmith_01 -> user_3" "${NPC_OFFLINE_LISTEN_LOG}"; then
    echo "错误: NPC 回复未在 user_3 登录后补投递"
    dump_npc_logs
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
  # 网关吸收 chat 直连入口的进程级 E2E(migration path 第 4 步):客户端只
  # 连 gateway,chat 业务包(2xxx)经 bridge 内部连接转发。chat 开
  # --gateway_service_secret(5001 信任门)+ --login_rate_limit_per_min 1,
  # 直连登录吃限流而 gateway 管道豁免(trusted),一次 smoke 同时验证
  # 「直连路径仍工作」(收尾约束)与「经 gateway 全管道」。
  #
  # 限流是 Redis-backed(无 redis 时 inert),本段硬依赖本地 redis-server。
  REDIS_SERVER_BIN="${REDIS_SERVER_BIN:-$(command -v redis-server || true)}"
  if [[ -z "${REDIS_SERVER_BIN}" ]]; then
    echo "错误: --smoke-edge 需要本地 redis-server(登录限流依赖 Redis)"
    exit 1
  fi

  AUTH_PORT="${AUTH_PORT:-$(pick_port)}"
  CHAT_PORT="${CHAT_PORT:-$(pick_port)}"
  CHAT_WS_PORT="${CHAT_WS_PORT:-$(pick_port)}"
  GW_PORT="${GW_PORT:-$(pick_port)}"
  GW_WS_PORT="${GW_WS_PORT:-$(pick_port)}"
  REDIS_PORT="${REDIS_PORT:-$(pick_port)}"
  REDIS_DIR="${REDIS_DIR:-$(mktemp -d /tmp/chirp_edge_smoke_redis.XXXXXX)}"
  REDIS_LOG="${REDIS_LOG:-/tmp/chirp_edge_smoke_redis.log}"

  AUTH_LOG="${AUTH_LOG:-/tmp/chirp_app_auth_smoke_edge.log}"
  CHAT_LOG="${CHAT_LOG:-/tmp/chirp_chat_smoke_edge.log}"
  GW_LOG="${GW_LOG:-/tmp/chirp_game_sdk_gateway_smoke_edge.log}"
  C1_LOG="${C1_LOG:-/tmp/chirp_edge_c1_direct.log}"
  C2_LOG="${C2_LOG:-/tmp/chirp_edge_c2_direct.log}"
  A_LOG="${A_LOG:-/tmp/chirp_edge_a_gateway.log}"
  B_LOG="${B_LOG:-/tmp/chirp_edge_b_refill.log}"
  B2_LOG="${B2_LOG:-/tmp/chirp_edge_b2_live.log}"
  A2_LOG="${A2_LOG:-/tmp/chirp_edge_a2_live.log}"
  APP_PORT="${APP_PORT:-$(pick_port)}"
  APP_LOG="${APP_LOG:-/tmp/chirp_app_sdk_gateway_smoke_edge.log}"
  A3_LOG="${A3_LOG:-/tmp/chirp_edge_a3_appgw.log}"
  B3_LOG="${B3_LOG:-/tmp/chirp_edge_b3_appgw.log}"
  WP8_LOG="${WP8_LOG:-/tmp/chirp_edge_wp8.log}"

  "${REDIS_SERVER_BIN}" --port "${REDIS_PORT}" --save '' --appendonly no --dir "${REDIS_DIR}" > "${REDIS_LOG}" 2>&1 &
  REDIS_PID=$!

  # AUTH_BIN/CHAT_BIN are overridable: the enhanced (MySQL) builds refuse to
  # start without a reachable database, so MySQL-less trees point both at
  # their own basic-form binaries (same CHAT_BIN precedent as --smoke-npc).
  AUTH_BIN="${AUTH_BIN:-./build/services/app/auth/chirp_app_auth}"
  CHAT_BIN="${CHAT_BIN:-./build/services/shared/chat/chirp_chat}"
  "${AUTH_BIN}" --port "${AUTH_PORT}" --jwt_secret dev_secret --allow_scaffold_login 1 "${MYSQL_ARGS[@]+"${MYSQL_ARGS[@]}"}" > "${AUTH_LOG}" 2>&1 &
  AUTH_PID=$!

  "${CHAT_BIN}" --port "${CHAT_PORT}" --ws_port "${CHAT_WS_PORT}" \
    --redis_host 127.0.0.1 --redis_port "${REDIS_PORT}" \
    --login_rate_limit_per_min 1 --gateway_service_secret edge-secret \
    "${MYSQL_ARGS[@]+"${MYSQL_ARGS[@]}"}" > "${CHAT_LOG}" 2>&1 &
  CHAT_PID=$!

  ./build/services/game/sdk_gateway/chirp_game_sdk_gateway --port "${GW_PORT}" --ws_port "${GW_WS_PORT}" \
    --auth_host 127.0.0.1 --auth_port "${AUTH_PORT}" \
    --chat_host 127.0.0.1 --chat_port "${CHAT_PORT}" --chat_service_secret edge-secret > "${GW_LOG}" 2>&1 &
  GW_PID=$!

  # app_gateway 吸收同一 chat 管道(WP-8 聚合边):登录经 app_auth(scaffold
  # 接受任意 token),bridge 以独立 service_id 过同一个 secret 信任门;
  # --sg_host 指向 app_chat(WP-8 目录 RPC 的信任门在 hub 上),订阅/未读
  # 自服务经 ServerGatewayPeer 转发进 hub。
  ./build/services/app/sdk_gateway/chirp_app_sdk_gateway --port "${APP_PORT}" \
    --auth_host 127.0.0.1 --auth_port "${AUTH_PORT}" \
    --chat_host 127.0.0.1 --chat_port "${CHAT_PORT}" --chat_service_secret edge-secret \
    --sg_host 127.0.0.1 --sg_port "${CHAT_PORT}" --sg_secret edge-secret > "${APP_LOG}" 2>&1 &
  APP_PID=$!

  cleanup() {
    stop_proc "${APP_PID:-}" "${GW_PID:-}" "${CHAT_PID:-}" "${AUTH_PID:-}" "${REDIS_PID:-}"
    rm -rf "${REDIS_DIR}"
  }
  trap cleanup EXIT

  wait_port "${AUTH_PORT}" chirp_app_auth "${AUTH_LOG}"
  wait_port "${CHAT_PORT}" chirp_chat "${CHAT_LOG}"
  wait_port "${GW_PORT}" chirp_game_sdk_gateway "${GW_LOG}"
  wait_port "${APP_PORT}" chirp_app_sdk_gateway "${APP_LOG}"

  echo ""
  echo "[edge] C1 direct chat login (consumes the only per-IP budget; proves direct entry still works)"
  timeout 30 ./build/tools/benchmark/chirp_login_client --host 127.0.0.1 --port "${CHAT_PORT}" \
    --token user_c1 --device dev_c1 --platform pc > "${C1_LOG}" 2>&1
  grep -q "code=0" "${C1_LOG}"

  echo ""
  echo "[edge] C2 direct chat login (rate limited: RATE_LIMITED=8)"
  set +e
  timeout 30 ./build/tools/benchmark/chirp_login_client --host 127.0.0.1 --port "${CHAT_PORT}" \
    --token user_c2 --device dev_c2 --platform pc > "${C2_LOG}" 2>&1
  C2_RC=$?
  set -e
  if [[ "${C2_RC}" != "0" ]]; then
    echo "错误: C2 直连进程异常退出 (rc=${C2_RC})"
    cat "${C2_LOG}" || true
    exit 1
  fi
  if ! grep -q "code=8" "${C2_LOG}"; then
    echo "错误: C2 直连登录未被限流(预期 code=8/RATE_LIMITED,说明 trusted 豁免之外的直连限流没有生效)"
    cat "${C2_LOG}" || true
    exit 1
  fi

  echo ""
  echo "[edge] A login via gateway (trusted pipe skips the exhausted per-IP budget) + send to offline B"
  timeout 30 ./build/tools/benchmark/chirp_login_client --host 127.0.0.1 --port "${GW_PORT}" \
    --token user_a --device dev_a --platform pc \
    --send_text "edge-offline-hello" --peer_user user_b > "${A_LOG}" 2>&1
  grep -q "code=0" "${A_LOG}"
  # 形态语义分歧:basic 离线发送回 code=6(消息照入离线队列),enhanced 回
  # code=0(CI 走 enhanced)。两形态是否真的入队,由下一步 B 的补投到达证明。
  grep -Eq "send code=(0|6)" "${A_LOG}"

  echo ""
  echo "[edge] B login via gateway (offline refill rides the pipe back as CHAT_MESSAGE_NOTIFY)"
  set +e
  timeout 30 ./build/tools/benchmark/chirp_login_client --host 127.0.0.1 --port "${GW_PORT}" \
    --token user_b --device dev_b --platform pc --expect_notify_ms 15000 > "${B_LOG}" 2>&1
  B_RC=$?
  set -e
  if [[ "${B_RC}" != "0" ]] || ! grep -q "notify from=user_a" "${B_LOG}"; then
    echo "错误: B 经 gateway 登录后未收到离线补投递 (rc=${B_RC})"
    cat "${B_LOG}" || true
    exit 1
  fi

  echo ""
  echo "[edge] live push: B2 holds on gateway, A2 sends through gateway, B2 observes CHAT_MESSAGE_NOTIFY"
  timeout 60 ./build/tools/benchmark/chirp_login_client --host 127.0.0.1 --port "${GW_PORT}" \
    --token user_b --device dev_b2 --platform pc --expect_notify_ms 15000 > "${B2_LOG}" 2>&1 &
  B2_PID=$!
  # 给 B2 的 bridge 握手一点时间;即使它还没就绪,A2 的消息会转入离线队列
  # 由 B2 登录补投递兜底,断言两条路径都成立(收到的内容区分实时/补投)。
  sleep 1
  timeout 30 ./build/tools/benchmark/chirp_login_client --host 127.0.0.1 --port "${GW_PORT}" \
    --token user_a --device dev_a2 --platform pc \
    --send_text "edge-live-hello" --peer_user user_b > "${A2_LOG}" 2>&1
  grep -q "code=0" "${A2_LOG}"
  grep -q "send code=0" "${A2_LOG}"

  set +e
  wait "${B2_PID}"
  B2_RC=$?
  set -e
  if [[ "${B2_RC}" != "0" ]] || ! grep -q "content=edge-live-hello" "${B2_LOG}"; then
    echo "错误: B2 未观察到 A2 经 gateway 的实时/补投通知 (rc=${B2_RC})"
    cat "${B2_LOG}" || true
    exit 1
  fi
  # B2 断开后的清理是异步链(客户端 close -> gateway on_close -> bridge
  # Detach -> 内部连接关 -> chat 移除 session;enhanced 形态还叠加 Redis
  # 路由注销)。A3/B3 因此用从未登录过的全新用户:离线判定不依赖任何
  # teardown 时序,A3 的消息必然入离线队列,B3 登录必然拉到补投。
  sleep 1

  echo ""
  echo "[edge] A3 login via app_gateway (chat pipe absorbed; 2xxx uplink relays to chat) + send to offline B"
  set +e
  timeout 30 ./build/tools/benchmark/chirp_login_client --host 127.0.0.1 --port "${APP_PORT}" \
    --token user_a --device dev_a3 --platform pc \
    --send_text "app-edge-offline-hello" --peer_user user_b_app > "${A3_LOG}" 2>&1
  A3_RC=$?
  set -e
  # 同上:basic 离线发送回 code=6、enhanced 回 code=0;入队由 B3 补投证明。
  if [[ "${A3_RC}" != "0" ]] || ! grep -q "code=0" "${A3_LOG}" || ! grep -Eq "send code=(0|6)" "${A3_LOG}"; then
    echo "错误: A3 经 app_gateway 登录/发送失败 (rc=${A3_RC},2xxx 上行应达 chat)"
    cat "${A3_LOG}" || true
    exit 1
  fi

  echo ""
  echo "[edge] B3 login via app_gateway (offline refill rides the app-edge pipe back as CHAT_MESSAGE_NOTIFY)"
  set +e
  timeout 30 ./build/tools/benchmark/chirp_login_client --host 127.0.0.1 --port "${APP_PORT}" \
    --token user_b_app --device dev_b_app --platform pc --expect_notify_ms 15000 > "${B3_LOG}" 2>&1
  B3_RC=$?
  set -e
  if [[ "${B3_RC}" != "0" ]] || ! grep -q "notify from=user_a" "${B3_LOG}" || ! grep -q "content=app-edge-offline-hello" "${B3_LOG}"; then
    echo "错误: B 经 app_gateway 登录后未收到离线补投递 (rc=${B3_RC},chat 管道未生效或 A3 消息未入队)"
    cat "${B3_LOG}" || true
    exit 1
  fi

  echo ""
  echo "[edge] WP-8 self-service via app_gateway (login through app_auth; subscribe/unread relayed by the sg peer into app_chat)"
  set +e
  timeout 30 ./build/tools/benchmark/chirp_wp8_client --host 127.0.0.1 --port "${APP_PORT}" \
    --user wp8_user --game game_alpha --channel world > "${WP8_LOG}" 2>&1
  WP8_RC=$?
  set -e
  if [[ "${WP8_RC}" != "0" ]] || ! grep -q "wp8 self-service chain ok" "${WP8_LOG}"; then
    echo "错误: WP-8 自服务链失败 (rc=${WP8_RC},sg peer → app_chat 信任门 → 目录 RPC 应全绿)"
    cat "${WP8_LOG}" || true
    exit 1
  fi

  echo ""
  echo "gateway log: ${GW_LOG}"
  tail -n 20 "${GW_LOG}" || true
  echo ""
  echo "chat log: ${CHAT_LOG}"
  tail -n 20 "${CHAT_LOG}" || true
  echo ""
  echo "B2 client log: ${B2_LOG}"
  cat "${B2_LOG}" || true
elif [[ "${1:-}" == "--smoke-jwt" ]]; then
  # 统一登录/会话语义(P1 收尾)进程级 E2E:auth-enhanced 校验 HS256 JWT
  # (--jwt_secret,含强制 exp),chat 用同一个 secret 本地验签(--token_secret),
  # 客户端原始 token 经 gateway -> ChatBridge -> chat 全链路逐字透传。
  # auth 不带 --allow_scaffold_login:scaffold token 必须被拒(AUTH_FAILED=3)。
  #
  # 不需要 redis-server:chat 不配限流器/redis 走内存兜底,auth 的 Redis
  # fail-open 且 LOGIN_REQ 路径不碰限流。唯一硬依赖是 MySQL(auth-enhanced
  # 启动即连库),与 --smoke 现状一致。
  AUTH_PORT="${AUTH_PORT:-$(pick_port)}"
  CHAT_PORT="${CHAT_PORT:-$(pick_port)}"
  CHAT_WS_PORT="${CHAT_WS_PORT:-$(pick_port)}"
  GW_PORT="${GW_PORT:-$(pick_port)}"
  GW_WS_PORT="${GW_WS_PORT:-$(pick_port)}"
  JWT_SECRET="${JWT_SECRET:-jwt_smoke_secret}"

  AUTH_LOG="${AUTH_LOG:-/tmp/chirp_app_auth_smoke_jwt.log}"
  CHAT_LOG="${CHAT_LOG:-/tmp/chirp_chat_smoke_jwt.log}"
  GW_LOG="${GW_LOG:-/tmp/chirp_game_sdk_gateway_smoke_jwt.log}"
  C1_LOG="${C1_LOG:-/tmp/chirp_jwt_c1_chat_scaffold.log}"
  C2_LOG="${C2_LOG:-/tmp/chirp_jwt_c2_gw_scaffold.log}"
  C3_LOG="${C3_LOG:-/tmp/chirp_jwt_c3_wrong_secret.log}"
  C4_LOG="${C4_LOG:-/tmp/chirp_jwt_c4_chat_jwt.log}"
  A_LOG="${A_LOG:-/tmp/chirp_jwt_a_send.log}"
  B_LOG="${B_LOG:-/tmp/chirp_jwt_b_refill.log}"

  ./build/services/app/auth/chirp_app_auth --port "${AUTH_PORT}" --jwt_secret "${JWT_SECRET}" "${MYSQL_ARGS[@]+"${MYSQL_ARGS[@]}"}" > "${AUTH_LOG}" 2>&1 &
  AUTH_PID=$!

  ./build/services/shared/chat/chirp_chat --port "${CHAT_PORT}" --ws_port "${CHAT_WS_PORT}" \
    --token_secret "${JWT_SECRET}" --gateway_service_secret edge-jwt-secret \
    "${MYSQL_ARGS[@]+"${MYSQL_ARGS[@]}"}" > "${CHAT_LOG}" 2>&1 &
  CHAT_PID=$!

  ./build/services/game/sdk_gateway/chirp_game_sdk_gateway --port "${GW_PORT}" --ws_port "${GW_WS_PORT}" \
    --auth_host 127.0.0.1 --auth_port "${AUTH_PORT}" \
    --chat_host 127.0.0.1 --chat_port "${CHAT_PORT}" --chat_service_secret edge-jwt-secret > "${GW_LOG}" 2>&1 &
  GW_PID=$!

  cleanup() {
    stop_proc "${GW_PID:-}" "${CHAT_PID:-}" "${AUTH_PID:-}"
  }
  trap cleanup EXIT

  wait_port "${AUTH_PORT}" chirp_app_auth "${AUTH_LOG}"
  wait_port "${CHAT_PORT}" chirp_chat "${CHAT_LOG}"
  wait_port "${GW_PORT}" chirp_game_sdk_gateway "${GW_LOG}"

  # 1) 直连 chat:--token_secret 生效,scaffold token 被拒(不再有兜底放行)
  echo ""
  echo "[jwt] direct chat rejects a scaffold token"
  set +e
  timeout 30 ./build/tools/benchmark/chirp_login_client --host 127.0.0.1 --port "${CHAT_PORT}" \
    --token user_junk --device dev_junk > "${C1_LOG}" 2>&1
  C1_RC=$?
  set -e
  if [[ "${C1_RC}" != "0" ]]; then
    echo "错误: C1 直连进程异常退出 (rc=${C1_RC})"
    cat "${C1_LOG}" || true
    exit 1
  fi
  if ! grep -q "code=3" "${C1_LOG}"; then
    echo "错误: chat 未拒绝 scaffold token(预期 code=3/AUTH_FAILED)"
    cat "${C1_LOG}" || true
    exit 1
  fi

  # 2) 经 gateway 打收紧后的 auth:scaffold token 同样被拒
  echo ""
  echo "[jwt] tightened auth rejects a scaffold token (via gateway)"
  set +e
  timeout 30 ./build/tools/benchmark/chirp_login_client --host 127.0.0.1 --port "${GW_PORT}" \
    --token user_junk --device dev_junk > "${C2_LOG}" 2>&1
  C2_RC=$?
  set -e
  if [[ "${C2_RC}" != "0" ]]; then
    echo "错误: C2 进程异常退出 (rc=${C2_RC})"
    cat "${C2_LOG}" || true
    exit 1
  fi
  if ! grep -q "code=3" "${C2_LOG}"; then
    echo "错误: auth 未拒绝 scaffold token(预期 code=3/AUTH_FAILED,scaffold login 默认关闭)"
    cat "${C2_LOG}" || true
    exit 1
  fi

  # 3) 错 secret 签的 JWT 被拒:证明是真验签,不是「长得像 JWT 就放行」
  echo ""
  echo "[jwt] wrong-secret JWT rejected by auth"
  set +e
  timeout 30 ./build/tools/benchmark/chirp_login_client --host 127.0.0.1 --port "${GW_PORT}" \
    --jwt_user user_x --jwt_secret totally_wrong_secret --device dev_x > "${C3_LOG}" 2>&1
  C3_RC=$?
  set -e
  if [[ "${C3_RC}" != "0" ]]; then
    echo "错误: C3 进程异常退出 (rc=${C3_RC})"
    cat "${C3_LOG}" || true
    exit 1
  fi
  if ! grep -q "code=3" "${C3_LOG}"; then
    echo "错误: auth 未拒绝错误 secret 的 JWT(预期 code=3/AUTH_FAILED)"
    cat "${C3_LOG}" || true
    exit 1
  fi

  # 4) 直连 chat 接受同一 secret 自签的 JWT:两端验签格式同构的独立证明
  echo ""
  echo "[jwt] direct chat accepts the self-signed JWT"
  timeout 30 ./build/tools/benchmark/chirp_login_client --host 127.0.0.1 --port "${CHAT_PORT}" \
    --jwt_user user_a --jwt_secret "${JWT_SECRET}" --device dev_probe > "${C4_LOG}" 2>&1
  grep -q "code=0" "${C4_LOG}"

  # 5) A 以 JWT 经 gateway 登录并发私聊给离线的 B。send code=0 只有在
  # bridge 的服务认证 + 登录重放(透传原始 JWT)被 chat 接受后才可能出现,
  # 这一步同时钉死「auth 验过 + chat 用同一 secret 验过 + token 逐字透传」。
  echo ""
  echo "[jwt] A login via gateway with JWT + send to offline B"
  timeout 30 ./build/tools/benchmark/chirp_login_client --host 127.0.0.1 --port "${GW_PORT}" \
    --jwt_user user_a --jwt_secret "${JWT_SECRET}" --device dev_a \
    --send_text "jwt-offline-hello" --peer_user user_b --sender user_a > "${A_LOG}" 2>&1
  grep -q "code=0" "${A_LOG}"
  # B is offline by design: basic answers TARGET_OFFLINE(6, queued),
  # enhanced answers OK; the refill assertion below is the real proof.
  grep -Eq "send code=(0|6)" "${A_LOG}"

  # 6) B 以 JWT 经 gateway 登录,离线补投递经管道回到客户端
  echo ""
  echo "[jwt] B login via gateway (offline refill rides the pipe back)"
  set +e
  timeout 30 ./build/tools/benchmark/chirp_login_client --host 127.0.0.1 --port "${GW_PORT}" \
    --jwt_user user_b --jwt_secret "${JWT_SECRET}" --device dev_b --expect_notify_ms 15000 > "${B_LOG}" 2>&1
  B_RC=$?
  set -e
  if [[ "${B_RC}" != "0" ]] || ! grep -q "notify from=user_a" "${B_LOG}" \
     || ! grep -q "content=jwt-offline-hello" "${B_LOG}"; then
    echo "错误: B 经 gateway 登录后未收到离线补投递 (rc=${B_RC})"
    cat "${B_LOG}" || true
    exit 1
  fi

  echo ""
  echo "gateway log: ${GW_LOG}"
  tail -n 20 "${GW_LOG}" || true
  echo ""
  echo "chat log: ${CHAT_LOG}"
  tail -n 20 "${CHAT_LOG}" || true
  echo ""
  echo "auth log: ${AUTH_LOG}"
  tail -n 20 "${AUTH_LOG}" || true
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
