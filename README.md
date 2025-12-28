# chirp

`chirp` 是一个面向游戏开发的轻量聊天后端骨架：支持游戏内聊天（TCP）、聊天 App（WebSocket）、以及共享账号/会话能力（Auth + 可选 Redis 分布式会话）。

目标是让你“几分钟跑起来、几天接入上线”，后续再逐步演进到更完整的社交/语音体系。

## 适用场景

- **游戏内聊天**：游戏客户端（C++/Unity/Unreal 等）通过 TCP 连接，低延迟、易控。
- **聊天 App/移动端**：App 通过 WebSocket 接入（适合浏览器/移动端网络环境）。
- **多端登录/挤下线**：同一账号多设备登录，支持 kick（最后登录优先）。
- **多实例 Gateway**：可选 Redis 实现分布式“session owner”并通过 Pub/Sub 触发跨实例踢人。

## 协议（对接要点）

传输层分两种：
- **TCP**：4 字节大端长度前缀 + Protobuf payload。
- **WebSocket**：WebSocket binary frame 内同样是“长度前缀 + Protobuf payload”。

业务层统一用 `proto/gateway.proto` 里的 `chirp.gateway.Packet` 做 envelope：
- `msg_id`：消息类型（Login/Heartbeat/Chat 等）
- `sequence`：请求序号（用于匹配响应）
- `body`：具体业务消息（protobuf bytes）

对应的业务 proto：
- `proto/auth.proto`：登录/登出响应、Kick 通知等
- `proto/chat.proto`：1v1 消息、历史拉取等

## 快速开始（本地）

依赖：CMake >= 3.15、C++17、Protobuf（`protoc` + C++ runtime）。Go **可选**（用于生成 `proto/go`）。

```bash
./gen_proto.sh
cmake -S . -B build
cmake --build build -j
```

### 一键 smoke test

```bash
./test_services.sh --smoke       # auth + gateway + tcp/ws 客户端
./test_services.sh --smoke-chat  # chat + chat clients
./test_services.sh --smoke-redis # 需要 Docker：Redis 分布式 session + 跨实例 kick（tcp + ws）
```

### 手动启动（默认端口）

```bash
./build/services/auth/chirp_auth --port 6000 --jwt_secret dev_secret
./build/services/gateway/chirp_gateway --port 5000 --ws_port 5001 --auth_host 127.0.0.1 --auth_port 6000
./build/services/chat/chirp_chat --port 7000
```

可选：启用 Redis 分布式 session（多实例 gateway 时有用）：

```bash
./build/services/gateway/chirp_gateway --port 5000 --ws_port 5001 --redis_host 127.0.0.1 --redis_port 6379
```

## 游戏快速接入（推荐路径）

### 方案 A：直接使用 Core SDK（C++）

`sdks/core` 提供了一个最小可用的 C++ 客户端（TCP + 长度前缀 Protobuf framing），适合在游戏客户端里快速打通登录、收消息、发消息的链路。

> 目前示例客户端默认直连 `services/chat`（端口 7000）来演示收发与历史；`services/gateway` 主要提供边缘接入与会话能力，后续可扩展为统一路由入口。

示例程序：

```bash
./build/sdks/core/sdk_example
```

### 方案 B：自己实现客户端（Unity/Unreal/自研引擎）

你只需要实现两件事：
1) **连接**：TCP 或 WebSocket（二进制帧）
2) **拆包/粘包处理**：4 字节大端长度 + payload（payload 是 `chirp.gateway.Packet`）

典型流程：
- `LOGIN_REQ` -> `LOGIN_RESP`
- 定时 `HEARTBEAT_PING` -> `HEARTBEAT_PONG`
- 收到 `KICK_NOTIFY`：提示并断开/重登
- 聊天：`SEND_MESSAGE_REQ` / `CHAT_MESSAGE_NOTIFY` / `GET_HISTORY_REQ`

> 当前仓库中，`services/chat` 支持 1v1 消息路由与内存历史（示例实现）。生产环境建议接入 Redis/MySQL 做离线与持久化。

## 聊天 App 接入（WebSocket）

`services/gateway` 同时提供 WebSocket（`--ws_port`）。WebSocket 客户端发送 binary frame，frame payload 仍然是长度前缀 + `chirp.gateway.Packet`。

> 当前 gateway 的 WebSocket 主要用于 Login/Heartbeat/Session（控制面）；聊天收发能力目前在 `services/chat`（TCP）里演示实现。要做“聊天 App 全 WebSocket”，可以在 chat service 增加 WS 支持或由 gateway 转发到 chat。

可以用仓库内工具快速验证：

```bash
./build/tools/benchmark/chirp_ws_login_client --host 127.0.0.1 --port 5001 --token user_1 --device dev --platform web
./build/tools/benchmark/chirp_ws_ping_client  --host 127.0.0.1 --port 5001
```

## 工程结构

- `proto/`：协议定义（生成代码在 `proto/cpp`、`proto/go`）
- `libs/common`：基础工具（logger、JWT/HS256、base64、sha256）
- `libs/network`：ASIO TCP/WS server/session + framing + Redis RESP（用于分布式 session）
- `services/gateway`：边缘入口（TCP + WebSocket），可选 Auth RPC、可选 Redis 分布式 session
- `services/auth`：认证服务（JWT HS256 校验示例）
- `services/chat`：聊天服务（1v1 + history 示例实现）
- `sdks/core`：C++ Core SDK（快速集成）
- `tools/benchmark`：本地验证工具（login/ping/ws/chat）
- `tests`：单元测试（gtest）

## Docs

更多说明见 `docs/README.md`。
