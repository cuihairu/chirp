#!/usr/bin/env bash
# test_services.sh 共享函数库:构建产物预检、端口去重选取、端口/日志/
# redis-claim 等待与进程停机。被 test_services.sh source;不放任何
# 主流程语句。

# 检查构建产物是否存在
require_bin() {
  if [ ! -f "$1" ]; then
    echo "错误: $1 未构建"
    exit 1
  fi
}

# pick_port 去重 + 占用探针:bind(0)→close 的端口在 close 后立即回到内核
# 临时端口池,连调可能拿到同一端口(本机实测 500 连摇 9 个重复,edge 腿 7 次
# 选取单腿撞车率约 12%,故 CI 偶发一腿红)。CI run 38021358618 实证:edge 腿
# CHAT_WS_PORT 与 REDIS_PORT 同得 42171,redis 先占,chirp_chat 的 WS 监听
# 再绑撞 Address already in use → std::system_error → terminate/Aborted,
# C1 直连登录随之失败。已发出端口记入每进程独占文件($$ 后缀)去重;connect
# 探活命中(已有进程在听)同样重摇。
PICK_PORT_FILE="/tmp/chirp_pick_ports.$$"
pick_port() {
  python3 - "${PICK_PORT_FILE}" <<'PY'
import socket, sys
issued = set()
try:
    with open(sys.argv[1]) as f:
        for line in f:
            line = line.strip()
            if line:
                issued.add(int(line))
except FileNotFoundError:
    pass
for _ in range(200):
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    if port in issued:
        continue
    # 端口若已有进程在听(外部占用)则重摇;connect 被拒=无人听
    probe = socket.socket()
    probe.settimeout(0.2)
    try:
        probe.connect(("127.0.0.1", port))
        continue
    except OSError:
        pass
    finally:
        probe.close()
    with open(sys.argv[1], "a") as f:
        f.write(f"{port}\n")
    print(port)
    break
else:
    raise SystemExit("pick_port: 200 次重摇仍无唯一空闲端口")
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
