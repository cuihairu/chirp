# Glossary

> 🌐 Language: **English** (this page) | [中文](zh/glossary.md)

This glossary uses responsibility-based abstractions, not directory names. Each anchor points to the source declaration that defines the concept. Return to the [architecture overview](architecture.md) for layer relationships.

| 术语 | English | 定义 | file:line |
| --- | --- | --- | --- |
| 协议信封 | Packet | All TCP and WebSocket payloads use one protobuf envelope carrying `msg_id`, `sequence`, body, and `request_id`. | proto/gateway.proto:358 |
| 消息编号 | MsgID | The shared message-id registry partitions authentication, chat, social, voice, trusted-server, notification, and party messages. | proto/gateway.proto:11 |
| 帧编解码 | Frame codec | Converts a protobuf packet to or from a big-endian `uint32` length prefix on a stream or WebSocket binary message. | libs/network/protobuf_framing.h:12 |
| 连接会话 | Session | Transport-neutral send, close, half-close, and peer-address contract shared by TCP and WebSocket sessions. | libs/network/session.h:7 |
| TCP 服务端 | TcpServer | Accepts framed TCP sessions and exposes start/stop lifecycle callbacks to services. | libs/network/tcp_server.h:14 |
| WebSocket 服务端 | WebSocketServer | Accepts binary WebSocket sessions using the same callback shape as `TcpServer`. | libs/network/websocket_server.h:13 |
| 进程配置 | Config | Optional key/value file reader; service deployments otherwise pass typed command-line flags. | libs/common/config.h:10 |
| 必填参数 | RequireArg | Refuses to start a process when a security-sensitive flag, such as a JWT signing secret, is absent. | libs/common/required_arg.h:28 |
| 运行指标 | Metrics counters | In-process counters and gauges exposed through the optional Prometheus-text HTTP endpoint. | libs/common/include/common/metrics.h:179 |
| 指标端点 | MetricsHttpServer | Small ASIO HTTP listener that serves runtime metrics and degrades the service when its bind fails. | libs/common/include/common/metrics_http_server.h:17 |
| 崩溃采集 | Crash handler | Initializes out-of-process crash capture before flag parsing or listener construction. | libs/common/crash_handler.h:33 |
| 会话注册表 | SessionRegistry | In-memory `(user, platform)` binding table; rebinding the same slot kicks the old session and different platforms coexist. | libs/network/session_registry.h:29 |
| Redis 会话认领 | Redis session claim | Cross-instance claim and kick mechanism using `chirp:sess:*` keys and per-instance kick channels. | libs/network/redis_session_manager.h:26 |
| 消息路由 | MessageRouter | Redis pub/sub wrapper for user/group/social/kick channels, with local-only operation when Redis is absent. | libs/network/message_router.h:17 |
| 服务桥 | ServiceBridge | One authenticated internal connection per client session, forwarding edge packets to chat or search with kick or degrade semantics. | libs/network/service_bridge.h:54 |
| 聊天对等枢纽 | ChatPeerHub | Hub side of game-chat to app-chat attachment, enforcing whitelist/version negotiation and cross-plane fan-out. | libs/network/chat_peer_hub.h:38 |
| 服务网关对端 | ServerGatewayPeer | Dial-out trusted-client contract for server authentication, heartbeats, injection, publishing, and event acknowledgement. | libs/network/server_gateway_peer.h:30 |
| 服务注册表 | ServiceRegistry | Maps a connected `service_id` to one live server-plane peer and returns the displaced peer on replacement. | services/game/server_gateway/src/service_registry.h:28 |
| 流代理 | StreamBrokerConsumer | Reads Redis Stream injection entries, authenticates producers, and converts at-least-once entries into server-gateway RPCs. | services/game/server_gateway/src/stream_broker.h:75 |
| 接入网关 | Access gateway | Client-facing game/app edge that authenticates or scaffold-logins a session, claims its slot, and forwards business packets. | services/game/sdk_gateway/src/main.cc:375 |
| 认证服务 | AuthService | Password login, registration, token issuance, refresh-token and session-limit policy backed by MySQL and Redis. | services/app/auth/src/auth_service.h:29 |
| 推送服务 | NotificationService | Stores device targets and forwards provider-independent push requests to logging, HTTP, APNs, or FCM behavior. | services/app/notification/src/notification_service.h:89 |
| 设备存储 | DeviceStore | Opt-in atomic line-oriented file snapshot for registered push devices. | services/app/notification/src/device_store.h:36 |
| 聊天核心 | Chat core | Single binary and business plane for game chat, app chat, cross-plane bridges, delivery, groups, and moderation. | services/shared/chat/src/main_enhanced.cc:1152 |
| 混合消息存储 | HybridMessageStore | Redis hot history/offline queues plus MySQL archive, with fallback queues and asynchronous cold-tier retries. | services/shared/chat/src/hybrid_message_store.h:66 |
| 投递跟踪 | MessageDeliveryTracker | Pending-delivery lifecycle, retries, acknowledgements, and statistics on top of the hybrid store. | services/shared/chat/src/message_delivery_tracker.h:18 |
| 玩家目录 | PlayerDirectory | App-plane identity bindings, subscriptions, unread ledgers, game presence, and bounded fan-out copies. | services/shared/chat/src/player_directory.h:52 |
| 搜索索引 | MessageSearchIndex | SQLite FTS5 projection of authoritative MySQL messages, including id mapping, deletion, and cursor search. | services/search/src/message_search_index.h:25 |
| 社交状态 | SocialState | In-memory friends, requests, blocks, notification preferences, presence, and game-presence overlay with optional Redis snapshots. | services/social/src/main.cc:86 |
| 组队状态 | PartyState | In-memory parties, invites, accepted-invite markers, and session bindings; parties persist optionally to Redis while invites are ephemeral. | services/party/src/main.cc:112 |
| 语音状态 | VoiceState | In-memory rooms, user-to-room mapping, session bindings, login gate, TURN credentials, and heartbeat timeouts. | services/voice/src/main.cc:228 |
| Go 服务客户端 | Go server client | Trusted dial-out client with authentication, heartbeat, RPC correlation, reconnect, injection, events, and device-presence callbacks. | sdks/go/client.go:141 |
| 桌面服务图 | Desktop service graph | Constructs the app edge connection, optional social/party/voice connections, APIs, and in-memory UI stores. | apps/desktop/src/api/services.ts:72 |
| Web 服务图 | Web service graph | Constructs chat, social, party, voice, and app-edge/device service graphs from Vite environment overrides or same-origin proxies. | apps/web_companion/src/api/services.ts:111 |
| SDK 钩子 | SDK hooks | Optional send/receive interceptors, token provider, local store, lifecycle listener, and command-handler seams. | sdks/ts/src/hooks.ts:61 |
| 客户端消息存储 | Client MessageStore | SDK-local archive contract used by C++, TypeScript, Kotlin, C#, and engine facades. | sdks/core/include/chirp/message_store.h:17 |
| 客户端传输 | Client transport | Platform WebSocket/TCP seam beneath shared connection state machines. | sdks/unity/Runtime/Chirp/ChirpTransport.cs:14 |
