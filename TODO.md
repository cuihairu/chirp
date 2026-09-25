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
- [x] **敏感词过滤**（2026-09-22：game_chat_features P0 第一项落地——`WordFilter` 三级策略 `--word_filter_policy replace|reject|record`（replace=命中区段合并折叠为 `**`、reject=拒收回 `INVALID_PARAM`（暂无专码，等 proto 批次）、record=放行留 Warn 痕），词库 `--word_filter_file` 每行一词、`#` 注释、大小写归一去重，mtime 惰性热更新（默认 5s 节流），词库缺失/不可读 fail-open 空转不挡聊天；basic/enhanced 两形态接线，位置在发送限流之后（拒绝仍耗预算）、跨平面拦截之前（过滤后内容不进游戏平面）；NPC/服务端注入不过滤（服务端可信）；`word_filter_tests` 11 例）
- [x] **消息长度限制**（2026-09-22：game_chat_features P0 第二项——按频道码点上限：私聊 200 字、世界 100 字、系统公告 500 字，TEAM/GUILD/MARQUEE 不限；`chat_validation` 新增公开 `MaxContentChars`/`ValidateContentLength`，按 UTF-8 非续字节计码点（CJK 3 字节/字不误伤），超限拒收回 `INVALID_PARAM`（不做截断）；basic 经 `ValidateSendMessageRequest` 自动获得，enhanced 在 `on_send_message` 词过滤之前调用（超长不耗词库工作；MySQL 构建列表补链 `chat_validation.cc`）；`chat_validation_tests` 新增 8 例达 35/35）
- [x] **发送频率限制**（2026-09-22：game_chat_features P0 第三项——`ChannelPacer` 按用户×频道最小间隔：世界 5s/公会 2s/私聊 1s，TEAM/MARQUEE/系统公告不限；窗口锚定在最后一次**放行**的发送，窗口内被拒的重试不延长等待；超频回 `RATE_LIMITED`；io 线程专有内存态（进程内限流，多实例部署下每实例独立计数——模糊闸仍是 Redis 全局口径）；basic 在模糊闸后、词过滤前接线（filter 拒绝也耗节奏槽），enhanced 在长度校验后接线、键取 registry 认证身份；`channel_pacer_tests` 8 例）
- [x] **enhanced 直连入口补每用户发送模糊闸**（2026-09-22：`--send_rate_limit_per_min` 接入 enhanced 的 `on_send_message`(长度校验后、节奏限流前,只对已认证会话计数,无 Redis/未开启时惰性直通,与 basic 同一 fail-open 契约);沿用 enhanced 模糊闸的**默认关**约定(`--login_rate_limit_per_min` 同款,默认 0),要开需显式配置。同批修掉 pacing 引入的 smoke flake:`--smoke-chat` 的 user_1 四连私聊间隔可能 <1s,补 3 处 `sleep 1.1` 模拟守节奏客户端）
- [x] **重复消息检测**（2026-09-22：game_chat_features P0 第四项——`RepeatGuard` 连续相同内容禁言：同用户连续第 3 条相同内容触发 5 分钟禁言,**触发那条本身也拒发**（不让第 3 条刷屏到达），禁言期内任何内容都拒（回 `RATE_LIMITED`）,禁言到期计数重置；插入内容不同即重置连击；io 线程专有内存态；basic 在节奏限流后、词过滤前接线,enhanced 同位置（键取 registry 认证身份）；`repeat_guard_tests` 6 例）
- [x] **频道屏蔽**（2026-09-22：game_chat_features P0 频道管理——`DeliveryPrefs` 每用户推送过滤器:仅 WORLD/GUILD/TEAM 可屏蔽(MARQUEE/系统公告是服务广播不可关,私聊归黑名单)；SET_CHANNEL_MUTE/GET_CHANNEL_MUTES(2235-2238),非可屏蔽频道拒回 `INVALID_PARAM`,未登录回 `AUTH_FAILED`,GET 恒回三项按 WORLD/GUILD/TEAM 序；屏蔽=推送过滤——历史仍可拉、屏蔽前已入离线队列的补投照发、群组 join/left 通知不过滤；basic 在群播尾段 `notify_member` 钩子过滤(实时跳过+离线不入队),本地手动探针经 game_sdk_gateway 完整管道 15 项断言全绿(屏蔽期世界消息不到达、GUILD 照常、解除恢复、不可屏蔽拒收)；enhanced 接 RPC 面+状态(两形态 API 对齐;其群播本地扇出暂缺订阅端——`BroadcastToGroup` 只发布无 `SubscribeGroupChat` 消费者,既有缺口,过滤随该路径落地生效)；io 线程专有内存态(与 ChannelPacer/RepeatGuard 同契约)；`delivery_prefs_tests` 6 例+分发用例 2 例)
- [x] **黑名单**（2026-09-23：game_chat_features P0 收官项——拉黑后对方的世界/公会/队伍频道消息按成员过滤、私聊静默成功(不暴露拉黑态)且不入离线队列,拉黑前已入离线队列的补投照发；BLOCK_MESSAGE_SENDER/UNBLOCK_MESSAGE_SENDER/GET_BLOCKED_SENDERS(2239-2244),空目标与自拉黑拒回 `INVALID_PARAM`,未登录回 `AUTH_FAILED`,解除幂等(解未拉黑者亦 OK)；与社交面 BLOCK_USER(3011,好友关系)互相独立,只作用于消息投递；basic 双钩子——私聊尾段在会话查找前整体吞掉(回 OK)、群播 `notify_member` 按成员检查 `msg.sender_id`(其他成员照常收到)；enhanced 在 local_send 回调(报"已投递"阻止离线入队)与跨实例 `SubscribeUserChat` 回调静默丢弃；io 线程专有内存态(与屏蔽同契约)；`delivery_prefs_tests` 新增 5 例+分发用例 3 例,端到端探针经 game_sdk_gateway 完整管道 23 项断言全绿(拉黑期世界+私聊不到达、私聊静默 code=0、C 用户旁证按发送者过滤、解除恢复、自拉黑拒、解未拉黑幂等)

### game_server_gateway（原 server_gateway，瘦身版）

- [x] 服务凭证认证（`SERVER_AUTH_REQ`）
- [x] 消息注入（`INJECT_MESSAGE_REQ` → `InjectMessageNotify`）
- [x] 事件下发（`EVENT_PUBLISH_REQ` / `EVENT_DELIVER_NOTIFY` / `EVENT_ACK_REQ`）
- [x] Redis Streams broker 回退
- [x] **搬走 WP-8 功能**（2026-09-22：身份绑定/频道订阅/未读计数三个 registry 与 9 个 WP-8 RPC（5013-5030）整体迁入 `services/shared/chat/src`（`identity_registry`/`subscription_registry`/`unread_ledger`/`player_directory`，命名空间 `chirp::chat`），`chirp_game_server_gateway` 只保留注入 + 事件；hub 对带 `game_id` 的注入回 `INVALID_PARAM`，`server_gateway_test` 瘦身至 53 例，WP-8 覆盖迁至新增 `chat_player_directory_tests` 55 例）
- [x] **瘦身后验证**（2026-09-22：主树 ctest 35/35 全绿；`test_services.sh --smoke-npc` 真实进程注入链路通过（NPC 关键词回复/兜底/历史/离线补投四环节），支持 `CHAT_BIN` 覆盖以无 MySQL 树跑 smoke（同 web_smoke 先例）；`--smoke-game` 需 MySQL（CI 容器内验证））

## App 平面（P1，游戏平面稳定后开始）

### app_sdk_gateway（原 app_gateway）

- [x] 基础登录/心跳/踢出/会话 claim
- [x] 6xxx 设备消息转发到 app_notification
- [x] ChatBridge 转发 2xxx 到 app_chat
- [x] **对接 app_auth + 自服务链切到 app_chat**(2026-09-22:app 边缘 `--auth_host` 指 app_auth(scaffold 同链可用);WP-8 迁入 app_chat 后转发目标随之切换——`--sg_host/--sg_port` 指 app_chat 主端口、`--sg_secret` 用其 `--gateway_service_secret`,零代码改动纯配置;`--smoke-edge` 端到端证明:新增 `chirp_wp8_client` 以空 `player_id` 登录后走订阅(幂等重订回同 id)/未读摘要/标读/退订/列表五 RPC,空 `player_id` 拿到 OK 即证明边缘把身份钉死为登录用户;`AUTH_BIN`/`CHAT_BIN` 覆盖让无 MySQL 树本地可跑 edge smoke,离线发送断言放宽为 `code=0|6` 对齐两形态语义分歧)

### app_chat（原 chat，部署为 App 平面 hub）

- [x] 基础聊天能力（与 game_chat 同一二进制）
- [x] **hub 模式**：接受 game_chat 的 `PEER_REGISTER_REQ`，白名单 + 版本协商（2026-09-22：9edaca3 将两种构建形态统一接线 libs 层 `ChatPeerHub`——`--hub_mode`/`--hub_peer_port`（默认 8200）独立监听注册面，`--allowed_peers` 白名单 + `--min_peer_version`/`VERSION_MISMATCH` 拒绝，同 id 顶替、心跳 idle 踢出、断线重连；`chat_peer_test` 36 例含真实 link↔hub 端到端）
- [x] **身份映射**（2026-09-22：`PlayerDirectory` + `IdentityRegistry` 落地 app_chat，`BIND/UNBIND/GET/RESOLVE` 四 RPC 经 `SERVER_AUTH_REQ` 信任门在 chat 主端口应答，(game_id, game_user_id) 唯一索引 replace-on-reassert，可选 Redis 镜像跨重启）
- [x] **频道订阅**（2026-09-22：`SubscriptionRegistry` 落地，SUBSCRIBE/UNSUBSCRIBE/GET 三 RPC，(player, game, channel) 三元组唯一索引 + (game, channel) 反向扇入索引；后端断言带幂等键，自服务空 id 由服务端铸 `sub-` id 且收敛稳定）
- [x] **跨平面 fan-out**（2026-09-22：`PlayerDirectory::FanoutChannelMessage` 在 hub 侧承接 spoke 的 `CHANNEL_MESSAGE_NOTIFY` 上行，每订阅者一份私信副本交接 + 未读自增；空订阅语义 no-op、超 `--max_fanout_per_message` 整条丢弃告警）
- [x] **跨平面回复**（2026-09-22：hub 模式拦截带 `<game_id>:<bare>` 前缀的非私聊发送——`PlayerDirectory::RelayGameReply` 编排，`ChatPeerHub::service_id_for_game` 反查在线 spoke + `IdentityRegistry::ResolveGameUser` 反查游戏身份（同游戏多绑定取字典序最小保证确定性），`PEER_INJECT_MESSAGE_NOTIFY` 注入 spoke（裸频道 ID、游戏侧铸 message_id、走存储/离线队列同一尾段）；回码 OK / SERVER_UNAVAILABLE（无在线 spoke 或下行失败）/ INVALID_PARAM（发送者无该游戏绑定），拒绝显式回码不降级本地频道；basic/enhanced 两形态接线，拦截点在发送限流之后（消耗发送预算）、mention 处理之前（内容透传游戏侧）；无回环——扇回 App 玩家的是无前缀私聊副本，不再触发本路径）
- [x] **未读计数**（2026-09-22：`UnreadLedger` 落地，fan-out 每份被接受副本自增 badge；`MARK_CHANNELS_READ` 分层选择器（单频道/单游戏/全部）幂等清除，`GET_UNREAD_SUMMARY` 按 (game, channel) 排序含过滤与 total_unread）
- [x] **离线推送触发**：消息投递时调 app_notification（PushBridge + NotificationClient 在 basic/enhanced/distributed 三入口全部接线，私聊接收方无健康会话、群广播离线成员、注入离线入队三处触发，`--notification_host` 门控；peer 注入路径同样落离线队列）
- [x] **enhanced 会话语义修复**（2026-09-22：`DistributedChatState` 不再自带 user 维度单 slot——存储整体换装共享 `SessionRegistry`（与 basic 同一份 (user, device) 互踢内核，`BindAuthenticatedSession` 返回同对旧会话），同对重登发 `KICK_NOTIFY`（"login from another device"）+ `LOGIN_RESP.kick_previous`，另一设备共存。本地投递（私聊/注入/扇出副本/peer 注入）与跨实例回调从单 slot 改为 `HealthyLocalSessions` 全设备扇出（半关连接跳过；`TrackAckIfCapable` 任一 ack-capable 设备即挂起待 MESSAGE_ACK），`RemoveSession`/`GetUserId` 走 registry 语义（陈旧断开不顶掉新会话）。互踢内核由 `session_registry_test` 12 例覆盖）

### app_auth（原 auth）

- [x] 基础 token 验证
- [x] enhanced 模式（MySQL + libsodium）
- [x] 确认只服务 App 平面，game 平面不依赖（2026-09-22 审计：`--auth_host` 仅非空时才建 `AuthClient`；游戏平面 scaffold + chat `--token_secret` 本地验签自足闭环，`app_auth` 只服务 App 平面；结论已落 architecture.md「凭证模型」）
- [x] **PostgreSQL 存储后端**（已决策：暂缓，等真实需求触发再立任务。接缝 2026-09 就绪——MySQL 驱动已换 libmariadb（`mysql_*` C API 兼容，vcpkg/CMake/Docker 三路径同步）；auth 的 `UserStore`/`SessionStore` 与 chat 的 `MessageStore` 均为后端中立纯虚接口，`services/app/auth/src/store_factory.cc` 与 `services/shared/chat/src/message_store_factory.cc` 是唯一换装点，届时新增 `postgres_*_store` + 工厂各一分支即可，调用方零改动；不引入 ORM，维持手写 SQL，chat 的 MySQL 方言留在实现内）

### app_notification（原 notification）

- [x] 设备注册/注销/token 更新/查询
- [x] 推送协议面（6xxx）
- [x] 修复 namespace 重命名后的构建问题（2026-09-22：`chirp::app_notification` 全链一致，push_transport/http_push_transport 单测在测，333/333 目标构建通过）
- [x] **真实 APNs/FCM 投递**（2026-09-22：通道做实——`HttpPushTransport` 经 `SslHttpConnectionFactory` 支持 https（TLS 1.2+，证书校验，SNI 仅对主机名；`--push_ca_file` 私有 CA、`--push_verify_tls off` 调试豁免）；端点全部可配（`--fcm-endpoint`/`--apns-endpoint`/`--apns-sandbox`）；无 token 设备改为显式失败（原来「无 token 记成功」的 stub 语义已删）。测试：TLS 回环 9 例（可信 CA/错误 CA/超时/大响应跨 record/明文回落）+ 语义单测，覆盖率保持 100%。APNs HTTP/2 与真实凭据接入留待部署环境（APNs 要求 HTTP/2，生产部署在本通道前置协议转换或走 provider 的 HTTP/1.1 兼容 API））

## 跨平面协议（P1）

- [x] **定义 peer 注册协议**：`PEER_REGISTER_REQ`（5050）/ `PEER_REGISTER_RESP`（5051）proto 定义
- [x] **定义频道消息协议**：`CHANNEL_MESSAGE_NOTIFY`（5052）/ `PEER_INJECT_MESSAGE_NOTIFY`（5053）proto 定义
- [x] **能力位定义**：`RELAY_READ_RECEIPTS`、`RELAY_TYPING`、`RELAY_PRESENCE`、`RELAY_OFFLINE_MESSAGES`
- [x] **版本协商实现**：握手时交换 protocol_version + supported_features（2026-09-22：libs 层 `ChatPeerHub`/`ChatPeerLink` 双向交换并校验，hub 按 `min_peer_version` 拒绝并回 `VERSION_MISMATCH` + min_version，spoke 收非 OK 断线重试；`chat_peer_test` 覆盖 mismatch 重试与 hub 拒绝两向。服务层旧实现连同其 resp 版本回带 bug 已随 9edaca3 删除）

## 构建与验证（P0）

- [x] 目录重构：`services/game/`、`services/app/`、`services/shared/`
- [x] 二进制重命名：`chirp_game_sdk_gateway`、`chirp_game_server_gateway`、`chirp_app_sdk_gateway`、`chirp_app_auth`、`chirp_app_notification`
- [x] proto 包重命名：`chirp.game_server_gateway`、`chirp.app_notification`
- [x] **更新 smoke test**：`test_services.sh` 适配新路径和二进制名，验证游戏平面端到端（2026-09-22：require_bin 预检 + find 全深度产物列举 + 新增 `--smoke-game` 纯游戏平面 E2E（无 `app_auth`）；可选 `MYSQL_*` 环境变量透传给 enhanced auth/chat；`--smoke-game` 本地通过，CI 已挂 smoke job）
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
