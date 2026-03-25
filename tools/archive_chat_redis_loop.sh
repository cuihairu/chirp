#!/bin/sh

set -eu

INTERVAL_SECS=60
ONCE=0
REDIS_HOST=127.0.0.1
REDIS_PORT=6379
MYSQL_CMD=
APPLY_ACK=0
INCLUDE_OFFLINE=1
HISTORY_TABLE=chat_messages
OFFLINE_TABLE=chat_offline_messages
ARCHIVER=
REDIS_CLI_BIN=
WORK_DIR=/tmp/chirp_archive_loop

resolve_archiver() {
  if [ -n "${ARCHIVER}" ]; then
    return 0
  fi
  if [ -x ./tools/archive_chat_redis.sh ]; then
    ARCHIVER=./tools/archive_chat_redis.sh
  else
    ARCHIVER=/usr/local/bin/archive_chat_redis.sh
  fi
}

resolve_redis_cli() {
  if [ -n "${REDIS_CLI_BIN}" ]; then
    return 0
  fi
  if command -v redis-cli >/dev/null 2>&1; then
    REDIS_CLI_BIN=$(command -v redis-cli)
  else
    REDIS_CLI_BIN=/usr/local/opt/redis/bin/redis-cli
  fi
}

usage() {
  cat <<EOF
usage: $0 [options]

options:
  --interval_secs N         Loop interval, default 60
  --once 0|1                Run once and exit, default 0
  --redis_host HOST         Redis host, default 127.0.0.1
  --redis_port PORT         Redis port, default 6379
  --mysql_cmd CMD           Shell command used to apply SQL
  --apply_ack 0|1           Delete archived Redis keys after successful mysql apply, default 0
  --include_offline 0|1     Export chat:offline:* in addition to history, default 1
  --history_table NAME      MySQL history table name, default chat_messages
  --offline_table NAME      MySQL offline table name, default chat_offline_messages
  --archiver PATH           Archive script path
  --redis_cli_bin PATH      redis-cli binary path
  --work_dir PATH           Temp working directory, default /tmp/chirp_archive_loop
EOF
}

while [ $# -gt 0 ]; do
  case "$1" in
    --interval_secs)
      INTERVAL_SECS="$2"
      shift 2
      ;;
    --once)
      ONCE="$2"
      shift 2
      ;;
    --redis_host)
      REDIS_HOST="$2"
      shift 2
      ;;
    --redis_port)
      REDIS_PORT="$2"
      shift 2
      ;;
    --mysql_cmd)
      MYSQL_CMD="$2"
      shift 2
      ;;
    --apply_ack)
      APPLY_ACK="$2"
      shift 2
      ;;
    --include_offline)
      INCLUDE_OFFLINE="$2"
      shift 2
      ;;
    --history_table)
      HISTORY_TABLE="$2"
      shift 2
      ;;
    --offline_table)
      OFFLINE_TABLE="$2"
      shift 2
      ;;
    --archiver)
      ARCHIVER="$2"
      shift 2
      ;;
    --redis_cli_bin)
      REDIS_CLI_BIN="$2"
      shift 2
      ;;
    --work_dir)
      WORK_DIR="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "unknown option: $1" >&2
      usage >&2
      exit 1
      ;;
  esac
done

resolve_archiver
resolve_redis_cli

if [ ! -x "$ARCHIVER" ]; then
  echo "archiver not found or not executable: $ARCHIVER" >&2
  exit 1
fi

mkdir -p "$WORK_DIR"

run_once() {
  TS=$(date +%Y%m%d_%H%M%S)
  OUT_SQL="$WORK_DIR/archive_${TS}.sql"
  ACK_SH="$WORK_DIR/archive_${TS}_ack.sh"
  LOG_FILE="$WORK_DIR/archive_${TS}.log"

  set +e
  "$ARCHIVER" \
    --redis_host "$REDIS_HOST" \
    --redis_port "$REDIS_PORT" \
    --out "$OUT_SQL" \
    --ack_out "$ACK_SH" \
    --mysql_cmd "$MYSQL_CMD" \
    --apply_ack "$APPLY_ACK" \
    --include_offline "$INCLUDE_OFFLINE" \
    --history_table "$HISTORY_TABLE" \
    --offline_table "$OFFLINE_TABLE" \
    --redis_cli_bin "$REDIS_CLI_BIN" >"$LOG_FILE" 2>&1
  RC=$?
  set -e

  if [ "$RC" -ne 0 ]; then
    echo "archive iteration failed rc=$RC log=$LOG_FILE" >&2
    cat "$LOG_FILE" >&2
    return "$RC"
  fi

  if ! grep -Eq 'history_messages=[1-9]|offline_messages=[1-9]' "$LOG_FILE"; then
    rm -f "$OUT_SQL" "$ACK_SH"
    echo "archive iteration produced no messages; cleaned empty artifacts"
    return 0
  fi

  echo "archive iteration succeeded sql=$OUT_SQL ack=$ACK_SH log=$LOG_FILE"
  return 0
}

while :; do
  run_once
  if [ "$ONCE" = "1" ]; then
    break
  fi
  sleep "$INTERVAL_SECS"
done
