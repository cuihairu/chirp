#!/usr/bin/env bash
# 单档「网关并发连接」基准:起 basic chat + game_sdk_gateway,用 chirp_load_client
# 打 N 条并发登录连接环形互发私聊,输出吞吐/RTT 分位数、峰值内存与两服务的 CPU 归因。
#
# 用法:
#   BIN_DIR=./build-rel bash tools/benchmark/run_capacity_bench.sh <conns> [rounds] [interval_ms] [ramp_ms]
#   BIN_DIR 默认 ./build;二进制建议用 Release 构建(coverage/Debug 插桩会压低数字)。
#
# ramp_ms = 0 时全部连接同瞬发起(连接冲击上限);ramp_ms > 0 把建连铺开到该窗口内,
# 测的是「N 条已建立的并发连接」而非「同瞬 N 连接」——网关到 chat 是每客户端一条
# 管道且握手串行(5s 超时),同瞬万级建连会打穿握手,得到的不是并发容量而是握手崩溃。
#
# CPU 隔离:chat 与 gateway 都是单线程 io.run()(services/shared/chat/src/main.cc、
# services/game/sdk_gateway/src/main.cc),而压测客户端每条连接一个线程。万级客户端
# 会把同机 CPU 抢光,把服务端 RTT 抬成排队时间而不是服务能力。默认用 taskset 把服务
# (SRV_CPUS)与客户端(CLI_CPUS)分核。taskset 以 execvp 接管子进程,故 $! 仍是服务
# 自身 PID,端口归属断言不受影响。PIN=0 可关掉亲和(仅用于独占机器的对照实验)。
#
# 结果解读与多档阶梯(1k/5k/10k/12k)见 docs/design-notes/capacity_benchmark.md。
# 约束:每用户私聊节奏 1s(服务端硬编码),interval_ms >= 1100 才能避开 RATE_LIMITED;
# 内容逐轮变化由工具内置,规避重复禁言。
set -euo pipefail

CONNS="${1:?usage: run_capacity_bench.sh <conns> [rounds] [interval_ms] [ramp_ms]}"
ROUNDS="${2:-20}"
INTERVAL_MS="${3:-1100}"
RAMP_MS="${4:-0}"

BIN_DIR="${BIN_DIR:-./build}"
CHAT_BIN="${CHAT_BIN:-${BIN_DIR}/services/shared/chat/chirp_chat}"
GW_BIN="${GW_BIN:-${BIN_DIR}/services/game/sdk_gateway/chirp_game_sdk_gateway}"
LOAD_BIN="${LOAD_BIN:-${BIN_DIR}/tools/benchmark/chirp_load_client}"
for b in "${CHAT_BIN}" "${GW_BIN}" "${LOAD_BIN}"; do
  if [ ! -x "${b}" ]; then
    echo "错误: 未找到 ${b}(先构建,或用 BIN_DIR 指定)" >&2
    exit 1
  fi
done

CHAT_PORT="${CHAT_PORT:-17800}"
CHAT_WS_PORT="${CHAT_WS_PORT:-17801}"
GW_PORT="${GW_PORT:-17802}"
GW_WS_PORT="${GW_WS_PORT:-17803}"
SECRET="${BENCH_SECRET:-bench-secret}"
OUT_DIR="${OUT_DIR:-/tmp/chirp-bench}"
mkdir -p "${OUT_DIR}"

# 并发/端口守卫(bench_guard.sh):先排他拿机器级基准锁,再逐个预检本档要独占的
# 端口。并行两轮基准会互踩端口、互抢 CPU,两边的数字都不可归因;而端口被别的
# 会话占住时,连通性探测会连到「对方」的服务上,产出看起来正常的废数据。
# 外来占用者一律报错退出(共享机纪律:绝不代杀别人的活体进程)。
. "$(dirname "${BASH_SOURCE[0]}")/bench_guard.sh"
bench_acquire_lock || exit 1
bench_preflight_ports "${CHAT_PORT}" "${CHAT_WS_PORT}" "${GW_PORT}" "${GW_WS_PORT}" || exit 1

# CPU 亲和:服务与压测客户端分核,避免万级客户端线程把服务端的 CPU 抢走。
PIN="${PIN:-1}"
SRV_CPUS="${SRV_CPUS:-0-3}"
CLI_CPUS="${CLI_CPUS:-5-13}"
T_PIN=0
if [ "${PIN}" = "1" ] && command -v taskset >/dev/null 2>&1; then
  T_PIN=1
else
  echo "提示: 未启用 taskset 亲和(服务与客户端共享全部核,数字不可归因)" >&2
fi

# 端口预检:端口被占时必须直接失败,不能继续。
# 否则新起的服务会 bind 失败 abort,而 wait_port 探测到的是「占用者」的端口,
# 于是压测客户端把全部连接打到一个来源不明的旧进程上——数字全废且无从察觉
# (本脚本曾因此跑出「5000/5000 在线、登录 p99=42s」的假数据)。FORCE=1 可强杀占用者。
port_holder() {
  ss -ltnpH "sport = :$1" 2>/dev/null |
    grep -oP 'pid=\K[0-9]+' | head -1
}

preflight_ports() {
  local port name pid clash=0
  for pair in "${CHAT_PORT}:chirp_chat" "${CHAT_WS_PORT}:chirp_chat" \
              "${GW_PORT}:chirp_game_sdk_gateway" "${GW_WS_PORT}:chirp_game_sdk_gateway"; do
    port="${pair%%:*}"
    name="${pair#*:}"
    pid="$(port_holder "${port}")"
    [ -n "${pid}" ] || continue
    if [ "${FORCE:-0}" = "1" ]; then
      echo "预检: 端口 ${port} 被 pid ${pid} 占用,FORCE=1 强杀" >&2
      kill -9 "${pid}" 2>/dev/null || true
      sleep 1
      pid="$(port_holder "${port}")"
      if [ -n "${pid}" ]; then
        echo "错误: 端口 ${port} 仍被 pid ${pid} 占用,放弃(勿并行跑两轮基准)" >&2
        clash=1
      fi
    else
      echo "错误: 端口 ${port} 已被 pid ${pid} 占用(${name} 需要它)。" >&2
      echo "      上一轮基准的孤儿进程还在,或另一轮基准正在跑——并行会互相踩端口。" >&2
      echo "      处理: kill ${pid}   或   FORCE=1 重新运行本脚本自动清理。" >&2
      clash=1
    fi
  done
  return "${clash}"
}

preflight_ports || exit 1

wait_port() {
  local port="$1" name="$2" log="$3" timeout_s=30
  local deadline=$((SECONDS + timeout_s))
  while (( SECONDS < deadline )); do
    if (exec 3<>"/dev/tcp/127.0.0.1/${port}") 2>/dev/null; then
      return 0
    fi
    local dead=0
    local pid
    for pid in "${SERVER_PIDS[@]}"; do
      kill -0 "${pid}" 2>/dev/null || dead=1
    done
    if (( dead )); then
      echo "错误: ${name} 启动即退出,日志尾部:" >&2
      tail -n 20 "${log}" >&2 || true
      exit 1
    fi
    sleep 0.1
  done
  echo "错误: ${name} 30s 内未监听 ${port}" >&2
  exit 1
}

# 端口打开 != 服务活着:bind 冲突时服务会 abort,而占用者的端口让探测假成功。
# 这里再确认「监听者就是本轮起的子进程」,把这种情况当场打掉而不是产出废数字。
assert_owns_port() {
  local port="$1" name="$2" pid="$3" log="$4" holder
  holder="$(port_holder "${port}")"
  if [ "${holder}" != "${pid}" ]; then
    echo "错误: 端口 ${port} 的监听者是 pid ${holder:-未知},不是本轮 ${name}(pid ${pid})" >&2
    echo "      —— 有别的进程抢占了端口,本轮数据作废。日志尾部:" >&2
    tail -n 20 "${log}" >&2 || true
    exit 1
  fi
}

peak_rss_kb() {
  # VmHWM = 进程生命周期峰值常驻内存
  awk '/^VmHWM/{print $2}' "/proc/$1/status" 2>/dev/null || echo 0
}

# 进程存活期累计 CPU 核·秒(utime+stime,clk_tck=100)。机器被多会话共用时 loadavg
# 说明不了「这轮数字被谁拖累」,但本轮两个服务自己的 CPU 增量是无争议的——
# 用它给出每千连接 CPU 成本。服务是单线程,>1 核·秒即说明有后台线程在干活。
cpu_seconds() {
  awk '{print ($14 + $15) / 100}' "/proc/$1/stat" 2>/dev/null || echo 0
}

CHAT_LOG="${OUT_DIR}/bench_chat.log"
GW_LOG="${OUT_DIR}/bench_gateway.log"
LOAD_LOG="${OUT_DIR}/bench_load_conns${CONNS}.log"

# Redis 开关:REDIS_HOST 为空则 chat 走 MySQL-only(fail-open),消息每条都落 MySQL。
# 报容量数字时应显式指定,与生产形态一致(热层 Redis + 冷层 MySQL)。
CHAT_REDIS_ARGS=()
if [ -n "${REDIS_HOST:-}" ]; then
  CHAT_REDIS_ARGS=(--redis_host "${REDIS_HOST}"
                   --redis_port "${REDIS_PORT:-6379}")
fi
# MySQL 同理:MYSQL_HOST 为空则用二进制默认(127.0.0.1:3306,连接失败时
# 每条消息都同步重试落库,会把 chat 单线程 io 拖死——见 capacity_benchmark.md)。
if [ -n "${MYSQL_HOST:-}" ]; then
  CHAT_REDIS_ARGS+=(--mysql_host "${MYSQL_HOST}"
                    --mysql_port "${MYSQL_PORT:-3306}"
                    --mysql_user "${MYSQL_USER:-chirp}"
                    --mysql_password "${MYSQL_PASSWORD:-chirp_password}"
                    --mysql_database "${MYSQL_DATABASE:-chirp}")
fi

if [ "${T_PIN}" = "1" ]; then
  taskset -c "${SRV_CPUS}" "${CHAT_BIN}" --port "${CHAT_PORT}" --ws_port "${CHAT_WS_PORT}" \
    "${CHAT_REDIS_ARGS[@]}" \
    --gateway_service_secret "${SECRET}" > "${CHAT_LOG}" 2>&1 &
  CHAT_PID=$!
  taskset -c "${SRV_CPUS}" "${GW_BIN}" --port "${GW_PORT}" --ws_port "${GW_WS_PORT}" \
    --chat_host 127.0.0.1 --chat_port "${CHAT_PORT}" \
    --chat_service_secret "${SECRET}" > "${GW_LOG}" 2>&1 &
  GW_PID=$!
else
  "${CHAT_BIN}" --port "${CHAT_PORT}" --ws_port "${CHAT_WS_PORT}" \
    "${CHAT_REDIS_ARGS[@]}" \
    --gateway_service_secret "${SECRET}" > "${CHAT_LOG}" 2>&1 &
  CHAT_PID=$!
  "${GW_BIN}" --port "${GW_PORT}" --ws_port "${GW_WS_PORT}" \
    --chat_host 127.0.0.1 --chat_port "${CHAT_PORT}" \
    --chat_service_secret "${SECRET}" > "${GW_LOG}" 2>&1 &
  GW_PID=$!
fi
SERVER_PIDS=("${CHAT_PID}" "${GW_PID}")
cleanup() { kill "${CHAT_PID}" "${GW_PID}" 2>/dev/null || true; wait 2>/dev/null || true; }
trap cleanup EXIT

wait_port "${CHAT_PORT}" chirp_chat "${CHAT_LOG}"
wait_port "${GW_PORT}" chirp_game_sdk_gateway "${GW_LOG}"
assert_owns_port "${CHAT_PORT}" chirp_chat "${CHAT_PID}" "${CHAT_LOG}"
assert_owns_port "${GW_PORT}" chirp_game_sdk_gateway "${GW_PID}" "${GW_LOG}"

LOAD_BEFORE=$(cut -d' ' -f1-3 /proc/loadavg)
set +e
if [ "${T_PIN}" = "1" ]; then
  taskset -c "${CLI_CPUS}" "${LOAD_BIN}" --host 127.0.0.1 --port "${GW_PORT}" \
    --conns "${CONNS}" --rounds "${ROUNDS}" --interval-ms "${INTERVAL_MS}" \
    --ramp-ms "${RAMP_MS}" --barrier "${BARRIER:-on}" \
    ${BARRIER_QUIET_MS:+--barrier-quiet-ms "${BARRIER_QUIET_MS}"} \
    ${TRACE_STRIDE:+--trace-stride "${TRACE_STRIDE}"} \
    --prefix "${USER_PREFIX:-bench${CONNS}_log}" > "${LOAD_LOG}" 2>&1
else
  "${LOAD_BIN}" --host 127.0.0.1 --port "${GW_PORT}" \
    --conns "${CONNS}" --rounds "${ROUNDS}" --interval-ms "${INTERVAL_MS}" \
    --ramp-ms "${RAMP_MS}" --barrier "${BARRIER:-on}" \
    ${BARRIER_QUIET_MS:+--barrier-quiet-ms "${BARRIER_QUIET_MS}"} \
    ${TRACE_STRIDE:+--trace-stride "${TRACE_STRIDE}"} \
    --prefix "${USER_PREFIX:-bench${CONNS}_log}" > "${LOAD_LOG}" 2>&1
fi
LOAD_RC=$?
set -e
LOAD_AFTER=$(cut -d' ' -f1-3 /proc/loadavg)

CHAT_RSS=$(peak_rss_kb "${CHAT_PID}")
GW_RSS=$(peak_rss_kb "${GW_PID}")
CHAT_CPU=$(cpu_seconds "${CHAT_PID}")
GW_CPU=$(cpu_seconds "${GW_PID}")
# 网关日志里的握手超时数:非 0 说明建连速率超过了 chat 侧管道握手的串行处理
# 上限,受影响的连接无法发送(数字见 capacity_benchmark.md 的瓶颈分析)。
HANDSHAKE_TIMEOUTS=$(grep -c 'handshake timed out' "${GW_LOG}" 2>/dev/null || echo 0)

echo "=== bench conns=${CONNS} rounds=${ROUNDS} interval_ms=${INTERVAL_MS} ramp_ms=${RAMP_MS} barrier=${BARRIER:-on} ==="
cat "${LOAD_LOG}"
echo "load_client_rc=${LOAD_RC} loadavg ${LOAD_BEFORE} -> ${LOAD_AFTER}"
echo "peak_rss: chat=${CHAT_RSS}KB gateway=${GW_RSS}KB"
echo "cpu_core_seconds: chat=${CHAT_CPU} gateway=${GW_CPU} (srv_cpus=${SRV_CPUS} cli_cpus=${CLI_CPUS})"
echo "handshake_timeouts=${HANDSHAKE_TIMEOUTS}"
echo "logs: ${LOAD_LOG} ${CHAT_LOG} ${GW_LOG}"
