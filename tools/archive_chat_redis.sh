#!/bin/sh

set -eu

REDIS_HOST=127.0.0.1
REDIS_PORT=6379
OUT_SQL=/tmp/chirp_chat_archive.sql
ACK_SH=/tmp/chirp_chat_archive_ack.sh
MYSQL_CMD=
APPLY_ACK=0
INCLUDE_OFFLINE=1
HISTORY_TABLE=chat_messages
OFFLINE_TABLE=chat_offline_messages
EXPORTER=
REDIS_CLI_BIN=

resolve_exporter() {
  if [ -n "${EXPORTER}" ]; then
    return 0
  fi
  if [ -x ./build/tools/benchmark/chirp_chat_mysql_exporter ]; then
    EXPORTER=./build/tools/benchmark/chirp_chat_mysql_exporter
  else
    EXPORTER=/usr/local/bin/chirp_chat_mysql_exporter
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
  --redis_host HOST         Redis host, default 127.0.0.1
  --redis_port PORT         Redis port, default 6379
  --out PATH                Output SQL path, default /tmp/chirp_chat_archive.sql
  --ack_out PATH            Output ack script path, default /tmp/chirp_chat_archive_ack.sh
  --mysql_cmd CMD           Shell command used to apply SQL, e.g. "mysql -h 127.0.0.1 -u root -p app_db"
  --apply_ack 0|1           Delete archived Redis keys after successful mysql apply, default 0
  --include_offline 0|1     Export chat:offline:* in addition to history, default 1
  --history_table NAME      MySQL history table name, default chat_messages
  --offline_table NAME      MySQL offline table name, default chat_offline_messages
  --exporter PATH           Exporter binary path
  --redis_cli_bin PATH      redis-cli binary path
EOF
}

while [ $# -gt 0 ]; do
  case "$1" in
    --redis_host)
      REDIS_HOST="$2"
      shift 2
      ;;
    --redis_port)
      REDIS_PORT="$2"
      shift 2
      ;;
    --out)
      OUT_SQL="$2"
      shift 2
      ;;
    --ack_out)
      ACK_SH="$2"
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
    --exporter)
      EXPORTER="$2"
      shift 2
      ;;
    --redis_cli_bin)
      REDIS_CLI_BIN="$2"
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

resolve_exporter
resolve_redis_cli

if [ ! -x "$EXPORTER" ]; then
  echo "exporter not found or not executable: $EXPORTER" >&2
  exit 1
fi

"$EXPORTER" \
  --redis_host "$REDIS_HOST" \
  --redis_port "$REDIS_PORT" \
  --out "$OUT_SQL" \
  --ack_out "$ACK_SH" \
  --redis_cli_bin "$REDIS_CLI_BIN" \
  --include_offline "$INCLUDE_OFFLINE" \
  --history_table "$HISTORY_TABLE" \
  --offline_table "$OFFLINE_TABLE"

echo "sql exported to $OUT_SQL"
echo "ack script written to $ACK_SH"

if [ -z "$MYSQL_CMD" ]; then
  echo "mysql_cmd not provided; skipping SQL apply and Redis ack"
  exit 0
fi

sh -c "$MYSQL_CMD" < "$OUT_SQL"
echo "mysql apply succeeded"

if [ "$APPLY_ACK" = "1" ]; then
  sh "$ACK_SH"
  echo "redis ack applied"
else
  echo "apply_ack=0; leaving Redis data intact"
fi
