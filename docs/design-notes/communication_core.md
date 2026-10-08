# Chirp Communication Core 模型收敛

> 状态说明：本文是架构收敛草案。文中 [现状] 代表当前已实现的运行面，[提案] 代表设计意图、尚未落地。请勿把 [提案] 当作默认支持的运行时功能。本文供后续协议与服务演进时对齐概念，不改变现有 wire 协议。

## 1. 为什么收敛

chirp 已从「聊天服务器」长成「面向游戏的实时通信基础设施」：游戏平面（客户端边缘 + 游戏内 chat）、App 平面（伴侣侧标识、推送、hub）、服务平面（游戏服出站注入、NPC/系统消息）、外加 social / voice / notification / search / npc 等实验面。

风险随之而来：如果继续按「Chat → Social → Voice → Notification …」横向加功能，最终会变成什么都做一点的大杂烩。核心抽象必须先收敛成一份共享词汇表，让新能力都成为 Communication 之上的应用，而不是往核心里塞新业务。

选「Communication」而不是「Chat」作为第一层抽象，是本文件唯一要做的判断。

## 2. 核心模型

```
Communication
├── Identity   谁在说话（player / npc / system / service / bot）
├── Channel    在哪说话（private / group / world / system 前缀频道）
├── Message    说了什么（message_id / sender / receiver / channel / payload / metadata）
├── Delivery   如何送达（离线队列、at-least-once 重投、回执）
├── Pipe       谁连谁（client / service / peer 三类受信连接）
└── Event      服务面/客户端的事件通道（注入、推送、踢人、正在输入…）
```

- Chat = Message + Channel + (部分)Delivery 的第一批消费者。
- NPC / Social / Party / Notification = 复用身份与注入通道的上层应用，不是核心实体。
- Voice / Search = 独立模块，不进入 Communication 核心（见 §8）。

## 3. 现状对照

每一条都对应当前可运行实现，不画饼：

| 概念 | [现状] |
| --- | --- |
| Identity | 平台侧 `player_id`（app_auth JWT）、游戏内 `game_user_id`（game_sdk_gateway 本地 token）；服务面以 `SERVICE` 身份注入。注入消息已带非玩家身份（SYSTEM / NPC / SERVICE 语义在 server_gateway 注入面落地，见 services/game/server_gateway 与 npc_dialog） |
| Channel | 私聊、群聊（`groups`）、世界频道（batch 消息面）、系统频道前缀（`<game_id>:<频道>` 跨平面寻址）；`npc:` 前缀私聊走 NPC 注入线 |
| Message | `message_id` 生成、历史存 MySQL（`messages` 表）、离线队列、消息编辑/删除/表情回应/已读回执（`read_receipts`/`read_cursors`）；Sender kind 以 sender 字段区分玩家与非玩家 |
| Delivery | 接收方离线 → 离线队列（内存兜底 200 条/用户，可选 Redis），`SEND_MESSAGE_RESP` 回 `TARGET_OFFLINE`，下次登录补投；服务面注入 at-least-once（hub ack + Redis Streams PEL 重放）；「`OK` 只代表服务面受理、不代表玩家侧送达」已写进 README 当前边界 |
| Pipe | 客户端边缘（`game_sdk_gateway` 5000/5001、`app_sdk_gateway` 5200/5201）、服务面出站长连接（`game_server_gateway` 8100）、chat 直连入口与 trusted bridge（`--smoke-edge` 验证的 trusted pipe）、game_chat ↔ app_chat 注册 + 白名单 + 版本协商 |
| Event | 服务面注入通道（`InjectMessageNotify`）、chat 离线推送桥 → `app_notification`（6xxx）、正在输入/已读事件 |

## 4. 目标模型（[提案]）

### 4.1 ActorKind（2026-10-04 收敛落地，形态调整）

把现有的「玩家 / NPC / SYSTEM / SERVICE」sender 语义正式收敛成一个枚举，作为 Message 与注入事件的 sender 侧身份。**落地形态说明**：没有新造六值 `ACTOR_*` 枚举，而是把既有两侧枚举钉成对齐契约——`chat.proto SenderKind`（USER=0 / SYSTEM / NPC / SERVICE）与 `game_server_gateway.proto SenderKind`（UNKNOWN=0 / SYSTEM / NPC / SERVICE）数值对齐，注释互指；注入边界（`inject_consumer`）按契约直传，UNKNOWN 拒收。已随链路落地：

- **全链持久化**：`MessageData.sender_kind` 随 Redis 镜像序列化往返；`messages` 表 `sender_kind` 列（新建库 `init_db.sql`，存量库 `scripts/upgrade_db_messages.sql`）；migration worker 转换带 kind；`GetHistory` 冷热合并、离线补投、历史下发全程重建 kind。
- **扇出保真**：跨平面 `deliver_copy` 逐订阅者私有副本保留源 kind（NPC/系统公告不降级成玩家，兑现 proto 扇入「SENDER_SERVICE 副本」承诺）。
- **防伪造**：`SendMessageRequest` 无 sender_kind 字段，客户端结构上无法声称非玩家身份。

```
ACTOR_PLAYER     玩家（客户端边缘验证过的用户）—— chat.SenderKind.SENDER_USER
ACTOR_NPC        规则/LLM 驱动的对话角色             —— SENDER_NPC（已贯通）
ACTOR_SYSTEM     系统公告、状态播报                   —— SENDER_SYSTEM（已贯通）
ACTOR_SERVICE    游戏服服务面身份（service_id + secret）—— SENDER_SERVICE（已贯通）
ACTOR_BOT        客服/审核/运营机器人                —— 未实现（无生产者，不加枚举值）
ACTOR_GM         GM                                  —— 未实现（同上）
```

约束：**用户凭证与服务凭证不可混用**——客户端边缘只验用户 token，服务面只验服务凭证，两个平面在注入边界做身份声明而不是信任客户端传入的 kind。

### 4.2 Message / Delivery 状态

```
Message  ── 发送 ──► Accepted ──► Persisted ──► (Queued | Delivered) ──► Acknowledged
                                                       │
                                                       └── offline 队列 → 下次登录补投
```

投递语义（Delivery Semantics）正式化为协议级词汇，而不是散落在实现注释里：

| 语义 | 含义 | [现状] 对应 |
| --- | --- | --- |
| BEST_EFFORT | 尽力而为，不保证送达 | 推送桥发完即忘（app_notification 默认 logging 传输只记日志；`--push_transport http` 为真实 HTTP POST，仍无送达回执） |
| AT_LEAST_ONCE | 至少一次，重投直到确认 | 服务面注入：hub ack + Redis Streams PEL 重放 |
| AT_MOST_ONCE | 至多一次，重复会被丢弃 | 消费端 dedup 已落地（2026-10-04）：`ChatMessage.delivery_id`（补投副本由服务端铸造，ack 回队重投保原值——同一次投递的重投同 id），SDK `MemoryMessageStore` 按 `message_id` 幂等入库；服务面注入幂等键 `inject_id` |
| DURABLE | 持久化到历史面，重启不丢 | 聊天消息落 MySQL 历史（`messages` 表） |

对应关系必须写清：**`OK` 只回答 Accepted**。玩家侧是否 Delivered/Acknowledged 由补投与已读回执回答，两者都是「受理」之外的第二条链。当前「OK 仅服务面受理」一句话，升格为上述状态机在协议文档中的正式表述。

### 4.3 request_id 与 sequence 分离（Packet 已落地）

- `sequence`：连接内单调序号，用于请求-响应配对的本地排序。
- `request_id`（`Packet` 字段 4，**全链已落地，2026-10-04 收口**）：分布式关联 id，跨平面/跨服务日志追踪用。0 = 未提供——缺省生成沿全链补齐：网关 ServiceBridge 对客户端出站 2xxx 包与服务面出站包按「连接内生成」兜底（单调自增）；chat 直连入口（裸协议客户端）按进程级单调兜底；peer 面（ChatPeerHub/ChatPeerLink）出站各自连接内单调生成（hub→spoke 与 spoke→hub 双向）；SDK 发送侧（sdks/core `MakePacket`、sdks/ts 与 apps/shared/protocol `rawSend`）自动生成。显式给出的值一律不改写、沿转发链路原样透传。日志面：server_gateway 注入出站 `req=`、chat 注入入站 `inject received ... req=`。
- `message_id` 与 `delivery_id` 分离：一条消息一次发送、多次投递（离线补投、多端）各自有 id，为 dedup 与回执提供主语。**`delivery_id`（`ChatMessage` 字段 15）已落地**：空 = 首次在线投递（投递主语即 `message_id`）；离线补投副本由服务端铸造独立值；ack 超时回队重投保留原值。消费端分工——UI 幂等按 `message_id`，传输层去重按 `delivery_id`；已读回执仍是消息级（`message_id` 主语）。**ack 显式携带投递主语已落地（2026-10-04）**：`MessageAck.delivery_id` / `MessageNack.delivery_id`——服务端 `DeliveryAckManager` 按投递主语（非空 `delivery_id`，空=首投回退 `message_id`）精确匹配在途投递；旧客户端空 `delivery_id` 走兼容匹配，重投同 id 刷新不重复挂起，迟到 ack 按主语清离线副本。**离线队列 per-device 拆分已落地（2026-10-04，余项清账）**：离线桶 slot = `NormalizePlatformId(platform)`（与 SessionRegistry 的 (user, platform) 槽位同单位；空 → `default` 共享桶，键形与拆分前的 user 级队列一致，零迁移）。三臂语义——①入队臂（发送时无任何在线端/群离线成员）恒落 `default` 共享桶，任何端登录先到先得（与拆分前行为一致，投递保证不降级）；②补投臂（登录）弹本 slot 桶 ∪ `default` 桶（本槽优先），per-device fidelity 从认领开始；③ack 回队臂落 Track 记账的设备桶（live 首投记 `default`，补投记认领端 slot，重投不被其他端的登录截走），迟到 ack 清理按同 (user, slot) 精确落桶。协议零变更：slot 是纯服务端路由概念，proto/SDK 不动。明确取舍——Redis 侧不维护设备花名册（Presence 邻接面，不做）；认领端被永久弃用时其设备桶内的回队副本滞留至 TTL（历史档案同源兜底，无主副本永不滞留）。见 [服务器平面](../server_plane.md)。注入面幂等键 `inject_id`、投递跟踪 `TrackMessageResponse.tracking_id` 是同族主语。

### 4.4 Pipe 分类（[提案] 概念正式化，非新协议）

```
Client Pipe   客户端 ──► 网关 ──► chat（用户凭证）
Service Pipe  游戏服 ──► server_gateway ──► chat（服务凭证）
Peer Pipe     game_chat ⇄ app_chat、chat ⇄ chat（trusted peer，白名单 + 版本协商）
```

现状已按此隐含实现；收敛目标是让「某条连接是哪类 Pipe」成为可枚举的属性（日志、限流、凭据校验都按 Pipe 类型分流），并明确 **Gateway = Transport Edge**：网关管连接/登录/登出/心跳/踢人/限流/协议适配/转发，不承担 Chat/Social/NPC 业务路由（README「gateway 还不是业务路由层，聊天包发去 chat」已是该约束的现状表述，本文将其钉为不可演进为 God Gateway 的约定）。

### 4.5 Redis 定位

- Redis = Runtime Coordination（会话、跨实例踢人、发布订阅、Redis Streams 注入 broker、临时离线队列回退）。
- Redis ≠ Source of Truth。持久化历史/资料只落 MySQL；Redis 数据随时可重建（清空不损业务账）。

### 4.6 app_chat 演进约束

app_chat 现在是 hub-spoke 的 hub，聚合多 game 频道、身份绑定、未读。收敛约束：**app_chat 是 routing hub，不是单点逻辑库**。未来扩容按频道/游戏分片（shard），hub 层只做路由与身份映射，不成为消息存储的唯一真源（消息仍在各平面 chat 的 MySQL）。现在不做分布式 chat：单写者 chat + 水平网关 + 可靠服务面，先把基准（连接数/吞吐）做出来，再谈分片。

### 4.7 Identity binding（文档化，不扩实现面）

身份链三层：

```
platform player_id   平台账号（app_auth 签发，横跨多款游戏）
  └─ game_user_id    游戏内用户（游戏后端权威，经 5013 绑定断言）
       └─ character  角色（提案预留：现状 wire 上 game_user_id 即聊天身份，
                     角色层未建；引入时在绑定元组上加一层，不改既有键）
```

现状实现（契约细节见 [服务器平面](../server_plane.md) 「玩家身份绑定」）：

- `player_id ↔ (game_id, game_user_id)` 绑定：5013/5015/5017/5019 四个 RPC，`binding_id` 幂等键（同 id 不同元组 → `INVALID_PARAM`）；一个 `(game_id, game_user_id)` 只绑一个玩家，游戏后端是权威（重绑顶旧）。
- 跨平面回复经 `ResolveGameUser` 反查发送者游戏身份（一个玩家在同一游戏多条绑定时取字典序最小，保证确定性）。
- 凭证边界同 4.1：platform `player_id` 只出自 app 平面验证（app_auth / app_gateway 钉死），`game_user_id` 只出自游戏平面断言；注入边界只认绑定结果，不信任客户端自报的身份。

## 5. 收敛路线

P0（随本文落地，文档/协议说明为主）：

1. 本文成为后续设计的共享词汇表；新服务标题即声明自己属于哪个上层应用。
2. Delivery 语义词汇进入 `docs/api/overview.md` 投递语义节；README「OK 仅受理」表述指向本文。
3. 网关边界钉进协议文档（Pipe 分类），social/voice/search 文档同步标注「非核心」。

P1（需要 wire 扩展，独立批次）：

4. `request_id` 进 Packet（✅ 收口：协议字段 + 服务面出站生成 + 网关桥兜底 + chat 入站日志；✅ 剩余面全落地——chat 直连兜底、peer（hub/link）出站生成、SDK/客户端发送侧生成；显式值端到端透传，见 §4.3）。
5. `delivery_id` 进投递/回执协议；dedup 语义（AT_MOST_ONCE）落地（✅ 第一步：`ChatMessage.delivery_id` 字段 + 补投铸造/重投保原值 + SDK store 按 `message_id` 幂等；✅ 余项第一块：`MessageAck`/`MessageNack` 显式携带 `delivery_id`，`DeliveryAckManager` 按投递主语精确匹配；✅ 余项第二块：离线队列 per-device 拆分——三臂语义见 §4.3，协议零变更）。
6. Identity binding 文档化（platform player_id ↔ game game_user_id ↔ character），不扩实现面（open）。

P2（明确不做成核心）：

7. Presence 独立服务（现状在线状态附于 chat/Web 侧，暂不抽服务）。
8. Voice / Search 维持插件面，Voice 不进 core 抽象。

## 6. 相关文档

- [整体架构](../architecture.md)——两平面分离、hub-spoke 接入模型
- [API 总览](../api/overview.md)——投递语义与 Packet 格式
- [服务器平面](../server_plane.md)——服务面注入契约
- [NPC 对话系统设计](./npc_dialog_system.md)、[scalability 系列](./SCALABILITY.md)——各自能力的演进参考
- [能力矩阵](../CAPABILITY_MATRIX.md)——真实完成度（本文 [提案] 项一律不在其中）