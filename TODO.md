# Chirp 任务清单

> 最后更新：2026-09-27：游戏在线状态 + 好友消息进游戏——身份绑定即"在游戏内"断言，绑定默认开启上报，可经自服务开关（5031-5034）关闭；关闭后不推状态、游戏内不投递。多端在线（顶号键 (user_id, platform) + 在线设备清单 + SDK/伴侣 UI，见 game_sdk_gateway 条目）同批落地。
>
> 2026-09-27：撤回墓碑贯通历史存档收口——墓碑改为「置位 + 抹除正文」并在三种存储实现里落地（MySQL 列 / Hybrid 双 tier / 基础形态内存+Redis 镜像，收敛为新单元 `recall_tombstone`），`BULK_DELETE` 软删补上立碑与离线回收，`GET_HISTORY` 从此读不出原文；覆盖率保持 100.0%。
>
> 2026-09-25：游戏平面 P0 收官——消息撤回（窗口/频道可配 + 六态回码 + 墓碑广播 + 离线副本回收 + SDK `RecallMessage`），并记账撤回墓碑贯通历史存档的剩余缺口。
>
> 2026-09-25 接入方支持批次（已提交）：避坑指南/API 总览重写/C++ 接入示例/压测工具/TS 幽灵依赖修复/smoke-sdk 竞态修复/测试环境鲁棒性加固。

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
- [x] **多端在线策略**（2026-09-27：顶号键 (user_id, platform)，跨端共存 + 在线设备清单。① 会话槽位键从 user 改为 **(user_id, platform)**（`session_registry` 重写）：同 platform 新登录顶掉旧会话（KICK_NOTIFY reason 走 `device_presence::LoginKickReason`："logged in on another <platform>"，legacy 空 platform 回退 "device" 文案，归一化的 "default" 不出现在玩家可见文案里）；跨 platform 共存互不干扰；同 device_id 断线重连的幂等重绑语义保留（同 device 必然同 platform，落同一槽位）；device_id 降为每会话元数据（在线清单上报用）。空 platform 归一 "default"——不上报 platform 的 legacy 客户端仍保持每用户一会话的历史行为。**web 多标签同型互顶是该语义的直接后果，不另开特例**（见 CAPABILITY_MATRIX）。Redis 跨实例 claim 同步迁到 platform 键（`chirp:sess:<user>\x1F<platform>`，kick payload user\x1Fplatform，`AsyncClaim/AsyncRelease` 带 platform）；voice 平面刻意不动（自身设计即每用户一会话）。② 任意端可见"其他哪些类型的端在线"：登录响应新增 `online_devices` 初始清单（`FillOnlineDevices`，排除刚登录会话自身）；登录/断开/被顶时向该用户其他在线会话广播 `DEVICES_PRESENCE_NOTIFY`(1020, seq 0，风格对齐 KICK_NOTIFY)，payload `[{platform, device_id, online, ts}]`（`BroadcastDevicePresence`，registry 本地广播——多节点部署下其他实例听不到，与既有本地 notify 同边界）。**断开广播只在 `RemoveAuthenticatedSession` 确认释放槽位时发**：被顶的旧会话迟到断开不产生假 offline 事件（测试锁定）。③ SDK 三层接入：Go `SetDevicePresenceHandler` + dispatch 分支；TS `@chirp/protocol` ChatEventListener 新增 `onDevicesPresence`/`onLoginDevices` 两 hook（ChatPipeline 注册 notify + 登录响应扇出）；Flutter ChirpClient/ChatPipeline 对齐（hooks.dart 两个空实现 hook，pipeline 登录扇出 + notify 派发）。web 伴侣新增在线设备 Dialog（侧栏入口带在线计数）+ `online_devices_store`；mobile 伴侣「我的」页新增在线设备清单区（同款 store）。④ party/social 平面：顶号键与 kick reason 同步切换，但**不广播设备清单**——设备在线清单属于客户端接入面（两个 sdk_gateway 与 chat 直连入口）。测试：`session_registry_tests` 重写为 platform 语义 24 例（NormalizePlatformId、跨型共存、被顶迟到断开不广播、`ListOnlineDevices`/`FillOnlineDevices`/`BroadcastDevicePresence` 含 exclude 语义）、`chat_enhanced_session_tests` 跨 platform 共存/上线 announce/离线 announce 3 例、Go dispatch 1 例、web store+组件 7 例、mobile store+api 3 例；`--smoke-redis` 冒烟场景按新语义重写（同型异设备互踢 + 跨型跨实例共存）；proto 变更走 `make proto`（gen_proto.sh），dart gencode 钉 protoc_plugin 21.1.2）

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
- [x] **消息撤回**（2026-09-25：game_chat_features P0 收官项——`DELETE_MESSAGE`(`is_hard_delete=false`)对发送者本人即撤回：`MessageEditManager::RecallMessage` 返回 `RecallStatus` 六态（已撤回/无台账/非发送者/非撤回频道/超窗/已撤回过），handler 映射回码 `OK`/`USER_NOT_FOUND`/`AUTH_FAILED`/`INVALID_PARAM`；窗口 `--recall_window_sec` 默认 120s（0=不限），可撤回频道 `--recall_channels` 默认 `private,guild`（空=全频道不可撤回，fail-closed），频道名单解析复用新增的 `chat_validation::ParseChannelTypeList`/`ChannelTypeInList`（trim+大小写归一，未知 token 丢弃=收窄而非放宽）；版主删除走 `DeleteMessage` 治理路径不受窗口/频道约束，`is_hard_delete` 仍限版主；`CanRecall` 给客户端同规则的预检；撤回成功向频道成员广播 `MESSAGE_DELETED_NOTIFY`（`deleted_by=作者` 供客户端渲染"消息已撤回"墓碑），重复撤回不二次广播；SDK 新增 `RecallMessage(id, cb)`；`chat_extensions_tests` 6 例 + `chat_edit_mention` 4 例 + `chat_validation` 2 例 + `sdk_core` 2 例）
- [x] **撤回回收离线队列副本**（撤回批次同日补齐：删除/撤回成功后按 `message_id` 摘掉接收方离线队列里已入队的副本——离线接收方没有 live 会话收不到 delete notify，副本留着就会在下次登录把原文补投出去，撤回等于白做。新增注入式 `OfflineMessagePurger`（`MessageEditHandlers` 第 5 个构造参数，缺省 no-op，部署形态可不给）+ basic 形态 `MessageStore::PurgeOfflineByMessageId`（内存队列 remove_if；Redis 队列无 message_id 索引，扫队列匹配后 `LRem`，队列有 200 条/用户上限故代价有界），成员列表复用广播用的 `members_` 解析（私聊=对方、群=除请求者外全员）；`chat_edit_mention` 新增 4 例）
- [x] **撤回墓碑贯通历史存档**（2026-09-27 收口：只置位不抹正文的墓碑等于原文仍可读，本批把墓碑做成「置位 + 抹除正文」。① 语义落在存储侧：`MySQLMessageStore::MarkMessageRecalled` → `UPDATE messages SET is_recalled = 1, content = ''`；`HybridMessageStore` 双 tier 同步抹除（Redis 热层 `LSet` 原位改写、列表序与其余条目不动，冷层委托上述 UPDATE）；② 基础形态的历史向量 + Redis 镜像改写逻辑抽为新单元 `services/shared/chat/src/recall_tombstone.{h,cc}`（`MarkRecalledInMemory`/`MarkRecalledInRedisList`：跳过不可解析条目、已置位不重复回写、幂等），main.cc 与 Hybrid 共用同一份；③ `BULK_DELETE` 的软删此前既不立碑也不回收离线副本（等于批量入口删掉的历史照样能读出原文），本批与单体软删对齐：成功的每条置位抹正文、每个成员回收副本，失败条目不动，marker/purger 仍可缺省不接；④ `GET_HISTORY` 不需要新改动即不再返回原文——basic 整 proto 回拷、enhanced 逐字段带 `is_recalled`（`main_enhanced.cc:499`）、`PaginatedHistoryRetriever` 透传 `MessageData.is_recalled`，写侧抹干净读侧自然正确。非交互自行判定并记账的假设：(a) 本批**协议零改动**——`ChatMessage.is_recalled`(field 14) 与各语言 gencode（含 TS `isRecalled`）已在 0b80b5f 落地，任务书所说「需要的 proto 字段同步补」实际无需补；(b) 采「标记时抹除」而非「读时过滤」，不保留取证副本，免得将来任何一条新读路径忘记过滤而泄露；(c) 已置位但正文未抹的旧行不做回填脚本（本仓库无生产数据，且抹除幂等）；(d) 离线队列那一半按任务书要求未重做。测试按 store 逐一配套：新 `chat_recall_tombstone_test.cc`（内存向量置位/抹除/幂等/未命中 + Redis 镜像原位改写/坏条目跳过/写失败回报）、`chat_mysql_tests` 增补 UPDATE 含 `content = ''`、双 tier 抹除断言（命中的热层条目正文清空、未命中条目原文保留）、新例 `RecallTombstoneSweepsBodyOutOfHistoryReadback` 断言 `GetHistory` 读回不再带原文（置位幂等与旧 schema 容忍沿用 0b80b5f 既有用例）；`chat_edit_mention` 增补批量墓碑与逐成员回收、null 接线容忍、以及 marker 绑真实墓碑原语的端到端「撤回→历史只剩墓碑」语义）
- [x] **enhanced 形态接入撤回端点**（2026-09-27 收口：游戏平面实际部署形态补上撤回入口。① `DistributedDispatchHandlers` 新增第 11 个槽位 `on_delete_message`，dispatch 分支与 basic 同契约——body 解析失败也回 `INVALID_PARAM`（撤回请求不许悬着），未接 handler 时同样只回码不崩；② enhanced 装 `MessageEditManager`/`MessageEditHandlers`（`MakeEnhancedRecallRuntime` 工厂，测试经 `#include main_enhanced.cc` 直驱同一装配）：`--recall_window_sec` 默认 120（0=不限）、`--recall_channels` 默认 `private,guild`（`chat_validation::ParseChannelTypeList` 解析，空=fail-closed）；每次发送后 `TrackMessage`/`RegisterMessage` 记台账，撤回据此查 sender/channel；notify 回调经 session registry `GetUserSessions` 扇出到对方**每个**在线设备；marker 接 `HybridMessageStore::MarkMessageRecalled`（双 tier 墓碑）、purger 接 `PurgeOfflineByMessageId`（Redis 队列 + Redis-down 内存回退都扫）；③ gateway 确认零改动：sdk_gateway 对已认证连接原样转发 2001-2999（2227 在列），ChatBridge 是 verbatim 管道，`MESSAGE_DELETED_NOTIFY`(2232) 可达客户端。非交互自行判定并记账的假设：(a) **成员解析只覆盖私聊**——游戏平面没有群组订阅登记，从规范 `a|b` 键解析对方即可；非私聊（guild）的成员解析返回空，撤回本身/墓碑/发送者校验都成立，但 notify 扇出与离线副本回收对空成员列表是 no-op（enhanced 的群聊扇出本就暂缺订阅端，与频道屏蔽同缺口，随该路径一起落地）；(b) **版主恒判 false**（fail-closed）——enhanced 无角色体系，`is_hard_delete=true` 一律回 `AUTH_FAILED`；(c) 超窗/重复撤回的时序语义在 manager 层用例已锁，dispatch 层不做依赖真实时间流逝的抖动用例。顺带修掉一个暴露出来的头卫生问题：`read_receipt_manager.h` 与 `message_store.h` 各有一份字段同名、成员顺序不同的 `ReadReceiptData`，任何同时包含两个头的 TU 直接重定义编译失败——收敛为只留 `message_store.h` 一份。测试：`chat_enhanced_session_tests` 新增 4 例（撤回成功 notify 到达每设备+历史只剩墓碑、离线副本回收、非发送者/未知消息/世界频道/硬删回码、dispatch 路由+垃圾 body+未接 handler），fixture 为墓碑断言自带 InMemoryRedis 活历史层；`chat_mysql_tests` 新增 `PurgeOfflineByMessageId` 存储层用例（Redis 命中/未命中/空参/回退队列末条删除后映射回收））
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
- [x] **游戏在线状态自服务开关**（2026-09-27：`SET_GAME_PRESENCE_ENABLED_REQ/RESP`（5031/5032）与 `GET_GAME_PRESENCE_REQ/RESP`（5033/5034）经与 WP-8 同一条自服务链应答——`DispatchPlayerDirectoryPacket` `SERVER_AUTH_REQ` 信任门 + `ForwardSubscriptionPacket` 把 `player_id` 钉死为登录身份（伪造 player_id 不落地，测试扫描 hub 全量上行帧验证无泄漏）；开启=删覆盖行、关闭=写覆盖行并停广播/清 roster（语义见 app_chat 条目）。客户端三层：Go `SetGamePresenceEnabled`/`GetGamePresence`（dispatch 白名单补两 RESP，往返测试覆盖默认开启读数）；TS `msg_map` 导出两 spec；Flutter `msg_map` 对齐。web 伴侣：`game_presence_store`/`game_presence_api`（与设备面共用 app_gateway socket）+ 在线设备 Dialog 内开关区（绑定前默认开启显示"未绑定"文案、关闭即本地清空游戏清单、读写失败标 unavailable 不再可点）；mobile 伴侣：「我的」页 SwitchListTile（副标题随状态切换：未绑定提示/当前生效游戏/已关闭文案，snack 反馈，unavailable 禁用））

### app_chat（原 chat，部署为 App 平面 hub）

- [x] 基础聊天能力（与 game_chat 同一二进制）
- [x] **hub 模式**：接受 game_chat 的 `PEER_REGISTER_REQ`，白名单 + 版本协商（2026-09-22：9edaca3 将两种构建形态统一接线 libs 层 `ChatPeerHub`——`--hub_mode`/`--hub_peer_port`（默认 8200）独立监听注册面，`--allowed_peers` 白名单 + `--min_peer_version`/`VERSION_MISMATCH` 拒绝，同 id 顶替、心跳 idle 踢出、断线重连；`chat_peer_test` 36 例含真实 link↔hub 端到端）
- [x] **身份映射**（2026-09-22：`PlayerDirectory` + `IdentityRegistry` 落地 app_chat，`BIND/UNBIND/GET/RESOLVE` 四 RPC 经 `SERVER_AUTH_REQ` 信任门在 chat 主端口应答，(game_id, game_user_id) 唯一索引 replace-on-reassert，可选 Redis 镜像跨重启）
- [x] **频道订阅**（2026-09-22：`SubscriptionRegistry` 落地，SUBSCRIBE/UNSUBSCRIBE/GET 三 RPC，(player, game, channel) 三元组唯一索引 + (game, channel) 反向扇入索引；后端断言带幂等键，自服务空 id 由服务端铸 `sub-` id 且收敛稳定）
- [x] **跨平面 fan-out**（2026-09-22：`PlayerDirectory::FanoutChannelMessage` 在 hub 侧承接 spoke 的 `CHANNEL_MESSAGE_NOTIFY` 上行，每订阅者一份私信副本交接 + 未读自增；空订阅语义 no-op、超 `--max_fanout_per_message` 整条丢弃告警）
- [x] **跨平面回复**（2026-09-22：hub 模式拦截带 `<game_id>:<bare>` 前缀的非私聊发送——`PlayerDirectory::RelayGameReply` 编排，`ChatPeerHub::service_id_for_game` 反查在线 spoke + `IdentityRegistry::ResolveGameUser` 反查游戏身份（同游戏多绑定取字典序最小保证确定性），`PEER_INJECT_MESSAGE_NOTIFY` 注入 spoke（裸频道 ID、游戏侧铸 message_id、走存储/离线队列同一尾段）；回码 OK / SERVER_UNAVAILABLE（无在线 spoke 或下行失败）/ INVALID_PARAM（发送者无该游戏绑定），拒绝显式回码不降级本地频道；basic/enhanced 两形态接线，拦截点在发送限流之后（消耗发送预算）、mention 处理之前（内容透传游戏侧）；无回环——扇回 App 玩家的是无前缀私聊副本，不再触发本路径）
- [x] **未读计数**（2026-09-22：`UnreadLedger` 落地，fan-out 每份被接受副本自增 badge；`MARK_CHANNELS_READ` 分层选择器（单频道/单游戏/全部）幂等清除，`GET_UNREAD_SUMMARY` 按 (game, channel) 排序含过滤与 total_unread）
- [x] **游戏在线状态 + 好友消息进游戏**（2026-09-27：身份绑定即"在游戏内"断言，绑定默认开启上报，开关见 app_sdk_gateway 条目。① 状态推导与广播：`GamePresence`（`services/shared/chat/src/game_presence.*`）只存**显式关闭**的覆盖行（缺行=开启，"绑定即默认开启"），键 `chirp:game_presence:setting:<player_id>`；`PlayerDirectory::RefreshPresence` 在绑定增删后重算 desired = 开启 ? 排序绑定 game_ids : 空，与 roster 键 `chirp:game_presence:online:<player_id>`（换行连接排序 game_ids，空则 DEL）比对，翻转才发布 `GamePresenceEvent{player_id, game_id, online}` 到 Redis pub/sub `chirp:game_presence:events`——Redis 是传输介质不是权威，进程重启从身份绑定全量重建（LoadAll 先恢复开关再服务，避免用空 roster 覆盖真相）。**关闭态完全静默**：无事件、无 roster（回归用例锁定）；game 断言随 spoke 断开/超时失效，状态自然下线，无独立心跳。② 好友私聊进游戏：`PlayerDirectory::RelayFriendMessage` best-effort 镜像——接收方开关开且有绑定时每绑定按 game_id 排序注入一份 `PEER_INJECT_MESSAGE_NOTIFY`（channel_id=接收方该游戏 `game_user_id`，sender_id=chirp 好友 `user_id`），常规端照常收；spoke 不在线/解析失败/注入拒绝逐项跳过不回码（镜像语义：聊天主链路不受影响，无回环——游戏内回复走既有 `RelayGameReply` 前缀路径）。③ social 平面叠加：`chirp_social` 订阅 `chirp:game_presence:events`（`ConsumeGamePresenceEvent` 事实化便于测试），好友 `EffectivePresence` 游戏叠加非空 → IN_GAME（status_message=逗号连接 game_ids，metadata[game_id]="1"）；叠加活过 social 登出，SET_PRESENCE AWAY 不覆盖 IN_GAME，断线（游戏断言失效）补发 offline 给好友。测试：`chat_player_directory_tests` 55→96 例（presence 边界改 5035、GamePresenceTest 5、PlayerDirectoryPresenceTest 11 含重启重建与 Redis 故障降级、RelayHarness 好友镜像 6、GetById 哨兵/缺失 1）、`social_tests` 28→37 例（断言呈现/回落/登出存活/多游戏进出/事件广播合并视图/断线通知/畸形事件忽略/SET_PRESENCE 不覆盖 IN_GAME）、`app_sdk_gateway_tests` 29→30 例、Go 往返 1 例、web store 4+api 6+Dialog 10、mobile store 4+api 8+widget 5）
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

## 接入方支持（2026-09-25 批次）

- [x] **接入避坑指南**（`docs/guide/integration-pitfalls.md` 新建）：发送侧四道防线阈值与回码（模糊闸 120/min、长度 私聊200/世界100/系统500 码点、节奏 世界5s/公会2s/私聊1s、重复第3条禁言5min）、接收侧静默语义（拉黑/频道屏蔽）、心跳/KICK/重连契约、`ec` vs `resp.code()` 代码示例、跨平面 `<game_id>:<频道>` 前缀、服务端接入三坑（双连接/ack 事件/inject_id 幂等）、fire-and-forget 发送+立即断开竞态、快速自查清单；vitepress 双侧栏收录
- [x] **API 总览重写**（`docs/api/overview.md`）：按平面重组端点表（二进制名×端口×说明）、2xxx 全量表+守门链说明、5xxx 含 5013-5030 app_chat 端口纠正与 5050-5053 peer 协议、错误码 0-11 全表、登录/消息 mermaid 流程更新（KICK 终态/TARGET_OFFLINE 队列语义）、notification 推送现状（`--push_transport http` vs 默认 logging）
- [x] **C++ 接入示例**（`sdks/core/examples/integration_example.cc` 新建）：接入全流程示例——监听器/鉴权 Provider/`SendOptions` 类型化发送按码分发（含世界节奏双发演示、210 字超长拒收）/`FetchHistory`/`MarkChannelRead`/`MemoryMessageStore` 本地历史；根 CMake 新增 `CHIRP_BUILD_SDK_EXAMPLES`（默认 ON，裸 configure/coverage preset 同步编译验证）；`sdks/core/README.md` 补示例章节
- [x] **压测工具**（`tools/benchmark/load_client.cc` 新建 + `README.md`）：N 环配对在线用户、阻塞线程 worker、SendAndRead 序列匹配跳过 notify、每轮变内容绕开 RepeatGuard、RTT p50/p90/p99/max、按回码拒绝计数（RATE_LIMITED 附提示）；README 记录守门栏约束（节奏硬编码→唯一杠杆 `--conns`/`--interval`）
- [x] **TS SDK 幽灵依赖修复**（`sdks/ts/package.json` 补 `long`/`protobufjs`）：proto TS gencode 直接 import 这两个包但此前只在 apps/web_companion 声明，`sdks/ts` 独立安装即 ERR_MODULE_NOT_FOUND/类型检查失败
- [x] **smoke-sdk 竞态修复**（`sdks/core/examples/sdk_example.cc`）：fire-and-forget `SendMessage` 把发送 post 到 io 线程,客户端收到预期消息立即 `Disconnect()` 停 io_context 时,排队中的发送 lambda 被丢弃——消息在客户端侧无声消失、服务端零痕迹（高负载/慢二进制下偶现,曾致全量门禁 smoke-sdk 阵亡）;改用带回执的类型化重载等 `SEND_MESSAGE_RESP` 再断开,`TARGET_OFFLINE`(basic 离线入队)与 `OK` 同计送达;避坑指南同步收录该坑
- [x] **测试环境鲁棒性加固**（3 个单测）:解析失败用例的 `*.invalid` 主机名在 DNS 代理沙箱会被假应答(198.18.1.174)导致用例失效——换成含空格主机名,任何环境都在 `getaddrinfo` 本地失败（`chat_peer_test`/`chat_bridge_test`/`push_transport_http_test`）
- [x] **覆盖率豁免行重锚定**（`scripts/run_coverage.sh`）：gcc 15 行归属漂移后 4 处豁免行号重钉（chat_peer_hub 监听臂/DoAccept 错误臂/SendRawPacket 关门守卫、delivery_tracker RunCheck 停止守卫、migration_worker migrating_ 守卫×4），修正两处过期注释；覆盖率回到 100.0%（8252/8252）
- [x] **smoke 断言两形态语义对齐**（`test_services.sh`）：离线接收方断言 `send code=0` 放宽为 `(0|6)`——basic 形态离线发送回 TARGET_OFFLINE(6,已入队)、enhanced 回 OK,补投递断言才是真正的证明

## 文档（P2）

- [x] architecture.md 全文中文，反映新架构
- [x] README.md 更新拓扑图和服务表
- [x] CORE.md 更新架构说明
- [x] server_plane.md 更新二进制名
- [x] 设计并集成 logo
- [x] CAPABILITY_MATRIX.md 更新服务名和路径（2026-09-22：`chirp_game_sdk_gateway` / `chirp_app_auth` / `chirp_app_sdk_gateway` / `chirp_app_notification` / `chirp_game_server_gateway` 全部对齐，补充二进制命名约定段）
- [x] 补充 peer 注册协议的详细文档（2026-09-22：新增 `docs/api/peer_protocol.md`，覆盖握手、字段、错误码、能力位、白名单、CLI、部署示例与实现状态；vitepress sidebar 与 architecture.md 已交叉引用）

## 实验性服务（暂不动）

`services/social`、`services/voice`、`services/party`、`services/search` 保持原样，后续按需迁移到对应平面目录。例外：social 已按需接入游戏在线状态事件（好友可见 IN_GAME 叠加，见 app_chat 条目 ③），voice/party/search 仍未动。
