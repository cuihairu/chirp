#!/bin/bash
# Chirp 桌面端本地走查拓扑：隔离 redis + app_auth(scaffold) + chat + app_gateway。
# 端口全部私有化（不碰共享 6379 redis 与 5200 默认口），对应桌面端登录地址
# ws://127.0.0.1:5201。`stack-down.sh` 或 kill 组进程收尾。
set -euo pipefail
cd "$(dirname "$0")/../../.."

BUILD=./build
RUN_DIR=/tmp/chirp-desktop-stack
mkdir -p "${RUN_DIR}"

SECRET=edge-secret

# 1) 隔离 redis（独立端口 + 独立数据目录，避免共享实例污染）
"${REDIS_SERVER_BIN:-redis-server}" --port 6390 --save '' --appendonly no \
  --dir "${RUN_DIR}" --daemonize no > "${RUN_DIR}/redis.log" 2>&1 &
echo $! > "${RUN_DIR}/redis.pid"

# 2) App 认证（scaffold 登录：用户 ID 即 token）
"${AUTH_BIN:-${BUILD}/services/app/auth/chirp_app_auth}" \
  --port 5300 --jwt_secret dev_secret --allow_scaffold_login 1 \
  > "${RUN_DIR}/auth.log" 2>&1 &
echo $! > "${RUN_DIR}/auth.pid"

# 3) Chat（增强形态；本机 MariaDB 127.0.0.1:3306 chirp 库 + 上面隔离的 redis）
"${CHAT_BIN:-${BUILD}/services/shared/chat/chirp_chat}" \
  --port 5310 --ws_port 5311 \
  --redis_host 127.0.0.1 --redis_port 6390 \
  --gateway_service_secret "${SECRET}" \
  > "${RUN_DIR}/chat.log" 2>&1 &
echo $! > "${RUN_DIR}/chat.pid"

# 4) App 平面边缘网关（登录转发 auth、2xxx 经 ServiceBridge 中继 chat、
#    WP-8 自服务经 ServerGatewayPeer 直发 hub）
"${BUILD}/services/app/sdk_gateway/chirp_app_sdk_gateway" \
  --port 5320 --ws_port 5201 \
  --auth_host 127.0.0.1 --auth_port 5300 \
  --chat_host 127.0.0.1 --chat_port 5310 --chat_service_secret "${SECRET}" \
  --sg_host 127.0.0.1 --sg_port 5310 --sg_secret "${SECRET}" \
  > "${RUN_DIR}/gateway.log" 2>&1 &
echo $! > "${RUN_DIR}/gateway.pid"

wait_port() {
  local port="$1" name="$2" log="$3"
  for _ in $(seq 1 150); do
    if (exec 3<>"/dev/tcp/127.0.0.1/${port}") 2>/dev/null; then
      echo "[stack] ${name} ready on ${port}"
      return 0
    fi
    sleep 0.1
  done
  echo "错误: ${name} 端口 ${port} 未就绪"; tail -n 30 "${log}" || true; return 1
}

wait_port 6390 redis "${RUN_DIR}/redis.log"
wait_port 5300 app_auth "${RUN_DIR}/auth.log"
wait_port 5310 chat "${RUN_DIR}/chat.log"
wait_port 5201 app_gateway "${RUN_DIR}/gateway.log"

echo "[stack] 桌面端登录地址: ws://127.0.0.1:5201 （scaffold：用户 ID 即凭据）"

# 5) Party / Voice 实验平面（组队与语音信令入口的真实后端）
"${BUILD}/services/party/chirp_party" \
  --port 7500 --ws_port 7501 --redis_port 6390 \
  > "${RUN_DIR}/party.log" 2>&1 &
echo $! > "${RUN_DIR}/party.pid"
"${BUILD}/services/voice/chirp_voice" \
  --port 9000 --ws_port 9001 --redis_port 6390 \
  > "${RUN_DIR}/voice.log" 2>&1 &
echo $! > "${RUN_DIR}/voice.pid"
wait_port 7501 party "${RUN_DIR}/party.log"
wait_port 9001 voice "${RUN_DIR}/voice.log"
echo "[stack] party ws://127.0.0.1:7501  voice ws://127.0.0.1:9001"

"${BUILD}/services/social/chirp_social" \
  --port 8000 --ws_port 8001 --redis_port 6390 \
  > "${RUN_DIR}/social.log" 2>&1 &
echo $! > "${RUN_DIR}/social.pid"
wait_port 8001 social "${RUN_DIR}/social.log"
echo "[stack] social ws://127.0.0.1:8001"
