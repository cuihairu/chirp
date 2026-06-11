# Chirp Quick Start

Chirp is currently a lightweight realtime communication backend skeleton. Start with the supported `gateway + auth + chat` path, then explore experimental services after the core path works.

## 1. Generate Protobuf

```bash
./gen_proto.sh
```

## 2. Build

Recommended development preset:

```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

Minimal build:

```bash
cmake --preset minimal
cmake --build --preset minimal
```

## 3. Run Smoke Tests

```bash
./test_services.sh --smoke
./test_services.sh --smoke-chat
./test_services.sh --smoke-redis
```

Integration script:

```bash
bash tests/run_integration_tests.sh
bash tests/run_integration_tests.sh --local-services --gateway-port 5500 --auth-port 6500
```

## 4. Docker Compose

```bash
docker compose up --build
```

Compose starts more than the minimal core path. Validate these first:

| Service | Port | Purpose |
| --- | --- | --- |
| Redis | 6379 | optional session/history/offline coordination |
| Auth | 6000 | login validation |
| Gateway | 5000 TCP / 5001 WS | login, logout, heartbeat, session |
| Chat | 7000 TCP / 7001 WS | direct chat messaging |

Other services in Compose are experimental unless the [Capability Matrix](./CAPABILITY_MATRIX.md) says otherwise.

## 5. Manual Startup

```bash
./build/services/auth/chirp_auth --port 6000 --jwt_secret dev_secret
./build/services/gateway/chirp_gateway --port 5000 --ws_port 5001 --auth_host 127.0.0.1 --auth_port 6000
./build/services/chat/chirp_chat --port 7000 --ws_port 7001 --redis_host 127.0.0.1 --redis_port 6379 --offline_ttl 604800
```

## Protocol

TCP and WebSocket both use:

```text
[uint32_be payload_size][chirp.gateway.Packet protobuf bytes]
```

`Packet.msg_id` is inside the protobuf envelope. It is not a separate network-frame field.

## Architecture Note

Gateway is not yet a universal business router. Current chat tests and SDK examples connect to Chat directly. Read [Overall Architecture](./architecture.md) for the current service boundaries and target direction.
