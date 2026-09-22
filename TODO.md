# Chirp 任务清单

> 最后更新：2026-09-22（第二次）：勾选 hub 模式/离线推送/版本协商/CI/单测/notification 构建六项（盘点核验已实现），APNs 项标注进行中边界。

## 当前焦点

游戏平面优先，先把 game_sdk_gateway + game_chat + game_server_gateway 端到端跑通。

详细特征清单见 [游戏聊天特征](docs/design-notes/game_chat_features.md)。
SDK 引擎兼容性见 [SDK 引擎兼容性](docs/design-notes/sdk_compatibility.md)。

## 游戏平面（P0）

### game_sdk_gateway（原 gateway）

- [x] 基础登录/心跳/踢出/会话 claim
- [x] ChatBridge 转发 2xxx 到 game_chat
- [x] **修复 scaffold 分支不 bind session 的 bug**：无 `--auth_host` 时登录成功但 2xxx 被静默丢弃，需要对齐 app_sdk_gateway 的 scaffold 行为（补 `BindAuthenticatedSession`）
- [x] 统一 TCP/WS 两份 switch 为 `HandleClientPacket`（已做，确认无残留）

### game_chat（原 chat，部署为游戏平面实例）

- [x] 基础聊天：私聊、群组、已读回执、正在输入、表情回应、消息编辑/删除、@提及
- [x] 历史、离线队列、可选 Redis/MySQL 存储
- [x] 本地验证 token（`--token_secret`，HS256 JWT）
- [x] trusted peer 信任门（`--gateway_service_secret`，gateway 管道免限流）
- [x] **注册协议**：作为 spoke 连接 app_chat，发送 `PEER_REGISTER_REQ`，支持白名单 + 版本协商
- [x] **频道消息推送**：登录后向已注册的 hub peer 推送频道消息（`CHANNEL_MESSAGE_NOTIFY`）
- [x] **接收玩家回复**：从 hub peer 收到 `INJECT_MESSAGE_NOTIFY`，注入本地频道

### game_server_gateway（原 server_gateway，瘦身版）

- [x] 服务凭证认证（`SERVER_AUTH_REQ`）
- [x] 消息注入（`INJECT_MESSAGE_REQ` → `InjectMessageNotify`）
- [x] 事件下发（`EVENT_PUBLISH_REQ` / `EVENT_DELIVER_NOTIFY` / `EVENT_ACK_REQ`）
- [x] Redis Streams broker 回退
- [ ] **搬走 WP-8 功能**：身份绑定/频道订阅/未读计数迁移到 app_chat 内部，game_server_gateway 只保留注入 + 事件
- [ ] **瘦身后验证**：smoke test 确认注入链路正常

## App 平面（P1，游戏平面稳定后开始）

### app_sdk_gateway（原 app_gateway）

- [x] 基础登录/心跳/踢出/会话 claim
- [x] 6xxx 设备消息转发到 app_notification
- [x] ChatBridge 转发 2xxx 到 app_chat
- [ ] 对接 app_auth（替代原来的共享 auth）

### app_chat（原 chat，部署为 App 平面 hub）

- [x] 基础聊天能力（与 game_chat 同一二进制）
- [x] **hub 模式**：接受 game_chat 的 `PEER_REGISTER_REQ`，白名单 + 版本协商（2026-09-22：9edaca3 将两种构建形态统一接线 libs 层 `ChatPeerHub`——`--hub_mode`/`--hub_peer_port`（默认 8200）独立监听注册面，`--allowed_peers` 白名单 + `--min_peer_version`/`VERSION_MISMATCH` 拒绝，同 id 顶替、心跳 idle 踢出、断线重连；`chat_peer_test` 36 例含真实 link↔hub 端到端）
- [ ] **身份映射**：持有 `player_id ↔ (game_id, game_user_id)` 绑定，game 后端调 `BIND_PLAYER_IDENTITY` RPC
- [ ] **频道订阅**：持有玩家订阅的 `(game_id, channel_id)` 列表
- [ ] **跨平面 fan-out**：收到 `CHANNEL_MESSAGE_NOTIFY` 后查询订阅者，注入私信副本
- [ ] **跨平面回复**：收到带 `{game_id}:` 前缀的消息后，解析 player_id → game_user_id，注入 game_chat
- [ ] **未读计数**：fan-out 时自增 badge，提供 `MARK_CHANNELS_READ` / `GET_UNREAD_SUMMARY`
- [x] **离线推送触发**：消息投递时调 app_notification（PushBridge + NotificationClient 在 basic/enhanced/distributed 三入口全部接线，私聊接收方无健康会话、群广播离线成员、注入离线入队三处触发，`--notification_host` 门控；peer 注入路径同样落离线队列）
- [ ] **enhanced 会话语义修复**：AddSession 改为 (user, device) 维度互踢，对齐 basic 的 session_registry 行为

### app_auth（原 auth）

- [x] 基础 token 验证
- [x] enhanced 模式（MySQL + libsodium）
- [ ] 确认只服务 App 平面，game 平面不依赖（待办：审计 game_sdk_gateway 的 `--auth_host` 依赖面与 scaffold/本地验签的独立性，结论落 architecture.md）
- [x] **PostgreSQL 存储后端**（已决策：暂缓，等真实需求触发再立任务。接缝 2026-09 就绪——MySQL 驱动已换 libmariadb（`mysql_*` C API 兼容，vcpkg/CMake/Docker 三路径同步）；auth 的 `UserStore`/`SessionStore` 与 chat 的 `MessageStore` 均为后端中立纯虚接口，`services/app/auth/src/store_factory.cc` 与 `services/shared/chat/src/message_store_factory.cc` 是唯一换装点，届时新增 `postgres_*_store` + 工厂各一分支即可，调用方零改动；不引入 ORM，维持手写 SQL，chat 的 MySQL 方言留在实现内）

### app_notification（原 notification）

- [x] 设备注册/注销/token 更新/查询
- [x] 推送协议面（6xxx）
- [x] 修复 namespace 重命名后的构建问题（2026-09-22：`chirp::app_notification` 全链一致，push_transport/http_push_transport 单测在测，333/333 目标构建通过）
- [ ] **真实 APNs/FCM 投递**（进行中：HttpPushTransport 已是真 HTTP/1.1 POST 通道；待做 TLS 支持、端点可配、修掉「无 token 记成功」的 stub 语义；真实凭据接入留待部署环境）

## 跨平面协议（P1）

- [x] **定义 peer 注册协议**：`PEER_REGISTER_REQ`（5050）/ `PEER_REGISTER_RESP`（5051）proto 定义
- [x] **定义频道消息协议**：`CHANNEL_MESSAGE_NOTIFY`（5052）/ `PEER_INJECT_MESSAGE_NOTIFY`（5053）proto 定义
- [x] **能力位定义**：`RELAY_READ_RECEIPTS`、`RELAY_TYPING`、`RELAY_PRESENCE`、`RELAY_OFFLINE_MESSAGES`
- [x] **版本协商实现**：握手时交换 protocol_version + supported_features（2026-09-22：libs 层 `ChatPeerHub`/`ChatPeerLink` 双向交换并校验，hub 按 `min_peer_version` 拒绝并回 `VERSION_MISMATCH` + min_version，spoke 收非 OK 断线重试；`chat_peer_test` 覆盖 mismatch 重试与 hub 拒绝两向。服务层旧实现连同其 resp 版本回带 bug 已随 9edaca3 删除）

## 构建与验证（P0）

- [x] 目录重构：`services/game/`、`services/app/`、`services/shared/`
- [x] 二进制重命名：`chirp_game_sdk_gateway`、`chirp_game_server_gateway`、`chirp_app_sdk_gateway`、`chirp_app_auth`、`chirp_app_notification`
- [x] proto 包重命名：`chirp.game_server_gateway`、`chirp.app_notification`
- [ ] **更新 smoke test**：`test_services.sh` 适配新路径和二进制名，验证游戏平面端到端
- [x] **更新 CI**：`ci.yml` 适配新路径（2026-09-22 核验：smoke/build-and-test/coverage 均构建两平面完整树，跑全部 7 个 smoke 模式；`.github/`/`scripts/`/CMake/`docker/`/`deploy/` 旧路径 grep 零命中）
- [x] **更新单元测试**：路径和 namespace 重命名后的测试修复（2026-09-22 核验：tests/unit 34 个目标全部引用新路径与新 namespace（`chirp::auth`/`chirp::gateway`/`chirp::app_notification`），旧路径残留 grep 零命中，ctest 34/34 通过）
- [x] **全量构建验证**：2026-09-22 clean build（vcpkg toolchain + Debug + ENABLE_TESTS=ON)333/333 目标通过，13 个 `chirp_*` 服务二进制全部产出，`ctest` 34/34 通过

## 文档（P2）

- [x] architecture.md 全文中文，反映新架构
- [x] README.md 更新拓扑图和服务表
- [x] CORE.md 更新架构说明
- [x] server_plane.md 更新二进制名
- [x] 设计并集成 logo
- [x] CAPABILITY_MATRIX.md 更新服务名和路径（2026-09-22：`chirp_game_sdk_gateway` / `chirp_app_auth` / `chirp_app_sdk_gateway` / `chirp_app_notification` / `chirp_game_server_gateway` 全部对齐，补充二进制命名约定段）
- [x] 补充 peer 注册协议的详细文档（2026-09-22：新增 `docs/api/peer_protocol.md`，覆盖握手、字段、错误码、能力位、白名单、CLI、部署示例与实现状态；vitepress sidebar 与 architecture.md 已交叉引用）

## 实验性服务（暂不动）

`services/social`、`services/voice`、`services/party`、`services/search` 保持原样，后续按需迁移到对应平面目录。
