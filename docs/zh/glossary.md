# 术语表

> 🌐 Language: [English](../glossary.md) | **中文**（本页）

本术语表按职责抽象落位，不按目录名解释。每个锚点指向定义该概念的源码声明。层间关系见[整体架构](architecture.md)。

| 术语 | English | 定义 | file:line |
| --- | --- | --- | --- |
| 协议信封 | Packet | 所有 TCP 与 WebSocket 载荷共用一个 protobuf 信封，携带 `msg_id`、`sequence`、body 与 `request_id`。 | proto/gateway.proto:358 |
| 消息编号 | MsgID | 共享消息编号注册表，划分认证、聊天、社交、语音、可信服务、通知与组队消息。 | proto/gateway.proto:11 |
| 帧编解码 | Frame codec | 在流或 WebSocket 二进制消息上，把 protobuf 包转换为带大端 `uint32` 长度前缀的帧，或反向解析。 | libs/network/protobuf_framing.h:12 |
| 连接会话 | Session | 供 TCP 与 WebSocket 会话共享的传输中立契约：发送、关闭、半关闭检测和对端地址。 | libs/network/session.h:7 |
| TCP 服务端 | TcpServer | 接受带帧 TCP 会话，并给服务暴露 Start/Stop 生命周期回调。 | libs/network/tcp_server.h:14 |
| WebSocket 服务端 | WebSocketServer | 接受二进制 WebSocket 会话，回调形状与 `TcpServer` 相同。 | libs/network/websocket_server.h:13 |
| 进程配置 | Config | 可选的键值文件读取器；服务部署通常改用带类型的命令行旗标。 | libs/common/config.h:10 |
| 必填参数 | RequireArg | 缺少安全敏感旗标（例如 JWT 签名密钥）时拒绝启动进程。 | libs/common/required_arg.h:28 |
| 运行指标 | Metrics counters | 进程内计数器和 gauge，经可选 Prometheus 文本 HTTP 端点暴露。 | libs/common/include/common/metrics.h:179 |
| 指标端点 | MetricsHttpServer | 小型 ASIO HTTP 监听器，服务运行指标；绑定失败时服务继续但无指标。 | libs/common/include/common/metrics_http_server.h:17 |
| 崩溃采集 | Crash handler | 在旗标解析和监听器创建之前初始化进程外崩溃采集。 | libs/common/crash_handler.h:33 |
| 会话注册表 | SessionRegistry | 进程内 `(user, platform)` 绑定表；同槽重登会踢旧会话，不同平台共存。 | libs/network/session_registry.h:29 |
| Redis 会话认领 | Redis session claim | 跨实例会话认领与踢出机制，使用 `chirp:sess:*` 键和按实例的踢出频道。 | libs/network/redis_session_manager.h:26 |
| 消息路由 | MessageRouter | Redis pub/sub 封装，覆盖用户/群/社交/踢出频道；无 Redis 时退化为本地模式。 | libs/network/message_router.h:17 |
| 服务桥 | ServiceBridge | 每个客户端会话一条已认证内部连接，把边缘包转发到 chat 或 search，并区分踢出/降级语义。 | libs/network/service_bridge.h:54 |
| 聊天对等枢纽 | ChatPeerHub | game_chat 到 app_chat 连接的 hub 侧，执行白名单/版本协商并跨平面扇出。 | libs/network/chat_peer_hub.h:38 |
| 服务网关对端 | ServerGatewayPeer | 可信拨出客户端契约：服务认证、心跳、注入、事件发布与事件确认。 | libs/network/server_gateway_peer.h:30 |
| 服务注册表 | ServiceRegistry | 把已连接 `service_id` 映射到唯一服务平面 peer；新连接替换旧连接时返回被替换 peer。 | services/game/server_gateway/src/service_registry.h:28 |
| 流代理 | StreamBrokerConsumer | 读取 Redis Stream 注入条目，认证生产者，并把 at-least-once 条目转成服务网关 RPC。 | services/game/server_gateway/src/stream_broker.h:75 |
| 接入网关 | Access gateway | 面向客户端的 game/app 边缘：完成认证或 scaffold 登录、认领会话槽位并转发业务包。 | services/game/sdk_gateway/src/main.cc:375 |
| 认证服务 | AuthService | 基于 MySQL 与 Redis 的密码登录、注册、令牌签发、refresh token 与会话上限策略。 | services/app/auth/src/auth_service.h:29 |
| 推送服务 | NotificationService | 存储推送目标，并把与提供商无关的推送请求分发到 logging、HTTP、APNs 或 FCM 行为。 | services/app/notification/src/notification_service.h:89 |
| 设备存储 | DeviceStore | 可选的按行、原子写入的推送设备文件快照。 | services/app/notification/src/device_store.h:36 |
| 聊天核心 | Chat core | 同一二进制承载游戏聊天、App 聊天、跨平面桥、投递、群组和治理的业务核心。 | services/shared/chat/src/main_enhanced.cc:1152 |
| 混合消息存储 | HybridMessageStore | Redis 热历史/离线队列加 MySQL 档案，带 Redis 故障回退队列和异步冷层重试。 | services/shared/chat/src/hybrid_message_store.h:66 |
| 投递跟踪 | MessageDeliveryTracker | 在混合存储之上管理 pending 投递、重试、确认与统计。 | services/shared/chat/src/message_delivery_tracker.h:18 |
| 玩家目录 | PlayerDirectory | App 平面身份绑定、订阅、未读台账、游戏在线状态和有上限的私有副本扇出。 | services/shared/chat/src/player_directory.h:52 |
| 搜索索引 | MessageSearchIndex | 以权威 MySQL 消息构建的 SQLite FTS5 投影，含 id 映射、删除和游标搜索。 | services/search/src/message_search_index.h:25 |
| 社交状态 | SocialState | 进程内好友、请求、黑名单、通知偏好、在线状态和游戏在线 overlay；Redis 快照可选。 | services/social/src/main.cc:86 |
| 组队状态 | PartyState | 进程内队伍、邀请、已接受邀请标记和会话绑定；队伍可选持久化到 Redis，邀请保持进程内。 | services/party/src/main.cc:112 |
| 语音状态 | VoiceState | 进程内房间、用户-房间映射、会话绑定、登录门、TURN 凭据和心跳超时。 | services/voice/src/main.cc:228 |
| Go 服务客户端 | Go server client | 可信拨出客户端：认证、心跳、RPC 关联、重连、注入、事件和设备在线回调。 | sdks/go/client.go:141 |
| 桌面服务图 | Desktop service graph | 组装 App 边缘连接、可选 social/party/voice 连接、API 与进程内 UI store。 | apps/desktop/src/api/services.ts:72 |
| Web 服务图 | Web service graph | 按 Vite 环境变量或同源代理组装 chat、social、party、voice 与 app-edge/device 服务图。 | apps/web_companion/src/api/services.ts:111 |
| SDK 钩子 | SDK hooks | 可选的收发拦截器、令牌提供方、本地存储、生命周期监听和本地命令处理接缝。 | sdks/ts/src/hooks.ts:61 |
| 客户端消息存储 | Client MessageStore | C++、TypeScript、Kotlin、C# 与引擎门面共用的 SDK 本地档案契约。 | sdks/core/include/chirp/message_store.h:17 |
| 客户端传输 | Client transport | 共享连接状态机之下的平台 WebSocket/TCP 接缝。 | sdks/unity/Runtime/Chirp/ChirpTransport.cs:14 |
