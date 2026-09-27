#!/usr/bin/env bash
# bench_guard.sh — tools/benchmark 下所有基准脚本共用的并发/端口守卫。
# 被 source,不单独执行。
#
# 防的两类事故都真实发生过(见 docs/design-notes/capacity_benchmark.md):
#  1) 两轮基准并行:端口被对方占住,本轮服务 bind 失败 abort,而 /dev/tcp 连通
#     性探测到的是「对方」的端口 → 压测全打在别人的进程上,还能产出
#     「5000/5000 在线、登录 p99=42s」这种看起来正常的废数据。
#  2) 端口占用者是别的会话正在跑的活体基准(不是孤儿):无差别 FORCE=1 强杀
#     会直接踩坏别人的服务。共享开发机上这是破坏性操作。
#
# 守卫两件套:
#  - bench_acquire_lock:flock 互斥,同机所有走本目录脚本的基准互相排队
#    (测时并发即污染,本就该互斥)。锁随进程退出自动释放,不留死锁文件。
#    LOCK_WAIT=1 排队等(LOCK_TIMEOUT 秒,默认 3600);默认忙则报错退出。
#  - bench_preflight_ports:逐端口查监听者。空闲放行;被占则只有「本用户 +
#    基准二进制名」的孤儿在 FORCE=1 时才允许清理,其余(别的 uid、别的服务)
#    一律报错退出,绝不代杀外来进程。

# 锁文件按机器级共享(同一 uid 之外也互斥,靠端口预检兜底)。
: "${BENCH_LOCK:=/tmp/chirp_bench.lock}"

bench_acquire_lock() {
  exec 9>"${BENCH_LOCK}" || {
    echo "错误: 无法打开锁文件 ${BENCH_LOCK}" >&2
    return 1
  }
  if [ "${LOCK_WAIT:-0}" = "1" ]; then
    if ! flock -w "${LOCK_TIMEOUT:-3600}" 9; then
      echo "错误: 等待基准锁 ${BENCH_LOCK} 超时(${LOCK_TIMEOUT:-3600}s)," >&2
      echo "      另一轮基准还在跑。用 LOCK_TIMEOUT= 放宽或等它结束。" >&2
      return 1
    fi
  elif ! flock -n 9; then
    local holder
    holder=$(fuser "${BENCH_LOCK}" 2>/dev/null | awk '{print $1}')
    echo "错误: 另一轮基准正持有锁 ${BENCH_LOCK}${holder:+ (pid ${holder})}。" >&2
    echo "      并行跑基准会互踩端口、互抢 CPU,两边的数字都不可归因。" >&2
    echo "      处理:等它跑完,或 LOCK_WAIT=1 重新运行本脚本排队等待。" >&2
    echo "      (若对方是被 kill -9 的进程,flock 会随其死亡自动释放,无需清锁。)" >&2
    return 1
  fi
  return 0
}

bench_port_holder() {
  # 只回一个 pid(监听者);ss 对 LISTEN 行能带出 pid/comm,跨用户也可见。
  ss -ltnpH "sport = :$1" 2>/dev/null |
    grep -oP 'pid=\K[0-9]+' | head -1
}

bench_own_binary() {
  # 判定 pid 是否「本用户起的基准二进制孤儿」——只清理这类,别的一律不碰。
  local pid="$1" uid bin exe
  uid=$(ps -o uid= -p "${pid}" 2>/dev/null | tr -d '[:space:]')
  [ "${uid}" = "$(id -u)" ] || return 1
  exe=$(readlink -f "/proc/${pid}/exe" 2>/dev/null) || return 1
  bin="${exe##*/}"
  case "${bin}" in
    chirp_chat|chirp_game_sdk_gateway|chirp_load_client) return 0 ;;
    *) return 1 ;;
  esac
}

bench_preflight_ports() {
  local port pid holder uid bin clash=0
  for port in "$@"; do
    pid="$(bench_port_holder "${port}")"
    [ -n "${pid}" ] || continue
    uid=$(ps -o uid= -p "${pid}" 2>/dev/null | tr -d '[:space:]')
    bin=$(readlink -f "/proc/${pid}/exe" 2>/dev/null)
    bin="${bin##*/}"
    if [ "${FORCE:-0}" = "1" ] && bench_own_binary "${pid}"; then
      echo "预检: 端口 ${port} 被本用户基准孤儿 ${bin}(pid ${pid})占用,FORCE=1 清理" >&2
      kill -9 "${pid}" 2>/dev/null || true
      sleep 1
      holder="$(bench_port_holder "${port}")"
      if [ -n "${holder}" ]; then
        echo "错误: 端口 ${port} 清理后仍被 pid ${holder} 占用(有东西在反复重起?)" >&2
        clash=1
        continue
      fi
      continue
    fi
    echo "错误: 端口 ${port} 已被 pid ${pid}(${bin:-未知二进制}, uid ${uid:-?})占用。" >&2
    echo "      ${port} 是本基准需要独占的端口。占用者不是本用户的基准孤儿," >&2
    echo "      出于共享机纪律(可能是别的会话的活体服务/基准)拒绝清理——" >&2
    echo "      继续跑会把连接打到别人的进程上,数字作废还踩别人一脚。" >&2
    echo "      处理:与对方协调,或换端口(如 CHAT_PORT=18100 CHAT_WS_PORT=18101 \\" >&2
    echo "            GW_PORT=18102 GW_WS_PORT=18103)。FORCE=1 仅对本用户孤儿生效。" >&2
    clash=1
  done
  return "${clash}"
}
