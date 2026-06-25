# chirp

`chirp` 是一个面向游戏开发的实时通信后端骨架。当前最成熟、最应该优先验证的主线是：

```text
gateway + auth + chat
```

仓库里还包含 `social`、`voice`、`notification`、`search`、多端 SDK、移动端和管理后台等扩展代码，但这些模块完成度不一致。阅读或对外介绍时，请把它定位为“可运行的核心通信骨架 + 实验性扩展”，不要把所有目录都描述成稳定产品能力。

## 先读什么

- [核心说明](docs/CORE.md)：当前可用链路、服务边界、协议和本地验证命令
- [能力矩阵](docs/CAPABILITY_MATRIX.md)：各服务、SDK、应用的真实完成度
- [快速开始](docs/guide/getting-started.md)：构建、Docker Compose、smoke test
- [API 概述](docs/api/overview.md)：当前 Packet 协议、消息 ID 和核心流程
- [整体架构](docs/architecture.md)：服务拓扑、数据流和架构判断

## 核心服务

| 服务 | 默认端口 | 当前状态 | 作用 |
| --- | --- | --- | --- |
| Gateway | TCP 5000 / WS 5001 | Supported | 登录、登出、心跳、会话绑定、可选 Redis 跨实例 kick |
| Auth | TCP 6000 | Supported | 基础 token flow；依赖满足时可构建增强认证实现 |
| Chat | TCP 7000 / WS 7001 | Supported | 私聊、历史、离线队列；可选 Redis/MySQL 增强路径 |

当前核心使用方式：

- 客户端连接 `gateway` 做登录、心跳和会话验证。
- 聊天消息当前直连 `chat`，不是经由 `gateway` 统一转发。
- Redis 是可选增强，用于 Gateway 分布式 session、跨实例 kick、Chat 历史和离线队列。

## 快速开始

依赖：CMake 3.21+、C++23、Protocol Buffers。Docker、MySQL、libsodium 为可选增强依赖。

```bash
./gen_proto.sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

最小构建：

```bash
cmake --preset minimal
cmake --build --preset minimal
```

smoke test：

```bash
./test_services.sh --smoke       # auth + gateway + TCP/WS login
./test_services.sh --smoke-chat  # chat + chat clients
./test_services.sh --smoke-redis # Redis session/kick path
```

如果你在 macOS 上通过 vcpkg 配置，记得显式传架构，否则 vcpkg 可能回退到系统依赖：

```bash
cmake --preset dev \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DCMAKE_OSX_ARCHITECTURES="$(uname -m)"
```

Docker Compose：

```bash
docker compose up --build
```

## 协议要点

TCP 和 WebSocket 使用同一套二进制 payload：

```text
[uint32_be payload_size][chirp.gateway.Packet protobuf bytes]
```

业务 envelope 定义在 `proto/gateway.proto`：

- `msg_id`：消息类型，例如 `LOGIN_REQ`、`HEARTBEAT_PING`、`SEND_MESSAGE_REQ`
- `sequence`：请求序号，用于匹配响应
- `body`：具体业务 protobuf bytes，例如 `chirp.auth.LoginRequest`

## 当前边界

- `gateway` 还不是通用业务路由层；聊天包请发到 `chat`。
- `gateway` 登录不会自动授权一个独立的 `chat` 连接。
- `social`、`voice`、`notification`、`search`、SDK、移动端、管理后台都不应默认视为生产稳定能力。
- NPC 对话系统目前主要是设计文档，不能当作已落地后端能力。

## 工程结构

- `proto/`：协议定义
- `libs/common`：日志、JWT/HS256、base64、sha256 等基础工具
- `libs/network`：ASIO TCP/WS server/session、framing、Redis RESP
- `services/gateway`：边缘入口和会话能力
- `services/auth`：认证服务
- `services/chat`：私聊、历史和离线队列
- `sdks/core`：C++ 客户端集成实验
- `tools/benchmark`：本地验证工具
- `tests`：单元和集成 smoke 测试
