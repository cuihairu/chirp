> 🌐 Language: [中文](./deployment.md) | **English** (this page)

# Deployment Guide

> Status note: the mature mainline is `game_sdk_gateway + auth + game_chat`, and the remaining services are Experimental. This page only covers what actually exists in the repo: service configuration is mainly command-line arguments (full examples in `docker-compose.yml`; the chat storage layer has additional `CHIRP_*` environment variable read points, see `services/shared/chat/src/message_store_config.cc`), and the Kubernetes manifests in `deploy/k8s/` are early templates whose env configuration names no longer match the current code, which this repo's CI does not verify either, so do not treat them as a production baseline; there are no Grafana dashboards. Each service's capability boundaries are defined by the [Capability Matrix](../CAPABILITY_MATRIX.en.md).

## Docker Compose deployment

`docker-compose.yml` brings up the infrastructure (Redis, MySQL) plus all services; `docker-compose.cluster.yml` is a single-machine cluster form with haproxy and adminer added:

```bash
docker compose up --build -d
docker compose logs -f
docker compose down
```

On a first deployment, focus on the four services redis, auth, gateway and chat, see [CORE.md](../CORE.en.md).

`docker-compose.cluster.yml` (single-machine cluster form, with haproxy), `deploy/haproxy.cfg`, `deploy/deploy-cluster.sh`, `deploy/k8s/*.yaml` and `scripts/build-cluster.sh` are all in the tree; the k8s manifests are early templates (see the status note at the top), and the correspondence between the haproxy configuration and the cluster compose script and the current service names/ports should be checked before use.

## Manual deployment

Build first:

```bash
./gen_proto.sh
cmake --preset dev
cmake --build --preset dev
```

Binaries land in the build directory following their source paths, e.g. `build/services/shared/chat/chirp_chat`. Every service is configured with command-line arguments; the following two are taken from `docker-compose.yml` (minus the dev-only `--allow_scaffold_login`):

```bash
# App plane auth
build/services/app/auth/chirp_app_auth \
  --port 6000 --jwt_secret <shared secret>

# Chat service (Redis + MySQL + full server-plane enhanced path)
build/services/shared/chat/chirp_chat --port 7000 --ws_port 7001 \
  --redis_host redis --redis_port 6379 \
  --mysql_host mysql --mysql_port 3306 --mysql_database chirp \
  --mysql_user chirp --mysql_password <password> \
  --server_gateway_host game_server_gateway --server_gateway_secret <secret> \
  --gateway_service_secret <secret> --npc_service_id npc_dialog
```

The other services (`chirp_game_sdk_gateway`, `chirp_game_server_gateway`, `chirp_app_sdk_gateway`, `chirp_app_notification`, etc.) start the same way; their arguments and ports are in each service's argument parsing at the top of `main.cc` and in `docker-compose.yml`.

Every service with a listener accepts `--host <addr>` (default `0.0.0.0`, all interfaces); the argument is passed straight into the TCP/WS listener constructors and actually controls the bind address (wired across all services as of 2026-10-19; before that, most services parsed the flag but never gave it to the listener). An invalid address fails at startup instead of silently falling back to all interfaces. To narrow the exposure surface: backend services that are only reached through gateways (`chirp_app_auth`, `chirp_chat`/`chirp_chat_distributed`, `chirp_social`, `chirp_party`, `chirp_voice`, `chirp_app_notification`, `chirp_search`) should bind `127.0.0.1` or an internal IP; the client-facing edges (`chirp_game_sdk_gateway`, `chirp_app_sdk_gateway`) and the server-plane-authenticated `chirp_game_server_gateway` stay on `0.0.0.0` or bind per network topology. A service's TCP and WS listeners (e.g. chat's `--port`/`--ws_port`) bind the same `--host` address.

**`--allow_scaffold_login` is a development scaffold, not a deployment argument**: with it on, auth treats a token that is neither a valid JWT nor an active session as "token equals user_id" (the LOGIN_REQ fallback branch in `services/app/auth/src/main_enhanced.cc`) — any caller with network reachability to the service can impersonate any user. Use it only in fully isolated local development demos; never pass it in production (`docker-compose.yml` carries it because that compose file is itself the dev-demo setup).

The rate-limit switches are on chat: `--login_rate_limit_per_min` (default 30/minute, per client IP) and `--send_rate_limit_per_min` (default 120/minute, per user). Window data lives in Redis, and Redis failures let traffic through (fail-open); with no `--redis_host` configured at all, rate limiting is not enabled.

## Production checklist

- Change MySQL and Redis away from the repo's example passwords (`chirp_pass` etc.) and replace `--jwt_secret` with a real secret
- Use a high-entropy random `--jwt_secret` (32+ bytes, e.g. `openssl rand -hex 32`); auth now refuses to start without one. When rotating, update every service sharing that secret in lockstep (auth and the edge gateways' `--token_secret` derive from the same source)
- Do not pass `--allow_scaffold_login` to auth (it is off by default; if you copy arguments from `docker-compose.yml`, drop it)
- Tune rate limits to your load, and note that the fail-open semantics mean rate limiting also stops working when Redis is down
- Narrow the bind surface of backend services with `--host`: services only reached through gateways should bind `127.0.0.1` or an internal IP, and edge gateways stay on `0.0.0.0` (see the binding notes above)
- TLS: `chirp_app_sdk_gateway` supports `--tls_port` / `--ws_tls_port` + `--tls_cert` / `--tls_key`; the other edge services are currently plaintext TCP/WS, so front them with a TLS proxy when exposing them externally
- Adjust log levels as needed; wiring critical errors to alerting is deployment-side operations work and is not built into the repo
- Bring Redis memory and MySQL connection counts into routine monitoring

## Monitoring

`chirp_chat`, `chirp_game_sdk_gateway`, `chirp_app_auth`, `chirp_app_notification`, `chirp_app_sdk_gateway`, `chirp_social`, `chirp_party`, `chirp_voice`, `chirp_game_server_gateway`, `chirp_npc_dialog`, `chirp_chat_distributed` and `chirp_search` support `--metrics_port <port>` to turn on `libs/common`'s `MetricsHttpServer` (Prometheus text format, `/metrics` endpoint); the default 0 means no listener, and a failed bind only logs a Warn without blocking the main service. Currently exported: chat has three (`chirp_chat_sessions` session-slot gauge / `chirp_chat_logins_total` / `chirp_chat_packets_total`), game gateway has three (`chirp_game_gateway_sessions/logins_total/packets_total`, with the same semantics as chat, decremented on both release paths, logout and disconnect), auth has two (`chirp_app_auth_logins_total` counting successful logins only / `chirp_app_auth_packets_total`), notification has five (`chirp_app_notification_packets_total` / `chirp_app_notification_devices_registered_total` / `chirp_app_notification_sent_total` / `chirp_app_notification_failed_total` / `chirp_app_notification_fcm_sent_total` + `chirp_app_notification_apns_sent_total` — the last two appear only when a send on the corresponding platform succeeds), app gateway has three (`chirp_app_gateway_sessions/logins_total/packets_total`, with the same semantics as chat), social/party have three each (`chirp_social_*`/`chirp_party_*` sessions/logins_total/packets_total, with the same semantics as chat), voice has three (`chirp_voice_sessions` bound-session gauge — one user per connection, repeated LOGIN and join-after-login are not double-counted, and disconnect/replacement always decrements / `chirp_voice_logins_total` / `chirp_voice_packets_total`), server gateway has two (`chirp_server_gateway_sessions` authenticated server-plane connection gauge (incremented only on successful auth, with disconnect/replacement/timeout all going through the same release funnel) / `chirp_server_gateway_packets_total`), npc_dialog has two (`chirp_npc_dialog_events_total` events received / `chirp_npc_dialog_replies_total` replies injected), distributed chat has three (`chirp_chat_distributed_sessions` bound-session gauge (repeated LOGIN is not double-counted, and logout/disconnect decrement through the same funnel) / `chirp_chat_distributed_logins_total` / `chirp_chat_distributed_packets_total`), and search has three (`chirp_search_packets_total` / `chirp_search_logins_total` / `chirp_search_queries_total` — search itself already listens on 5007, with 2248/2249 reaching it via both gateways' `--search_host` (default 5007) and a failed bridge degrading to `SERVER_UNAVAILABLE` without kicking the client); finer observability comes from logs and Redis/MySQL's own facilities. The basic fallback entry points are now complete: without a MySQL build `chirp_chat` falls back to `main.cc` and `chirp_app_auth` falls back to `main.cc`, both with the same `--metrics_port` + same-named metrics (an either-or build of the same target, never running simultaneously), and chat basic's replacement-safety semantics match enhanced (a single Bind point + a single `HandleDisconnect` funnel, with logout and disconnect decrementing through the same funnel). `docker-compose.yml`'s development stack enables metrics by default for all ten services and publishes them to the host on 19101~19110 (chat 19101/game gateway 19102/auth 19103/social 19104/party 19105/voice 19106/notification 19107/app gateway 19108/server gateway 19109/npc_dialog 19110; the cluster production stack is untouched).

## Load balancing

A layer-4 load balancer is enough for persistent TCP services; an HAProxy example:

```
frontend chirp_gateway
    bind *:5000
    mode tcp
    default_backend gateway_servers

backend gateway_servers
    mode tcp
    balance roundrobin
    server gateway1 10.0.1.10:5000 check
    server gateway2 10.0.1.11:5000 check
```

Multiple gateway instances need Redis configured (`--redis_host`), otherwise cross-instance kicks and session sharing do not take effect.

## Capacity reference

The data comes from the [gateway capacity benchmark](../design-notes/capacity_benchmark.en.md) of 2026-09-27, measured on a single machine:

- Concurrent online connections: 12000/12000 logged in and online, 0 handshake timeouts (a pure-capacity tier spreading 200 conn/s over 60 seconds)
- Connection establishment rate: 200 conn/s is clean with no concurrent traffic; when connection establishment overlaps with message sending, even 50 conn/s breaks through (the steady500 tier has 165/500 pipe handshake timeouts)
- Steady-state private-chat throughput: constrained by server-side pacing (1 second per user for private chat, hard-coded), capped at roughly connections/1.1 messages per second

social, party and voice have no comparable measurements and no numbers are given. When planning a new deployment, estimate from the bottleneck items above (connection establishment overlapping with traffic, the pacing cap) and verify with your own load test.

## Backup

```bash
# Daily MySQL backup and restore
mysqldump -u chirp -p chirp > backup_$(date +%Y%m%d).sql
mysql -u chirp -p chirp < backup_20240318.sql

# Redis snapshot
redis-cli BGSAVE
cp /var/lib/redis/dump.rdb backup/
```

Use the `upgrade_db_*.sql` files under `scripts/` for schema upgrades (e.g. `upgrade_db_messages.sql`). These files are not idempotent — MySQL's `ALTER TABLE ... ADD COLUMN` has no `IF NOT EXISTS`, so running them twice reports that the column already exists; check the current table structure first.

## Rollback

Without a versioned-image release process, a rollback means going back to a previous commit and redeploying:

```bash
docker compose down
git checkout <last good commit>
docker compose up --build -d
docker compose logs --tail=50
```

Database columns are additive (see the upgrade scripts), and old binaries tolerate extra columns on the read side (guarded by `row.size()`), so rolling back the binaries first and deciding later whether to roll back the data is viable; data rollback can only go through backup restore.

## Troubleshooting

- High CPU: check connection counts and Redis memory first, then sample the specific process with perf/FlameGraph
- High memory: check Redis `maxmemory` and the MySQL buffer pool, then the process heap
- Connections dropping: check the load balancer's health checks and idle timeouts, and read the service logs; note that chat's private-chat pacing (1 second per user) is deliberate behavior, not a fault
- Rate limiting hitting the wrong traffic: confirm the Redis state (if fail-open is not in effect, Redis is reachable, so the rejections come from real rate limiting) and the `--login_rate_limit_per_min` / `--send_rate_limit_per_min` thresholds

## Related pages

- [Overall Architecture](../architecture.en.md)
- [Scalability notes](../design-notes/SCALABILITY.en.md)
- [API Overview](../api/overview.en.md)
