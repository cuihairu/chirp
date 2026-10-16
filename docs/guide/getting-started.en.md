> 🌐 Language: [中文](./getting-started.md) | **English** (this page)

# Getting Started

This guide focuses on the currently supported backend path: `gateway + auth + chat`.

## Prerequisites

Required:

- CMake 3.21 or newer
- A C++23 compiler
- The Protocol Buffers compiler and C++ runtime

Optional:

- Docker and Docker Compose, for Redis/MySQL and multi-service startup
- MySQL client development libraries, for the enhanced auth/chat builds
- libsodium, for the enhanced auth build

## Local build

From the repo root:

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

On macOS with vcpkg you also need to pass the host architecture:

```bash
cmake --preset dev \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DCMAKE_OSX_ARCHITECTURES="$(uname -m)"
```

## Smoke tests

```bash
./test_services.sh --smoke       # auth + gateway + TCP/WS login client
./test_services.sh --smoke-chat  # chat service + chat client
./test_services.sh --smoke-sdk   # game client SDK (sdks/core) + chat: login / online delivery / offline queue
./test_services.sh --smoke-npc   # NPC dialog loop on the server plane
./test_services.sh --smoke-redis # Redis-backed distributed session/kick path
```

Integration smoke tests:

```bash
bash tests/run_integration_tests.sh
bash tests/run_integration_tests.sh --local-services --gateway-port 5500 --auth-port 6500
```

## Starting with Docker Compose

```bash
docker compose up --build
```

Compose starts Redis, MySQL, Auth, Gateway, Chat and the experimental services. On the first verification pass, only focus on:

- `redis`: cache and session storage
- `auth`: the authentication service
- `gateway`: the gateway service
- `chat`: the chat service

Common default ports:

| Service | TCP | WebSocket | Notes |
| --- | --- | --- | --- |
| Gateway | 5000 | 5001 | login, logout, heartbeat, session |
| Auth | 6000 | - | authentication verification |
| Chat | 7000 | 7001 | direct chat entry point |

## Starting manually

```bash
./build/services/app/auth/chirp_app_auth --port 6000 --jwt_secret dev_secret
./build/services/game/sdk_gateway/chirp_game_sdk_gateway --port 5000 --ws_port 5001 --auth_host 127.0.0.1 --auth_port 6000
./build/services/shared/chat/chirp_chat --port 7000 --ws_port 7001 --redis_host 127.0.0.1 --redis_port 6379 --offline_ttl 604800
```

The most basic local chat test can run without Redis configured, but it is recommended when verifying the offline queue, history lists or distributed session behavior.

## Protocol reminder

Both TCP and WebSocket carry:

```text
[uint32_be payload_size][chirp.gateway.Packet protobuf bytes]
```

`Packet.msg_id` lives inside the protobuf envelope; it is not a separate 2-byte network frame header.

## Current limitations

- Gateway login does not automatically authenticate a separate Chat connection.
- Chat messages currently go directly to the Chat service and do not pass through the Gateway.
- Social, voice, notification, search, the mobile apps, the admin dashboard and the various SDK wrappers are not all production-ready.

For the precise state see [Overall Architecture](../architecture.en.md) and [Capability Matrix](../CAPABILITY_MATRIX.en.md).
