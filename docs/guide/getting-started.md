---
title: Getting Started
---

# Getting Started

This guide focuses on the current supported backend path: `gateway + auth + chat`.

## Prerequisites

Required:

- CMake 3.21 or newer
- C++23 compiler
- Protocol Buffers compiler and C++ runtime

Optional:

- Docker and Docker Compose for Redis/MySQL and multi-service startup
- MySQL client development library for enhanced auth/chat builds
- libsodium for enhanced auth builds

## Build Locally

From the repository root:

```bash
./gen_proto.sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

If you only want the minimal build path:

```bash
cmake --preset minimal
cmake --build --preset minimal
```

If you use vcpkg on macOS, also pass the host architecture:

```bash
cmake --preset dev \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DCMAKE_OSX_ARCHITECTURES="$(uname -m)"
```

## Smoke Tests

```bash
./test_services.sh --smoke       # auth + gateway + TCP/WS login clients
./test_services.sh --smoke-chat  # chat service + chat clients
./test_services.sh --smoke-redis # Redis-backed distributed session/kick path
```

Integration smoke tests:

```bash
bash tests/run_integration_tests.sh
bash tests/run_integration_tests.sh --local-services --gateway-port 5500 --auth-port 6500
```

## Docker Compose

```bash
docker compose up --build
```

Compose starts Redis, MySQL, Auth, Gateway, Chat, and experimental services. For first validation, focus on:

- `redis`
- `auth`
- `gateway`
- `chat`

Default useful ports:

| Service | TCP | WebSocket | Notes |
| --- | --- | --- | --- |
| Gateway | 5000 | 5001 | login, logout, heartbeat, session |
| Auth | 6000 | - | auth validation |
| Chat | 7000 | 7001 | direct chat entry |

## Manual Startup

```bash
./build/services/auth/chirp_auth --port 6000 --jwt_secret dev_secret
./build/services/gateway/chirp_gateway --port 5000 --ws_port 5001 --auth_host 127.0.0.1 --auth_port 6000
./build/services/chat/chirp_chat --port 7000 --ws_port 7001 --redis_host 127.0.0.1 --redis_port 6379 --offline_ttl 604800
```

Redis is optional for the most basic local chat test, but it is recommended when validating offline queues, history lists, or distributed session behavior.

## Protocol Reminder

Both TCP and WebSocket carry:

```text
[uint32_be payload_size][chirp.gateway.Packet protobuf bytes]
```

`Packet.msg_id` is inside the protobuf envelope. It is not a separate 2-byte network header.

## Current Limitations

- Gateway login does not automatically authenticate a separate Chat connection.
- Chat messages currently go to the Chat service directly, not through Gateway.
- Social, voice, notification, search, mobile app, admin dashboard, and SDK wrappers are not all production-ready.

See [Overall Architecture](../architecture.md) and [Capability Matrix](../CAPABILITY_MATRIX.md) for the exact status.
