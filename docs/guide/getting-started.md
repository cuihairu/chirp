---
title: 快速上手
---

# 快速上手

本指南聚焦当前受支持的后端路径:`gateway + auth + chat`。

## 前置条件

必需:

- CMake 3.21 或更新
- C++23 编译器
- Protocol Buffers 编译器与 C++ 运行时

可选:

- Docker 与 Docker Compose,用于 Redis/MySQL 和多服务启动
- MySQL 客户端开发库,用于增强版 auth/chat 构建
- libsodium,用于增强版 auth 构建

## 本地构建

在仓库根目录:

```bash
./gen_proto.sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

只想要最小构建路径的话:

```bash
cmake --preset minimal
cmake --build --preset minimal
```

macOS 上用 vcpkg 的话,还要传宿主架构:

```bash
cmake --preset dev \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DCMAKE_OSX_ARCHITECTURES="$(uname -m)"
```

## 冒烟测试

```bash
./test_services.sh --smoke       # auth + gateway + TCP/WS 登录客户端
./test_services.sh --smoke-chat  # chat 服务 + 聊天客户端
./test_services.sh --smoke-sdk   # 游戏客户端 SDK(sdks/core)+ chat:登录 / 在线投递 / 离线队列
./test_services.sh --smoke-npc   # server plane 上的 NPC 对话闭环
./test_services.sh --smoke-redis # Redis 支撑的分布式会话/踢线路径
```

集成冒烟测试:

```bash
bash tests/run_integration_tests.sh
bash tests/run_integration_tests.sh --local-services --gateway-port 5500 --auth-port 6500
```

## Docker Compose

```bash
docker compose up --build
```

Compose 会启动 Redis、MySQL、Auth、Gateway、Chat 和实验性服务。第一次验证时,只关注:

- `redis`
- `auth`
- `gateway`
- `chat`

常用默认端口:

| 服务 | TCP | WebSocket | 说明 |
| --- | --- | --- | --- |
| Gateway | 5000 | 5001 | 登录、登出、心跳、会话 |
| Auth | 6000 | - | 认证校验 |
| Chat | 7000 | 7001 | 直连聊天入口 |

## 手动启动

```bash
./build/services/auth/chirp_auth --port 6000 --jwt_secret dev_secret
./build/services/game/sdk_gateway/chirp_game_sdk_gateway --port 5000 --ws_port 5001 --auth_host 127.0.0.1 --auth_port 6000
./build/services/chat/chirp_chat --port 7000 --ws_port 7001 --redis_host 127.0.0.1 --redis_port 6379 --offline_ttl 604800
```

最基本的本地聊天测试可以不配 Redis,但验证离线队列、历史列表或分布式会话行为时建议配上。

## 协议提醒

TCP 和 WebSocket 承载的都是:

```text
[uint32_be payload_size][chirp.gateway.Packet protobuf bytes]
```

`Packet.msg_id` 在 protobuf 信封里面,不是独立的 2 字节网络帧头。

## 当前限制

- Gateway 登录不会自动认证一条独立的 Chat 连接。
- 聊天消息目前直连 Chat 服务,不经过 Gateway。
- Social、voice、notification、search、移动应用、管理后台和各 SDK 包装层并非都达到生产可用。

精确状态见 [Overall Architecture](../architecture.md) 与 [Capability Matrix](../CAPABILITY_MATRIX.md)。
