#!/bin/bash
# 收走桌面端走查拓扑（见 stack-up.sh）。
set -uo pipefail
RUN_DIR=/tmp/chirp-desktop-stack
for name in social voice party gateway chat auth redis; do
  pid_file="${RUN_DIR}/${name}.pid"
  if [[ -f "${pid_file}" ]]; then
    pid="$(cat "${pid_file}")"
    kill -TERM "${pid}" 2>/dev/null || true
    sleep 0.3
    kill -KILL "${pid}" 2>/dev/null || true
    rm -f "${pid_file}"
  fi
done
echo "[stack] down"
