#!/bin/sh
set -eu

# CHIRP_BIN:dev 单服务镜像(Dockerfile.service)构建期烧入的执行体,优先。
# CHIRP_SERVICE:多服务演示镜像(compose 注入)按名选 /usr/local/bin/chirp_*。
bin="${CHIRP_BIN:-/usr/local/bin/chirp_${CHIRP_SERVICE:-chat}}"
exec "$bin" "$@"
