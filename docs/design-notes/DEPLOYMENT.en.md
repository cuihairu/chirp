> 🌐 Language: [中文](./DEPLOYMENT.md) | **English** (this page)

# Deployment notes (historical)

> This is an early deployment note and still mixes in outdated template content. The current deployment guide is [guide/deployment.md](../guide/deployment.en.md) (Docker Compose, real binary paths, real command-line flags, measured capacity data); use that page when you need to draw deployment conclusions.

## Service dependencies

```
┌─────────────┐     ┌─────────────┐
│   Gateway   │────▶│     Auth     │
│  (5000/5001)│     │    (6000)    │
└──────┬──────┘     └─────────────┘
       │
       ├──────────┬──────────┬──────────┐
       ▼          ▼          ▼          ▼
   ┌────────┐ ┌──────┐ ┌────────┐ ┌───────┐
   │ Chat   │ │Social│ │  Voice │ │ Redis │
   │ 7000   │ │ 8000 │ │  9000  │ │ 6379  │
   └────────┘ └──────┘ └────────┘ └───────┘
       │          │          │          ▲
       └──────────┴──────────┴──────────┤
                                          ▼
                                    ┌─────────┐
                                    │  MySQL  │
                                    │  3306   │
                                    └─────────┘
```

## Port overview

| Service | Port | Protocol |
|---------|------|----------|
| Gateway (game_sdk_gateway) | 5000, 5001 | TCP, WS |
| Auth (app_auth) | 6000 | TCP |
| Chat (chirp_chat) | 7000, 7001 | TCP, WS |
| Social | 8000, 8001 | TCP, WS |
| Party | 7500, 7501 | TCP, WS |
| Voice | 9000, 9001 | TCP, WS |
| Server Gateway | 8100 | TCP |
| App Notification | 5006, 5016 | TCP, WS |
| App SDK Gateway | 5200, 5201 | TCP, WS |
| Redis | 6379 | TCP |
| MySQL | 3306 | TCP |

## Common commands

```bash
# bring the whole stack up (docker compose)
docker compose up -d
docker compose logs -f
docker compose down

# build from source and verify locally
./gen_proto.sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

Service configuration is authoritative in the command-line flags; samples are in `docker-compose.yml` and [guide/deployment.md](../guide/deployment.md). The chat storage layer additionally reads `CHIRP_*` environment variables (`message_store_config.cc`). Logs are plain text lines (`YYYY-MM-DD HH:MM:SS [LEVEL] [tid=...] message`, `libs/common/logger.*`), with no JSON structured output.

## Capacity

For the capacity numbers, use the measurements in [capacity_benchmark.md](./capacity_benchmark.md): 12000 concurrent logged-in online connections on a single machine, 200 conn/s for clean establishment (with concurrent traffic in flight even 50 conn/s gets pushed through), and steady-state private-chat throughput constrained by the 1 second/user pacing. This page no longer maintains an estimate table.

## Backup and network security

```bash
# MySQL backup and restore
mysqldump -u chirp -p chirp > backup_$(date +%Y%m%d).sql
mysql -u chirp -p chirp < backup_20240301.sql

# Redis snapshot
redis-cli BGSAVE
```

Only the client edge ports are exposed externally; internal ports (6xxx, 7000, 8100 and so on) are blocked from the internet by firewall rules:

```bash
ufw allow 5000/tcp
ufw allow 5001/tcp
ufw deny 7000/tcp
```

Redis high availability (Sentinel) and MySQL master-slave replication are deployment-side operations choices, and the repository ships no configuration for them.
