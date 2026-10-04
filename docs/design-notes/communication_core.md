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
| Pipe | 客户端边缘（`game_sdk_gateway` 5200/5000、`app_sdk_gateway` 5201/5200）、服务面出站长连接（`game_server_gateway` 8100）、chat 直连入口与 trusted bridge（`--smoke-edge` 验证的 trusted pipe）、game_chat ↔ app_chat 注册 + 白名单 + 版本协商 |
| Event | 服务面注入通道（`InjectMessageNotify`）、chat 离线推送桥 → `app_notification`（6xxx）、正在输入/已读事件 |

## 4. 目标模型（[提案]）

### 4.1 ActorKind

把现有的「玩家 / NPC / SYSTEM / SERVICE」sender 语义正式收敛成一个枚举，作为 Message 与注入事件的 sender 侧身份：

```
ACTOR_PLAYER     玩家（客户端边缘验证过的用户）
ACTOR_NPC        规则/LLM 驱动的对话角色
ACTOR_SYSTEM     系统公告、状态播报
ACTOR_SERVICE    游戏服服务面身份（service_id + secret）
ACTOR_BOT        客服/审核/运营机器人（预留）
ACTOR_GM         GM（预留）
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
| BEST_EFFORT | 尽力而为，不保证送达 | 推送桥发完即忘（app_notification HTTP 投递日志 stub） |
| AT_LEAST_ONCE | 至少一次，重投直到确认 | 服务面注入：hub ack + Redis Streams PEL 重放 |
| AT_MOST_ONCE | 至多一次，重复会被丢弃 | （未实现，提案预留 dedup 位：inject_id 幂等已支持按注入 id 去重） |
| DURABLE | 持久化到历史面，重启不丢 | 聊天消息落 MySQL 历史（`messages` 表） |

对应关系必须写清：**`OK` 只回答 Accepted**。玩家侧是否 Delivered/Acknowledged 由补投与已读回执回答，两者都是「受理」之外的第二条链。当前「OK 仅服务面受理」一句话，升格为上述状态机在协议文档中的正式表述。

### 4.3 request_id 与 sequence 分离（Packet 已落地，余项 [提案]）

- `sequence`：连接内单调序号，用于请求-响应配对的本地排序。
- `request_id`（`Packet` 字段 4，已落地）：分布式关联 id，跨平面/跨服务日志追踪用。0 = 未提供，网关 ChatBridge 对客户端出站 2xxx 包与服务面出站包按「连接内生成」兜底（单调自增）；显式给出的值沿转发链路原样透传。现状：gateway.proto + server_gateway 出站生成（注入出站日志 `req=`）+ chat 注入入站日志（`inject received ... req=`）。待扩：chat 直连入口兜底、peer（hub）转发透传、SDK/客户端发送侧生成。
- `message_id` 与 `delivery_id` 分离：一条消息一次发送、多次投递（离线补投、多端）各自有 id，为 dedup 与回执提供主语。现状 `message_id` 已存在；`delivery_id` 为提案（现状参考：`TrackMessageResponse.tracking_id` 承担单次投递跟踪主语，注入面 `inject_id` 承担幂等键）。

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

4. `request_id` 进 Packet（✅ 第一步已落地：协议字段 + 服务面出站生成 + 网关桥兜底 + chat 入站日志；待扩——chat 直连兜底、peer 转发透传、SDK 发送侧生成）。
5. `delivery_id` 进投递/回执协议；dedup 语义（AT_MOST_ONCE）落地（open，见 4.3）。
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