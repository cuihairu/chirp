#!/usr/bin/env bash
# Lockstep guard: apps/shared/protocol（companion 应用协议层）与 sdks/ts（游戏
# SDK 协议层）是两份逐字镜像 —— sdks/ 是游戏集成面，apps 不依赖 sdks（2026-10
# 边界裁决），跨端一致性不靠共享实现，靠「同名文件逐字节相同」+ 两侧各自的
# 测试。任何一侧单独改动会让本脚本变红；改协议实现/测试必须两份同步改。
set -euo pipefail
cd "$(dirname "$0")/.."

A=apps/shared/protocol/src
B=sdks/ts/src

# 镜像契约清单（两目录各有一份、内容必须逐字节相同）。新镜像文件加入这里。
MIRRORED=(
  chat_pipeline.ts chirp_client.ts errors.ts frame.ts gzip.ts hooks.ts
  msg_map.ts word_filter.ts word_filter_sync.ts
  chat_pipeline.test.ts chirp_client.test.ts errors.test.ts frame.test.ts
  gzip.test.ts msg_map.test.ts proto.test.ts word_filter.test.ts
  word_filter_sync.test.ts
)

fail=0
for f in "${MIRRORED[@]}"; do
  for d in "$A" "$B"; do
    if [ ! -f "$d/$f" ]; then
      echo "drift: mirror file missing on one side: $d/$f" >&2
      fail=1
    fi
  done
  if [ -f "$A/$f" ] && [ -f "$B/$f" ] && ! cmp -s "$A/$f" "$B/$f"; then
    echo "drift: $f differs between $A and $B" >&2
    diff -u "$B/$f" "$A/$f" | head -40 >&2 || true
    fail=1
  fi
done

# 漏网同名文件：两侧都存在但没登记进 MIRRORED 的 .ts 也不许悄悄漂移。
unregistered=$(comm -12 <(cd "$A" && ls *.ts | LC_ALL=C sort) <(cd "$B" && ls *.ts | LC_ALL=C sort) \
  | grep -vFf <(printf '%s\n' "${MIRRORED[@]}" | LC_ALL=C sort) || true)
if [ -n "$unregistered" ]; then
  echo "drift: same-named files not registered in MIRRORED (add them):" >&2
  echo "$unregistered" >&2
  fail=1
fi

if [ "$fail" -ne 0 ]; then
  echo "protocol lockstep broken — apply the same change to BOTH apps/shared/protocol/src and sdks/ts/src" >&2
  exit 1
fi
echo "protocol lockstep OK: ${#MIRRORED[@]} mirrored files identical"
