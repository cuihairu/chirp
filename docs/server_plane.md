# Server Plane:游戏后端集成

状态:**Experimental**——枢纽(`chirp_game_server_gateway`)、chat 侧消费者、Redis Streams broker 回退都已实现并经单测验证(行覆盖 100%):chat 以内部 peer 身份拨入,注入消息走与玩家消息相同的存储/投递尾段。两平面拓扑见 [Architecture](./architecture.md)。

> **架构注记(2026-09-21)。** 目标拓扑:`game_chat` 用原生 peer 注册协议(带白名单与版本协商)注册进 `app_chat`,没有外部桥接进程。身份绑定、频道订阅和未读红点账本都由 `app_chat` 内部处理。`chirp_game_server_gateway` 保持其原有职责:游戏后端注入 + 可靠事件下行。

## 这是什么

server plane 是游戏后端与 chirp 通话的方式。它刻意与玩家边缘分开:

- **拨出(dial-out)**:游戏服务器主动开到 `chirp_game_server_gateway` 的连接。chirp 永远不需要够到游戏网络,私有子网里的游戏服务器也不需要公网回调端点。
- **服务身份,不是用户身份**:peer 用 `service_id` + 共享 secret 认证。它们从来不是用户账号,不出现在会话/踢线/在线状态里,注入消息携带非用户发送者类型(`SYSTEM` / `NPC` / `SERVICE`)。
- **同一套帧**:TCP + `[uint32_be size][chirp.gateway.Packet]`,使用 `5xxx` msg-id 段。

游戏后端不必从零复刻这份线上契约:`sdks/go`(包 `chirp`,见其 README)是参考 Go 客户端——认证握手、服务端指定心跳、sequence 关联的 inject/event RPC、至少一次事件 ack、失败挂起重连——与仓内 C++ peer(`libs/network/server_gateway_peer.cc`)语义一致。

### 凭证边界

`service_id` + secret 这一对是 appkey/appSecret 式凭证:它标识接入的后端,长期有效。由此有两条铁律:

- **永远不要带进客户端。** 凡打进游戏客户端或应用二进制的东西,等同于公开。客户端持有的是短时效用户 token;游戏后端在玩家自身登录之后兑换/派生这些 token。见 [Credential model](./architecture.md#credential-model-service-credentials-vs-user-tokens)。
- **永远不要用它冒充用户。** 注入携带 `sender_kind`(`SYSTEM` / `NPC` / `SERVICE`)正是为了 server plane 可以行动而不假装成某个玩家账号。

## 运行

```bash
cmake --preset dev && cmake --build --preset dev
./build/services/game/server_gateway/chirp_game_server_gateway \
  --port 8100 \
  --service game=game-secret \
  --service chat=chat-secret \
  --chat_service_id chat \
  --heartbeat_interval 30 \
  --auth_timeout 10 \
  --max_pending 1000
```

| 参数 | 默认 | 含义 |
| --- | --- | --- |
| `--port` | 8100 | 服务连接的 TCP 监听 |
| `--service` | (无) | 可重复的 `service_id=secret` 凭证条目 |
| `--chat_service_id` | `chat` | 接收消息注入的服务 |
| `--heartbeat_interval` | 30 | 下发的保活节奏(秒) |
| `--auth_timeout` | 10 | 认证时限,超时断开 |
| `--max_pending` | 1000 | 每服务排队(未 ack)事件上限 |

没有 `--service` 条目时枢纽照常启动但拒绝一切登录(fail closed)。

## 连接生命周期

1. 游戏服务器拨 TCP,必须在 `--auth_timeout` 秒内发出 `SERVER_AUTH_REQ`,否则连接被关闭。
2. `SERVER_AUTH_RESP` 返回结果、服务器时间和下发的 `heartbeat_interval_seconds`。连接每 `2 × heartbeat_interval` 秒内至少要有一次流量,否则被关闭。
3. 同一 `service_id` 从另一条连接再次登录会顶掉旧连接;被顶的连接被关闭。

## 上行:消息注入

可信服务请求 chirp 投递一条发送者不是用户的消息(公告、NPC 台词、交易状态)。`INJECT_MESSAGE_REQ` 携带 `chirp.server_gateway.MessageInjectRequest`:

- `inject_id`:调用方提供的幂等键(响应中原样回显)
- `sender_kind`:`SENDER_SYSTEM` / `SENDER_NPC` / `SENDER_SERVICE`
- `sender_id`:如 `npc:blacksmith_01`、`trade`
- `channel_type` + `channel_id`,或一对一场景的 `receiver_id`
- `content`
- `game_id`(可选):把注入切到扇入投递——见下一节

响应码:

| 码 | 含义 |
| --- | --- |
| `OK` | 校验通过并已交给 chat 服务(`InjectMessageNotify`);扇入场景指至少交付给一个订阅者(见下) |
| `INVALID_PARAM` | 空 content / `SENDER_UNKNOWN` / 空 `sender_id` / 无频道也无接收者 / `game_id` 与 `PRIVATE` 频道或空 `channel_id` 组合 |
| `RATE_LIMITED` | 仅扇入:频道订阅者数超过 `--max_fanout`(默认 10000);在发出任何副本之前整体拒绝 |
| `SERVER_UNAVAILABLE` | chat 服务未连接,或写失败 |

`OK` 的含义仍是"被本平面接受":chat 侧异步消费注入,响应不确认玩家已收到。

### 扇入投递(WP-8 切片 3)

带 `game_id` 的注入不会被原样转发。枢纽在订阅注册表里查 `(game_id, channel_id)` 的全部订阅(见下),给**每个订阅者投一份私聊副本**:`channel_type` 变为 `PRIVATE`,`receiver_id` = 订阅者的 `player_id`,`sender_kind` 变为 `SENDER_SERVICE`,原始 `sender_id` 和 `content` 保留——副本如何聚成聊天历史线程由后端说了算(每个来源固定用一个系统身份,同一频道的副本就能落进同一线程)。每份副本的 `inject_id` 派生为 `<original>#<player_id>`,仅用于日志关联;枢纽不做基于它的去重。订阅者遍历顺序未定义。

从那之后投递由 chat 服务负责——在线推送、离线队列、基于 ack 的重投——所以扇入不给 chat 添任何逻辑。各种结局:

- **无订阅者**:不联系 chat 直接 `OK`。没人关注的频道是语义上的空操作;回 `SERVER_UNAVAILABLE` 会让流式 broker 永远重放这条记录。
- **chat 离线,或每份副本都失败**:`SERVER_UNAVAILABLE`,什么都没存——重放是安全的。流式 broker 里这种状态的条目保持 pending 并被重投(见"Broker 回退")。
- **部分成功**:`OK`。重试会把已交付的副本翻倍。
- **订阅者超过 `--max_fanout`**:`RATE_LIMITED`,在发出任何副本之前拒绝。broker 把任何非 `SERVER_UNAVAILABLE` 的应答当终态并 ack 该条目;长连接调用方应当拆分频道或调高上限。

混合部署(新枢纽 + 旧 chat)是安全的:副本把 `game_id` 带过去,旧 chat 二进制会把它当未知 proto3 字段丢掉。

每份成功交给 chat 服务的副本,还会在枢纽的未读账本里给接收者按 `(game_id, channel_id)` 加一(见下文"统一未读")。部分失败只给已交付的副本计数;被拒和不可用的扇出不计数。

### chat 侧消费

chat 服务以内部 peer 身份连到枢纽(`--server_gateway_host`,默认空即关闭),应答认证和心跳。转发来的 `InjectMessageNotify` 走与 `SEND_MESSAGE` 相同的尾段:

- 先存历史;没有成员校验,也没有提及冷却(发送者不是用户)。
- `PRIVATE` 且接收者在线立即投递,否则为离线用户排队。
- 非私聊频道(`TEAM` / `GUILD` / `WORLD`)广播给成员并为离线成员排队。
- 格式损坏的 `InjectMessageNotify` 记日志后跳过;连接保持。

### Broker 回退(上游走 Redis Streams)

对无法承载长连接客户端的游戏后端,同一注入路径也可以走 Redis Stream。以 `--broker_redis_host` 启动枢纽(空,即默认,禁用消费者):

| 参数 | 默认 | 含义 |
| --- | --- | --- |
| `--broker_redis_host` | (空) | Redis 主机;空禁用 broker |
| `--broker_redis_port` | 6379 | Redis 端口 |
| `--broker_stream` | `chirp:server_plane:inject` | 要消费的 Stream |
| `--broker_group` | `chirp-plane` | 消费者组(幂等创建) |
| `--broker_consumer` | `<hostname>:<pid>` | 组内消费者名 |
| `--broker_claim_min_idle_ms` | 30000 | `XAUTOCLAIM` 的 min-idle-time(重投用) |

要求 Redis >= 6.2(`XAUTOCLAIM`)。生产方每条消息写一个条目,扁平字符串字段——任何能 `XADD` 的语言都能接入:

| 字段 | 必填 | 含义 |
| --- | --- | --- |
| `service_id` | 是 | 服务身份(必须存在于 `--service`) |
| `secret` | 是 | 共享 secret,与连接面同一套凭证 |
| `sender_kind` | 是 | `SYSTEM` / `NPC` / `SERVICE`(容忍 `SENDER_` 前缀) |
| `channel_type` | 是 | `PRIVATE` / `TEAM` / `GUILD` / `WORLD`,或 `0`–`3` |
| `sender_id` | 是* | 与 proto 路径一样在下游校验 |
| `channel_id` / `receiver_id` | — | 频道目标,或一对一的接收者 |
| `game_id` | 否 | 扇入投递:设置后把条目扇出给每个 (game_id, channel_id) 订阅者各一份私聊副本 |
| `content` | 是* | 消息体 |
| `inject_id` | 否 | 幂等键;缺省时生成 `<consumer>-<seq>` |
| `reply_to` | 否 | 接收 `{inject_id, code}` 结果条目的 Stream 名 |

语义:

- 枢纽跑一个消费者组(`XREADGROUP ... BLOCK`),把每个条目送进与长连接面相同的 `HandleInject` 路径,校验与响应码完全一致。
- `OK` / `INVALID_PARAM` / `AUTH_FAILED` **立即 ack**(`XACK`):畸形或被拒的条目是毒丸,不能重放。
- `SERVER_UNAVAILABLE`(chat 服务离线)**不 ack**:条目留在 pending entries list 里,由周期性 `XAUTOCLAIM` 清扫重投,直到 chat 恢复。重投在设计上不设上限——毒丸已被上面的立即 ack 规则挡住。
- 带 `reply_to` 时,结果(`inject_id` + `ErrorCode` 名,如 `OK`)只对 **已 ack 的终态** 回写,因此一条重放的条目恰好应答一次。
- 命令间传输失败会断开重连;未 ack 的条目重放。整体至少一次。

## 下行:事件(至少一次)

`EVENT_PUBLISH_REQ`(`chirp.server_gateway.EventPublishRequest`)发布一条必须到达目标服务的事件——例如 chat 侧逻辑产生的任务触发器:

- `event_id`:可选的调用方幂等键;缺省时生成(`evt-<ts>-<n>`)并在响应中回显
- `target_service_id`, `event_type`, `payload`

投递语义:

- 目标**在线** → 立即以 `EVENT_DELIVER_NOTIFY` 投递(`queued=false`)。
- 目标**离线** → 按服务排队(`queued=true`),在其下次登录/重连时投递。
- 事件一直排队,直到 `EVENT_ACK_REQ` 确认。重连会重投全部未确认事件;每投一次 `attempt` 加一。
- 队列满(超过 `--max_pending`)时,发布以 `SERVER_UNAVAILABLE` 拒绝,而不是悄悄丢掉旧事件。发布方带退避重试。
- ack 幂等;未知 id 回 `OK`。
- 被顶掉的旧连接晚关不会重置存活连接的在途跟踪(不会有重复重投风暴)。

## NPC 对话服务

`services/npc_dialog` 是这个平面上的第一个事件消费者:一个纯 server plane 客户端(无玩家侧监听),把 NPC 对话闭环端到端打通。

- **上行(chat → hub → npc_dialog)。** chat 把玩家发给 `npc:` 前缀接收者的私聊改写成 `npc.player_message` 类型的 `EventPublishRequest`(payload:`chirp.chat.NpcPlayerUtterance`;`event_id` = chat 消息 id)并发完即忘——玩家的 `OK` 表示已受理,不代表会有回复。聊天历史里保留玩家的原话。
- **回复(npc_dialog → hub → chat)。** responder 用关键词规则表(`npc_id<TAB>keyword<TAB>reply` 的 TSV,`*` = 该 NPC 的兜底台词,ASCII 大小写不敏感子串匹配,先命中先用;不给 `--rules_file` 时内置演示规则)生成回复,以 `SENDER_NPC` 走私聊频道注回玩家,`event_id` 作为 `inject_id` 幂等键。
- **ack 策略。** 陌生事件类型、解析失败的 payload、缺发送者/NPC 身份的语句都立即 ack(毒丸:重试永远不可能变有效)。有效事件只在枢纽接受其回复注入(`OK`)**之后**才 ack;其他任何结局都保持未 ack,枢纽因此重投——至少一次。**重投去重(2026-09):**回复一旦被接受,事件 id 会记进一个有界的最近窗口(1024 条);重投的已应答事件直接 ack 不再注入;注入未成功的事件绝不入窗,其重投会真正重试。在第一次尝试出结果前赶到的重复投递不会被抑制(枢纽串行投递,这只是理论边界)。

进程级验证:`./test_services.sh --smoke-npc` 以真实进程跑枢纽 + chat + npc_dialog,检查关键词回复、兜底回复、历史(玩家原话 + 回复)和离线队列补投路径。

## 玩家身份绑定(WP-8 切片 1)

聚合面需要一个横跨多款游戏的平台级 `player_id`;server plane 就是游戏后端断言这些绑定的地方。`chirp_game_server_gateway` 在四个 RPC 后面维护一个 `IdentityRegistry`(`identity_registry.{h,cc}`):

- `BIND_PLAYER_IDENTITY_REQ`(5013)——`binding_id`(调用方选定的幂等键)、`player_id`、`game_id`、`game_user_id`。同 id + 同元组再来一次 → `OK` 且 `existed=true`;同 id + 不同元组 → `INVALID_PARAM`(复用键会悄悄破坏重复检测)。一个 `(game_id, game_user_id)` 对只能绑到一个玩家:用新 `binding_id` 再断言会顶掉旧绑定(换号/解绑重绑——游戏后端是权威)。
- `UNBIND_PLAYER_IDENTITY_REQ`(5015)——按 `binding_id` **或**按完整 `(game_id, game_user_id)` 对,二者不可同时给也不可都不给(否则 `INVALID_PARAM`)。目标不存在 → `OK`(幂等)。
- `GET_PLAYER_IDENTITIES_REQ`(5017)——某 `player_id` 的全部绑定。
- `RESOLVE_GAME_USER_REQ`(5019)——`(game_id, game_user_id)` → `player_id`(未绑定时 `OK` 且 `player_id` 为空)。

存储为内存权威 + 直写 Redis 镜像(`chirp:binding:entry:<binding_id>` = 序列化的 `StoredIdentityBinding`,`--binding_redis_host`/`--binding_redis_port`,默认关)。启动时重放全部存量条目;损坏记录跳过并告警。Redis 写失败是尽力而为——内存保持权威,同一记录下次变更会重试写——所以 Redis 故障降级为纯内存语义,而不是报错。

这个注册表是两种聚合面设计共同需要的地基(带游戏命名空间的共享多租户核心,或联邦桥);扇入投递与统一未读(都已上线,见上下文)就建在它上面。

## 玩家频道订阅(WP-8 切片 2)

绑定回答"这个游戏用户是哪个平台玩家",订阅回答"这个玩家想要哪些游戏频道"。`chirp_game_server_gateway` 在三个 RPC 后面维护一个 `SubscriptionRegistry`(`subscription_registry.{h,cc}`)。从 WP-8 切片 3 起,注册表还支撑扇入投递:inject 处理器读它的 (game_id, channel_id) 反向索引(见上文"扇入投递")——注册表本身仍只存意图,投递决策住在 inject 路径里。

- `SUBSCRIBE_PLAYER_CHANNEL_REQ`(5021)——`player_id`、`game_id`、`channel_id`,外加可选 `subscription_id`。带 id 时,它是调用方的幂等键:同 id + 同元组再来一次 → `OK` 且 `existed=true`;同 id + 不同元组 → `INVALID_PARAM`(复用键会悄悄破坏重复检测)。`(player_id, game_id, channel_id)` 三元组全局唯一:用新 id 订阅同一元组会顶掉旧记录——断言方是权威(比如游戏改版重排频道)。`subscription_id` 为**空**时由枢纽铸造一个(`sub-...`):这是玩家自服务路径,app_gateway 转发前把 `player_id` 钉到已认证用户,同元组重复订阅会收敛到存量记录(id 稳定、`existed=true`),而不是堆行。
- `UNSUBSCRIBE_PLAYER_CHANNEL_REQ`(5023)——按 `subscription_id` **或**按完整 `(player_id, game_id, channel_id)` 三元组,不可同时给也不可都不给(否则 `INVALID_PARAM`)。目标不存在 → `OK`(幂等)。
- `GET_PLAYER_SUBSCRIPTIONS_REQ`(5025)——某 `player_id` 的全部订阅,可带 `game_id` 过滤("我在游戏 X 的订阅")。

同一组六个消息 id 服务两类调用方:游戏后端直连 server plane,玩家经 app_gateway 转发到达同一批处理器——应用边缘把 `player_id` 钉死,客户端永远只能为自己创建、列举或删除订阅。

存储与绑定镜像:内存权威 + 直写 Redis 镜像(`chirp:subscription:entry:<subscription_id>` = 序列化的 `StoredChannelSubscription`,`--subscription_redis_host`/`--subscription_redis_port`,默认关),启动重放并跳过损坏记录,尽力而为写、Redis 故障降级纯内存。

遗留问题(扇入上线时重新审视过,仍然保留):订阅不校验既有身份绑定——通过自服务,玩家可以订阅一款自己从没玩过的游戏的频道。扇入投递按宽松选择上线(任何地方都不要求绑定;后端断言保持可信),枢纽的扇出因此没有跨注册表依赖。自服务路径是否终究该要求绑定,仍是待定的产品决策。

## 统一未读(WP-8 切片 4)

枢纽维护一个 `UnreadLedger`(`unread_ledger.{h,cc}`):按玩家、按 `(game_id, channel_id)` 的红点计数,统计**未处理的扇入通知**——每份成功交给 chat 服务的扇出副本加一(见上文"扇入投递")。这是红点,不是已读游标:它看不见 chat 服务的已读状态(gateway 2201-2207),两者互不供数。退订也不清红点——标记已读是唯一的递减路径,投递失败永不回滚。计数器是 `int32`。

两个 RPC(游戏后端直连;玩家经 app_gateway 转发,`player_id` 钉到已认证用户):

- `MARK_CHANNELS_READ_REQ`(5027)——分层选择器:给了 `channel_id`(要求 `game_id`)清那一个频道;只给 `game_id` 清该游戏的全部频道;都空则清玩家的全部。幂等:目标不存在回 `OK` 且 `cleared = 0`。
- `GET_UNREAD_SUMMARY_REQ`(5029)——每个非零计数一条 `UnreadSummaryEntry`(`game_id`、`channel_id`、`unread_count`),按 `(game_id, channel_id)` 排序,外加 `total_unread`——可选 `game_id` 过滤后的总和("我在游戏 X 的未读")。

存储与各注册表镜像:内存权威 + 直写 Redis 镜像(`chirp:unread:entry:<player_id>:<game_id>:<channel_id>` = 序列化的 `StoredUnreadEntry`,`--unread_redis_host`/`--unread_redis_port`,默认关),启动重放并跳过损坏记录与零计数,尽力而为写、Redis 故障降级纯内存。清掉的条目从镜像里删除而不是存零,计数器因此不会复活或累加。键的组成部分是原样拼接:含 `:` 的 id 在磁盘上可能别名成另一条目的键——内存 map 保存精确元组,所以只影响奇异 id 的重启保真度。

## 路线图

1. ~~chat 服务以内部 peer 连入并消费 `InjectMessageNotify`~~——完成(回环端到端验证);进程级 E2E 冒烟留作后续选项。
2. ~~为无法承载长连接客户端的集成方提供 Redis Streams 回退 broker(ack + 重放,不用裸 pub/sub)~~——完成,仅上游注入(见上文"Broker 回退");下行事件仍走长连接面。
3. ~~chat 侧的事件生产~~——NPC 对话部分完成(`npc.player_message`,由 `services/npc_dialog` 消费,见上);敏感词处罚与交易状态流转仍开放。
