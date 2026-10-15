---
title: 游戏聊天特征
---

# 游戏聊天特征

最后更新：2026-10-15（表情/贴图落地注记；P1 消息处理消息编辑/消息引用与 P0 消息搜索补已落地注记——以源码为真相对账，标注实测落地日期与差异裁决）

本文档列出游戏聊天系统需要支持的特征，按优先级分阶段实现。

## P0 — 核心（游戏上线必须）

### 消息处理

- **敏感词过滤**（已落地 2026-09-22，服务端+客户端双形态）：服务端发送链 `WordFilter::Filter` 两级策略——Replace（替换，连续命中塌缩）/Reject（拦截）；词库 5 秒热更下发（登录后 `HandleWordFilterFetch`，条件 GET + 守卫），加载失败 fail-open 不拦发送；只滤发送侧。客户端预检三语言同款 `WordFilterInterceptor`（C++/C#/TS，与服务端共用同一词库文件）。**与文档差异**：原文三级（替换/拒绝/记录待审核）落地为两级——Record（审计）是服务端治理职责，不进客户端过滤链。
- **消息长度限制**（已落地）：`ValidateContentLength` 按 `MaxContentChars` 频道上限——私聊 200 / 世界 100 / 系统公告 500 码点，TEAM/GUILD/MARQUEE 协调面无上限；按 Unicode 码点计数（非 UTF-8 字节），超长回 `CONTENT_TOO_LONG`（不截断）；挂在 `ValidateSendMessageRequest`（basic/enhanced 两形态同链）。
- **发送频率限制**（已落地 2026-09-14）：按用户 60s 固定窗口（Redis 计数，`ChatRateLimiter::CheckSend`；登录另有按 IP 的 `CheckLogin` 模糊限流）+ 按频道节奏（世界 5s / 公会 2s / 私聊 1s，ChannelPacer），超频回 `RATE_LIMITED`。
- **重复消息检测**（已落地 2026-09-22）：`RepeatGuard`——同一用户连续 3 条相同内容自动禁言 5 分钟（`kRepeatTriggerCount=3` / `kRepeatMuteMs=5min`），位于发送链节奏限流之后、敏感词之前；禁言窗口内发送回 `MUTED`。
- **消息撤回**（已落地：`MessageEditManager::RecallMessage`，窗口 `--recall_window_sec` 默认 120s、可撤回频道 `--recall_channels` 默认 `private,guild`；版主删除走 `DeleteMessage` 治理路径，不受窗口约束。回码：非发送者 `AUTH_FAILED`、超窗/非撤回频道/重复撤回 `INVALID_PARAM`、本进程无台账 `USER_NOT_FOUND`）：发送后 2 分钟内可撤回（私聊/公会）。撤回后对方通过 `MESSAGE_DELETED_NOTIFY`（`is_hard_delete=false`、`deleted_by=作者`）把气泡换成"消息已撤回"墓碑，接收方离线队列里已入队的副本同步按 `message_id` 回收。**历史存档墓碑已贯通（2026-09-27）**：墓碑的语义是「置位 `is_recalled` + 抹除正文」，只置位等于原文仍躺在存档里——`MessageStore::MarkMessageRecalled` 与基础形态的 `MarkRecalled` 都连 `content` 一并清空（MySQL `UPDATE ... SET is_recalled = 1, content = ''`；Hybrid 双 tier：Redis 热层 `LSet` 原位改写、列表顺序与其余条目不动，冷层同上 UPDATE；镜像就是 `ChatMessage` proto 字节，抹除逻辑收敛为 `recall_tombstone.{h,cc}` 供三种存储实现共用）。因此重拉 `GET_HISTORY` 只剩"这条被撤回过"的事实，读不出原文；`BULK_DELETE` 的软删与单体软删同语义（成功每条立碑、每成员回收副本）。**两种部署形态都有撤回入口（2026-09-27）**：basic 之外，enhanced（`MYSQL_FOUND` 下实际构建的 `main_enhanced.cc`）此前根本没有 `DELETE_MESSAGE` 槽位，现已接入同一套 `MessageEditManager`/`MessageEditHandlers`——发送侧记台账，撤回侧经 session registry 把墓碑扇出到接收方的每一条在线设备，marker/purger 分别接 `HybridMessageStore::MarkMessageRecalled`（双 tier 抹除）与 `PurgeOfflineByMessageId`（Redis 与 Redis-down 回退队列都扫）。两条已记账的形态差异：游戏平面没有群组订阅登记，成员解析只覆盖私聊（规范 `a|b` 键取对方），公会撤回的 notify/离线回收对空成员列表是 no-op——与频道屏蔽同缺口，随群聊扇出路径一起落地；无角色体系则版主恒判 false（fail-closed），`is_hard_delete=true` 一律 `AUTH_FAILED`。gateway 侧零改动：已认证连接原样转发 2001-2999，`MESSAGE_DELETED_NOTIFY`(2232) 经 ServiceBridge 的 verbatim 管道可达客户端。
- **@提及**（已落地 2026-09-21）：`MentionManager`/`MentionHandlers`——发送侧 `ProcessOutgoingMessage` 提取提及并随消息下发（`ChatMessage.mentions` / `mentioned_user_ids` / `mentions_everyone` / `mentions_here`，接收端按字段渲染高亮）；群成员建议 `GetMentionSuggestions`（`@某人` 补全）。

### 频道管理

- **世界频道**（已落地）：全服广播；发言等级门槛见 P1 聊天等级门槛行（`LEVEL_REQUIRED = 13`，`--world_channel_min_level` 缺省 0 = 关）；节奏每 5 秒一条。
- **公会频道**（已落地）：群成员自动加入、退出/被踢自动移除（`GroupManager` 内存台账，与群本体同生命周期）；公会管理者可设发言权限（MODERATOR+，见 P1 禁言行）。
- **队伍频道**（已落地）：组队自动创建、解散/离队自动销毁（party 服务）；仅队长可解散。
- **私聊**（已落地）：1v1 对话；离线消息入队（上限 200 条），上线登录时推送补投（enhanced 形态离线队列 + 历史重建双臂）。
- **系统公告频道**（已落地）：全服只读广播，经 game 平面 inject 投递（chat 服务按只读广播处理，不产生玩家发言路径）；公告正文上限 500 码点。
- **频道屏蔽**（已落地 2026-09-23）：`DeliveryPrefs.SetChannelMuted` / `GetChannelMutes`——世界/公会/队伍三个频道可屏蔽（推送过滤：历史仍可浏览，屏蔽前已入队的离线消息登录时仍补投）；MARQUEE / SYSTEM_CHANNEL 服务广播不可屏蔽（`kMuteableChannels` 白名单）；「不听此人的消息」属黑名单维度（见下行）。

### 消息展示

- **走马灯**（已落地 2026-10-14，伴侣面形态）：`priority = URGENT` + `ttl_seconds > 0` 的消息在屏幕顶部滚动。同时只显示一条，新的替换旧的。判定函数在伴侣面 api 层（`chat_api.ts` 的 `systemSurfaceOf`，web/desktop 同款口径）：`PRIORITY_HIGH` 先判（归弹窗），`PRIORITY_URGENT` 且 `ttl > 0` 归走马灯；事件由 `ChatApi.onSystemSurface` 单槽派发——新的替换当前槽位不叠加，与通知权限、当前打开的会话/频道无关。web `MarqueeStrip`（顶部条，到点自动收起，换内容重置计时）、desktop `MarqueeStrip`（窗口顶栏同款）。**范围裁决**：滚动动画、时长与替换语义与文档一致；游戏内客户端的自渲染（真实滚动速度与可点跳转）走 SDK 侧，伴侣面只做展示。
- **系统弹窗**（已落地 2026-10-14，伴侣面形态）：`priority = HIGH` 的系统消息弹窗显示（维护通知、全服奖励）。web/desktop `SystemAnnounceDialog`（确认即关，多行正文保留换行）；消息本体仍按普通气泡留在会话流里，不因弹窗而消失。
- **消息气泡**（已落地 2026-10-14，伴侣面形态）：按 `sender_kind` 区分样式——普通玩家纸色、系统黄色、NPC 绿色、交易橙色。web `MessageBubble` 与 desktop `ChatPanel` 的 MessageRow 同款映射（`KIND_FACE` 表 + `data-sender-kind` 标签供测试断言）；自己的消息恒走主色（客户端结构上只能以玩家身份发送，故按 `mine` 收口而非按类别）；枚举外的未知值回落玩家面色（向前兼容不留 undefined 底色）。`ChatMessage.priority / ttl_seconds / sender_kind` 三字段由 web/desktop `toView` 透传，历史与实时两条路同口径。
- **物品链接**（已落地 2026-10-14，伴侣面形态）：`msg_type = ITEM_LINK`，可点击查看详情弹窗。品质颜色：白(普通)/绿(优秀)/蓝(精良)/紫(史诗)/橙(传说)（0-4 整数编码，枚举外未知值回落白渲染）。服务端在发送门校验 metadata——解析不出 `ItemMetadata` 或缺 `item_id` 回 INVALID_PARAM（basic/enhanced 双形态同口径，非 ITEM_LINK 的 metadata 原样透传）；web `MessageBubble` 与 desktop `ChatPanel` 渲染物品卡片（名称按品质分色、count>1 显 ×N、item_id caption），点击开详情弹窗（itemId/iconUrl/count/attrs 键值列表，`data-quality` 标签供测试断言）。游戏内客户端的物品详情自渲染走 SDK 侧（批形状与坐标链接同款：`ItemMetadata` 随 ChatMessage.metadata 通用管线承载，SDK 四端零改动）。
- **历史消息**（已落地）：离线期间的消息上线时拉取，`GetHistoryRequest` 分页加载（`limit` + `before_timestamp` 游标 + `has_more`）；enhanced 形态历史与离线队列双臂重建（metadata 全链保真）。
- **消息已读**（已落地）：`unread_ledger` 未读 badge（`MarkRead` / `GetUnreadSummary`，经 game_server_gateway 5035-5036 与多端在线设备面共用）；已读回执 `StoreReadReceipt` / `GetReadReceipts`（MySQL 冷层按 message_id 留痕）；私聊气泡按对方已读游标渲染「已读」标记。

### 用户行为

- **黑名单**（已落地 2026-09-23）：`DeliveryPrefs.BlockUser` / `UnblockUser` / `IsUserBlocked`——拉黑后对方的私聊静默丢弃（对发送者返回成功、不入离线队列），频道消息按成员过滤不投递；解封幂等（解封从未拉黑的人仍报成功）；`GetBlockedUsers` 列表。**与文档差异**：频道消息对被拉黑者直接不投递（接收方无感知），无「已屏蔽用户的消息」占位渲染。
- **在线状态**（已落地）：`services/social` `PresenceManager` 服务端权威（在线/忙碌/离开/隐身），好友可见；社交 RPC 3001-3029（好友花名册 + 上线通知开关见 P1 好友上线行）。
- **正在输入**（已落地）：`TypingIndicator` 频道广播（proto 899-902），接收端按会话聚合显示「对方正在输入…」（web/desktop typing store 本地 TTL 状态）。

## P1 — 增强（上线后迭代）

### 消息处理

- **消息编辑**（已落地 2026-09-18）：`EDIT_MESSAGE_REQ/RESP = 2225/2226`（gateway.proto:116）。服务端 `MessageEditHandlers` + `MessageEditManager` 内存台账（编辑次数上限 `max_edit_count=100`、编辑历史容量 `max_edit_history_size`，超限拒绝；`is_edited`/`edit_count`/`edit_history` 随 ChatMessageFull 下发）。SDK 四端对齐（C++ core `ChatClient::EditMessage`、sdks/ts 与 apps/shared/protocol 双包 msg_map spec、C# 同批）；web/desktop `editMessage`（chat_api.ts:540 双端镜像）+ 气泡"已编辑"标记。**与文档差异**：文档原文"可编辑一次、编辑历史不保留"——实际为最多 100 次编辑（可配），且编辑历史保留并随消息下发。
- **表情/贴图**（已落地 2026-10-15，伴侣面形态）：16 个内置系统表情（web_companion `components/emoji.ts` 与 desktop `ui/emoji.ts` 同文 `SYSTEM_EMOJIS`）。消息文本中的 `[emoji:code]` 经 `expandEmojiCodes` 分片展开渲染为 emoji 字符（正则 `[a-z0-9_]+`，未知 code 原样保留字面——向前兼容，与 sender_kind/品质/稀有度回落同口径）；输入面带表情选择器（表情按钮 + Popover 网格，点击向草稿插入 `[emoji:code]`）。**服务端零改动**：表情走 TEXT 内容透传，长度/敏感词门照常生效；SDK 四端零改动（metadata 管线不涉及）。贴图（大图包）未做——待贴图资源与 metadata 载体（参照 ACHIEVEMENT 批形状）另批。
- **消息引用**（已落地 2026-10-11）：`replyToMessageId` 随 SEND_MESSAGE 全链透传（发送侧第 4 参 + wire 字段 + 历史回读映射，web chat_api.ts:307/348/400/1039，desktop 镜像）；气泡渲染引用摘要块（`MessageBubble` quoted 区，web/desktop 同形；被引用方已删除或不可见时显示"原消息已删除或不可见"占位）。服务端按普通字段透传（无独立校验门）。
- **富文本**（已落地 2026-10-15，伴侣面形态）：消息文本里的 `**粗体**` 与 `*斜体*` 标记在气泡内渲染为 strong/em——web `MessageBubble` 与 desktop `ChatPanel` 同形（`parseRichText` 双端同文：web `components/rich_text.ts`、desktop `ui/rich_text.ts`，各 10 例单测）。**服务端零改动**：标记走 TEXT 内容透传，长度/敏感词门照常生效（与 P1 表情批同口径）。**解析刻意保守**：定界符左侧必须不是空白（串首算合法边界）、跨度内必须非空且成对闭合，否则原样保留字面——"2 * 3 = 4"、"user_id" 这类裸星号/下划线不被吞掉，未闭合的 `**粗` 也照原样显示（右定界符侧不加空白约束，中文正文标记紧贴前字更常见）。**与文档差异**：颜色（VIP 特权）未做——聊天面无 VIP 权益数据源（与聊天等级门槛同口径缺口），等权益面出现再随其一起落；`message_interceptor.h` 已留 VIP 特效接缝但无权益输入。

### 频道管理

- **临时频道**（已落地 2026-10-15，群域承接）：活动/副本临时频道 = 带 TTL 的群——`CreateGroupRequest.ttl_seconds`（字段 7，0 = 永久群；上限 30 天，超限/负数回 `INVALID_PARAM`），到点时刻换算成 `GroupInfo.expires_at`（epoch 毫秒）随 `GET_GROUP_INFO` / `GET_USER_GROUPS` / `GROUP_CREATED_NOTIFY` 下发，客户端据此显示倒计时并在到点后本地移除该群。**到点语义**：群视同不存在——成员的本群频道发送回 `AUTH_FAILED`（与非成员同语义，发送门零改动），读路径惰性摘除（`GroupManager::LookupLocked` 命中到点群即从台账与 `user_to_groups_` 摘除），无人访问的过期群由 `GroupExpirySweeper` 周期回收（enhanced 形态接线，5 分钟一拍；basic 惰性摘除已覆盖语义，清扫只是内存回收）。「参与者自动加入」由既有 `CreateGroupRequest.initial_members` 覆盖，零新增。Inc 2（SDK 四端建群 TTL 入参与客户端倒计时渲染）已落地 2026-10-15：C++ core `CreateGroup` 加 `ttl_seconds` 重载（0 = 永久群，两参版保持不变）；TS 双包 schema 驱动自动携带（wire-lock 测试补向量）；web/desktop 伴侣端建群对话框加有效期档位（永久/1 小时/1 天/7 天），群会话凭 `expiresAt` 显示「剩 N 分/小时/天」倒计时（15s 时钟粒度），名册为准修剪（退群/被踢/到点本地消失，私聊不参与修剪）。
- **跨服频道**：合服后的跨服聊天。按 server_id 路由。
- **自定义频道**（范围裁决 2026-10-15，群域承接）：玩家建频道 = 建群——`CREATE_GROUP`（2101/2102）即玩家创建的命名频道（群 id 即频道 id，GUILD 频道域），创建者为 OWNER、可授 ADMIN/MODERATOR（`GroupMemberRole` 阶梯：踢人/禁言/别名等管理面齐备）。**裁决**：不再做独立的"自定义频道"频道类型——语义与群组系统完全重叠（交易频道/组队频道用建群承接），独立频道命名空间（跨群检索/目录/按主题聚合）留待出现真实需求另批。

### 用户行为

- **禁言**（已落地 2026-10-10，群域形态）：群 MODERATOR+ 禁言/解禁本群成员（`SET_GROUP_MUTE_REQ/RESP` 2250/2251 + 全群通知 `GROUP_MEMBER_MUTED_NOTIFY` 2252；时长秒数计，0=解禁，上限 30 天）。禁言期间该成员的群频道发言被拒（`MUTED` 回码，basic/enhanced 两形态发送门在存储之前拦截，消息不落任何存储）；私聊/台账外频道不受影响。禁言态随 `GroupMember.muted_until_ts` 下发（`GET_GROUP_MEMBERS`），GroupManager 内存台账惰性过期，与群本体同一生命周期。**范围裁决**：文档原文"管理员禁言所有频道"需要全局管理员身份，chirp 聊天面角色体系是群内的（无全局身份可挂），故本批落地群域禁言；全局禁言随全局管理员身份另批。 SDK 四端对齐（C++ core / sdks/ts / apps/shared/protocol / C#，2250 往返 + 2252 通知钩子，2026-10-10）。
- **举报**（已落地 2026-10-13，举报面形态）：玩家举报消息，`REPORT_MESSAGE_REQ/RESP` 2253/2254。服务端进程内存台账（`ReportLedger`）按 (message_id, reporter_id) 去重幂等——同一玩家重复举报同一消息不重复计数，响应回显去重后的累计 `report_count`；被举报消息必须在本实例编辑台账里（即本实例实际投递过的消息），台账外回 `USER_NOT_FOUND`——与撤回的"台账外消息"同一语义，也挡住垃圾 message_id 刷台账的内存增长。理由可选，上限 500 码点（服务端校验），每消息封顶留痕 100 条。**范围裁决**：文档原文"管理后台审核队列（无事/警告/禁言/封号）"需要全局管理员身份与后台面，与群域禁言同一裁决——审核队列待全局管理员面另批；台账按时间留痕即为其预留消费面。SDK 四端对齐（C++ core `ChatClient::ReportMessage`、sdks/ts 与 apps/shared/protocol 双包 `reportMessage`、C# `ReportMessageAsync`，2253 往返 + 未连接早退，2026-10-13）；web/desktop 消息气泡带举报入口（Flag 按钮 + confirm，自己的消息不可举报）。
- **聊天等级门槛**（已拍板 2026-10-11，采纳形态①落地）：世界频道要求声明等级达到 `--world_channel_min_level` 才能发言，未达标回 `LEVEL_REQUIRED = 13`（新专码）。**拍板依据与结论**：聊天面全链无玩家等级数据源（登录协议、账号面、存储均无 level 字段），可选三形态裁决——① **采纳**：`LoginRequest.level = 5`（客户端声明，可信度与既有裸 userId 登录同级——聊天面本来就信任登录声明的身份本身，补一个可伪造的等级不改变威胁模型，改动最小）；② gateway 桥带真数据源记为后续增强（需实时准确时再启）；③ 管理后台白名单否决（需先建 chat 配置 API 与后台面，与该功能不成比例）。**落地形态**：声明等级随 `SessionRegistry` 会话元数据走（`session_to_level`，非身份元数据，仅世界门读取）；门槛旗标缺省 **0 = 关**（app/companion 部署零感知，游戏部署显式开启，建议 10）；门槛位于发送守门链的 auth/param 档（参数校验之后、限流/节奏/重复禁言/敏感词之前）——不达标发送不消耗任何预算或过滤器工作，未认证会话的 AUTH_FAILED 路径不受影响；basic/enhanced 两形态同接线（enhanced 在 `on_send_message` 门链内）。测试：basic 生产派发驱动（缺省关/门槛拒/达标过墙/私聊不受影响）+ registry 往返；enhanced 门链与 free 函数分体、测试夹具驱动不到门槛，按禁言批先例 basic 覆盖语义。SDK 四端 `level` 声明随登录对齐：C++ core `ChatClient::Login` 尾参（io 线程成员存档，续期重登沿用）、sdks/ts 与 apps/shared/protocol 双包 `ChatPipelineDeps.level?` getter（每轮登录重读，缺省 0）、C# `LoginAsync` 尾参（续期重登沿用）；web/desktop `ChatApi.login` 尾参（伴侣面恒缺省 0）。
- **VIP 特权**：VIP 玩家消息带颜色、喇叭效果、专属表情。

### 消息展示

- **坐标链接**（已落地 2026-10-13，消息展示形态）：`msg_type = COORD_LINK = 15`，坐标载荷走 `ChatMessage.metadata`（`CoordMetadata{map_id, x, y, z, label}`）。发送侧校验 `ValidateCoordMetadata`：垃圾 metadata / 缺 `map_id` 回 `INVALID_PARAM`（与正文长度校验同档，basic/enhanced 两形态同接线，落任何存储之前拦截）；非 COORD_LINK 的 metadata 原样透传（协议层不越权校验未启用消息类型）。存储链全通：basic 内存/Redis 直存 `ChatMessage`，enhanced 经 `MessageData`（离线队列 + 历史）与 MySQL 冷层 BLOB 列（HEX/UNHEX 二进制安全，幂等 ALTER，旧库无列容忍）。SDK 四端 sendMessage 带 metadata：C++ core `SendOptions{msg_type, metadata}`（本地存档同写）、sdks/ts 与 apps/shared/protocol 双包 `SendOptions.metadata`（wire + 本地档案双落）、C# `SendOptions.Metadata`。**范围裁决**：文档原文"可点击自动寻路到指定坐标"需要游戏内客户端的寻路面，伴侣面（web/desktop）无寻路——落地为坐标卡片（点击复制 "x, y[, z]"，零高度省略），游戏内接入走 SDK 自渲染。
- **成就分享**（已落地 2026-10-14，伴侣面形态）：`msg_type = ACHIEVEMENT`，卡片式展示（图标 + 名称 + 稀有度 + 描述；获得时间由气泡既有时间标签承担）。服务端在发送门校验 metadata——解析不出 `AchievementMetadata` 或缺 `achievement_id` 回 INVALID_PARAM（与坐标/物品链接同层同口径，非 ACHIEVEMENT 的 metadata 原样透传；basic/enhanced 双形态同接线）；web `MessageBubble` 与 desktop `ChatPanel` 渲染成就卡片（名称按稀有度分色、稀有度文案、描述、achievement_id caption、iconUrl 图标，`data-rarity` 标签供测试断言）；稀有度 0-4 整数编码（普通/优秀/稀有/史诗/传说，与品质同阶梯配色），枚举外未知值回落普通渲染。游戏内客户端的成就卡片自渲染走 SDK 侧（批形状与物品链接同款：`AchievementMetadata` 随 ChatMessage.metadata 通用管线承载，SDK 四端零改动）。
- **公会招募**（已落地 2026-10-15，伴侣面形态）：`msg_type = GUILD_RECRUIT = 16`，卡片式展示（公会名 + `等级 N · 人数 M[/K]` + 招募语 + guild_id caption）。载荷走 `GuildRecruitMetadata{guild_id, guild_name, level, member_count, max_members, description}`（metadata 通用管线，SDK 四端零改动）；服务端发送门校验——解析不出或缺 `guild_id` 回 INVALID_PARAM（与交易/成就链接同层同口径，basic/enhanced 双形态同接线）；web `MessageBubble` 与 desktop `ChatPanel` 渲染公会卡片（max_members=0 只显 M）。**范围裁决**：文档原文"一键加入按钮"需要公会系统与加入协议面（伴侣面无此动作面）——落地为展示性卡片，游戏内接入走 SDK 自渲染。
- **组队招募**（已落地 2026-10-15，伴侣面形态）：`msg_type = PARTY_RECRUIT = 17`，卡片式展示（副本名 + `缺 N 人 · M/K` + 队长 + party_id caption）。载荷走 `PartyRecruitMetadata{party_id, dungeon_name, needed, current, leader_name}`；服务端发送门校验——解析不出或缺 `party_id` 回 INVALID_PARAM（同上同口径）；web/desktop 渲染组队卡片（current=0 只显缺 N 人）。**范围裁决**："一键申请按钮"同上——展示性卡片，游戏内自渲染走 SDK。
- **消息搜索**（已落地 2026-10-08/10-10）：`SEARCH_MESSAGE_REQ/RESP = 2248/2249`（gateway.proto:158）。服务端 chirp_search（TCP 5007）维护 SQLite FTS5 持久索引（MySQL 核对自愈，失败关闭不降级；决策记录 docs/design-notes/message_search.md）：CJK 单字成 token + 拉丁按词的两侧一致分词做 FTS5 短语匹配，`channel_id`/`content_types` 过滤，`(before_timestamp, before_message_id)` 复合游标分页（limit 钳到 [1,50]，缺省 20），原文回传高亮由客户端做（服务端不回 snippet）。desktop `ChatApi.searchMessages`（226d912）+ web `SearchDialog`（7e7a190，5 例单测）；apps/shared/protocol `chat_pipeline.searchMessages`（wire 往返 + 连接/关键词校验顺序单测）。**与文档差异**：文档原文"按关键词、时间范围、频道过滤"——频道过滤已落地（channel_id），时间范围无独立范围参数（before_* 是排序分页游标，非区间过滤）；消息类型过滤额外支持。

### 游戏特色

- **NPC 对话**（已落地 2026-09-13）：`services/game/npc_dialog`——`NpcEngine` 关键词规则表（ASCII 大小写不敏感子串匹配，`*` 兜底回复）+ `npc_responder` 应答面 + chat 上行链路（`chat_npc_uplink`）；`--metrics_port` 埋点（事件/回复计数）。接口是替换更聪明引擎（如 LLM 服务）的接缝。
- **系统播报**（已落地 2026-10-15，inject 展示面元数据形态）：game 平面 `MessageInjectRequest`（proto/game_server_gateway.proto）带 `priority`/`ttl_seconds` 展示面字段（值域与 chat.Priority 对齐，缺省 0 零感知）；chat 侧 `InjectConsumer` 直传 `ChatMessage.priority`/`ttl_seconds`，server_gateway 整消息转发零改动。走马灯判定二要素（URGENT + ttl>0，客户端 `systemSurfaceOf` 与频道无关）与系统弹窗（HIGH）经注入即点亮；渲染面（走马灯/弹窗/气泡分色）2026-10-14 已落地。**范围边界**：事件源（BOSS 刷新/首杀/稀有掉落/玩家成就）在游戏侧；inject 合法频道域为 PRIVATE..WORLD（`InjectConsumer` 校验），走 WORLD 频道注入即可。
- **交易消息**（已落地 2026-10-15，协议+消费面形态）：`msg_type = TRADE_STATUS`，卡片式展示（状态徽章 + 物品摘要 ×N + 金额 + trade_id caption）。服务端在发送门校验 metadata——解析不出 `TradeMetadata` 或缺 `trade_id` 回 INVALID_PARAM（与坐标/物品/成就链接同层同口径，basic/enhanced 双形态同接线）；`status` 为展示文本不锁枚举（proto 约定 CREATED/ACCEPTED/COMPLETED/CANCELLED，未知名值原样渲染，新状态值不被旧服务端挡下）；web `MessageBubble` 与 desktop `ChatPanel` 渲染交易卡片（`data-trade-status` 标签供测试断言）。SDK 四端零改动（metadata 通用管线承载）。**范围边界**：文档原文的"上架/出价/成交/过期状态变更走系统频道"事件源在游戏侧拍卖行系统；当前 game 平面 inject 面（`MessageInjectRequest`）只有 content 无 msg_type/metadata 字段，注入卡片消息需先扩 inject 协议（metadata 透传）——属后续增强，玩家/SDK 侧消费链（发送门 + 存储 + 渲染）本批已就绪。
- **好友上线通知**：好友上线时推送通知（可关闭）。✅ 服务端已落地（3025-3029，2026-10-13）：`SET/GET_NOTIFY_PREFS` 用户级开关（缺省全开，Redis 持久化）+ `FRIEND_ONLINE_NOTIFY` 只在有效状态 OFFLINE→非 OFFLINE 跃迁时补发（登录/SET_PRESENCE/游戏断言三链路，逐收件人过滤），body 复用 `PresenceNotify`、花名册 3021 不受影响。**消费侧已落地（2026-10-13/15）**：TS 双包 `msg_map` 的偏好 spec 双份同步（服务端对缺失子消息回 INVALID_PARAM，不静默当成"关"）；web `social_api`（`onFriendOnlineNotify` 订阅 3029，自己与名册外用户去重、畸形体忽略）+ desktop 同款；两端 `ChatPage`/`MainWindow` 走单槽 toast 展示，3025/3027 开关同面。
- **邮件通知**：新邮件到达时推送通知。

## P2 — 运营（长期维护）

### 安全与合规

- **反广告**（自动禁言部分已落地 2026-09-22，重复检测形态）：`RepeatGuard`（services/shared/chat/src/repeat_guard.h）按用户跟踪连续相同内容——第 3 次连续相同发送（`kRepeatTriggerCount`）开启 5 分钟禁言窗（`kRepeatMuteMs`），窗内**所有**内容都拒发（不只重复内容），拒绝回 `RATE_LIMITED`（P0 重复消息检测与发送频率共享该码）；守门位置在 pacing 之后、敏感词过滤之前，basic/enhanced 双形态同接线（io 线程内态，无锁）。**范围裁决**：文档原文"标记审核"需要管理后台审核队列（全局管理员面）——与举报/禁言同款裁决，审核队列另批；举报台账（ReportLedger，2026-10-13）按时间留痕即为其预留消费面。
- **消息审计**：所有消息留存 N 天（可配置）。管理后台可回溯查询。
- **实名关联**：消息可追溯到实名账号。配合举报/投诉处理。
- **防骚扰**（已落地 2026-10-15，接收方私聊限制形态）：`SET/GET_DM_RESTRICTION 2255-2258` 接收方开关（缺省**关**，io 线程内存态——与黑名单/频道屏蔽同生命周期，进程重启即复位），开启后陌生人的私聊被拒 `DM_RESTRICTED`（common 专码 14，消息**不落任何存储**：门在写入之前）。"陌生人"判定 = 非 social 平面好友 **且** 无共同群：好友半边由 `FriendGraph`（services/shared/chat/src/friend_graph.cc）只读消费 social 服务落在共享 Redis 的快照 `chirp:social:friends:<receiver>`（`StoredFriendList`，与 social `main.cc` 的 key 契约固定、无缓存随取随新），同群半边走 `GroupManager::ShareAnyGroup`（过点临时群按到点惰性摘除语义参与判定）。豁免：好友、同群、本人会话、NPC 接收方。**降级方向 = 收紧**：Redis 缺失/键缺失/快照损坏一律按非好友处理（限制只剩同群半边生效），不抛不崩。**范围裁决与与文档差异**：原文"同公会"——chat 面的群体系是 GroupManager 台账（无独立公会平面），"共同群"即其对应物；黑名单语义更强（投递侧静默丢弃、位于存储之后）且与本开关可叠加，两者不互斥。basic/enhanced 双形态同接线；distributed 形态不接（与禁言批同口径——无客户端开关面）。

### 性能与运维

- **消息持久化**（已落地，2026-10-11/12 hybrid store 形态）：`HybridMessageStore`（services/shared/chat/src/hybrid_message_store.h）双写 Redis 热层 + MySQL 冷层；Redis 不可用时消息落内存待发队列，恢复后补灌。历史查询热层先行、冷层兜底；撤回墓碑热层原位改写 + 冷层置位；私聊引用目标先扫 Redis 同步写的热层。MySQL 经连接池（HEX/UNHEX 二进制安全，幂等 ALTER 旧库无列容忍）。
- **消息过期**（已落地 2026-10-15，enhanced 形态保留窗）：`--history_retention_days`（普通频道）与 `--private_history_retention_days`（私聊，未配时跟随普通窗——「私聊保留更久」= 把它配得更大的旋钮）双旗标，天数计，**缺省 0 = 关**（app/companion 部署零 I/O）。`HistoryRetentionSweeper` 每小时一趟（定时臂与 MessageMigrationWorker 同款 strand 形状）把 `now - retention` 换算成 cutoff 交给 `HybridMessageStore::ExpireBefore`：热层（Redis history 列表）逐 blob 解析按 channel_type 各用各的 cutoff 逐条 LRem（解析失败的 blob 不动）；冷层（MySQL 档案）一条 DELETE 两 cutoff（epoch 毫秒恒正，cutoff <= 0 的子句天然匹配不到行）。清理尽力而为（Redis/MySQL 不可用计 0 不抛）。**范围裁决**：basic 形态（内存/Redis 直存）不接——内存态随进程消亡，其 Redis 列表属开发冒烟面；离线队列有独立 TTL（`redis_offline_ttl_seconds`）不受保留窗管。
- **消息压缩**（服务端已落地 2026-10-15，历史读回形态）：范围裁决——「大量消息传输」落为**历史读回**（GET_HISTORY_RESP 大清单方向；实时扇出/离线补投保持单条原样，本来就是小包）。协议（additive，对未声明客户端零可见）：`LoginRequest.supports_gzip = 6` 登录一次性声明（同 `supports_message_ack` 的客户端能力先例，会话级记账、断开即清）；声明会话的成功历史响应里 `GetHistoryResponse.messages_gzip = 4`（bytes，gzip 容器 RFC 1952）替代明文 `messages`——`code`/`has_more` 留在外层，压缩载荷是**只带 messages 清单的同型 GetHistoryResponse**，客户端解压后按同型消息解析回 `.messages`，线上无旁格式。空清单不压缩（保持明文空列表）；压缩失败兜底保持明文不丢消息。gzip 容器（windowBits 15+16）让四端运行时零配置直读（TS `DecompressionStream('gzip')`、C# `GZipStream`、Go `compress/gzip`、Java `GZIPInputStream`；C++ 侧 zlib windowBits 15+32 自动识别）。`libs/common/gzip.{h,cc}` 是压缩原语（解压带 16 MiB 炸弹上限），服务端三形态同接线（basic/enhanced 能力记账走 SessionRegistry `gzip_sessions`，distributed 本地集合；重登录改口双向覆盖）。**解压侧已落地 C++ core、TS 双包、C# 与 Android Kotlin（2026-10-15）**——`sdks/core` 登录即声明 `supports_gzip`，`FetchHistory` 复用 `chirp::common::GzipDecompress` 拆封同型载荷、把清单搬回明文 `messages`，坏容器/坏载荷上报既有 `BadResponse`、超时上报既有 `Timeout`；TS 双包（`sdks/ts` + `apps/shared/protocol`）登录声明 + `GET_HISTORY` 解码走可选 `inflateResponse` 钩子拆封（`DecompressionStream('gzip')` 跨运行时零依赖，16 MiB 解压上限与 C++ 同值），容器损坏/超限抛 `GzipError` 让上层看到失败、不静默丢历史；`web_companion` / `desktop` 登录已声明该能力。C#（`sdks/unity`）登录声明 + `GET_HISTORY` spec 挂拆封钩子（`GZipStream` 零配置直读 RFC 1952，16 MiB 上限同值；**额外自校验 CRC/ISIZE 尾部**——`GZipStream` 只在 deflate 末块到达时才验尾部，中途截断的容器会被静默读成短清单，尾部自校验把截断拉回与 C++/TS 一致的报错），坏容器抛 `GzipInflateError`。Android Kotlin（`apps/android` 协议核）登录声明 + `MsgSpecs.getHistory` 挂拆封钩子（`GZIPInputStream` 零配置直读且自带尾部校验，16 MiB 上限同值），坏容器抛 `GzipError`。`sdks/unreal` 是 `sdks/core` 的薄门面，解压面随 C++ core 自动继承。iOS Swift / 鸿蒙解压侧待落地（**卡点**：Foundation 两平台都不提供 gzip 容器解码——`NSData.decompressed(using: .zlib)` 是 zlib 容器不是 RFC 1952、`Compression` 框架只给裸 deflate——要读就得加 zlib `systemLibrary` target + FFI 或引第三方包，而两端本仓均无可跑工具链，写完无法编译验证；落地前不声明该能力，服务端对未声明会话永远发明文，明文路径不受影响）。
- **多设备同步**（已落地，2026-09-14 起多平台会话形态）：会话注册表按 (user_id, platform) 每平台一个槽（libs/network/session_registry.h，同对重绑踢旧会话——登录顶号）；投递扇出 `GetUserSessions` 覆盖同账号全部在线会话，多设备同收实时消息；已读光标经 `MESSAGE_READ_NOTIFY` 广播给相关成员（多设备已读同步）；设备清单经 `online_devices` 种子 + `DEVICES_PRESENCE_NOTIFY`（1020）实时增减。
- **消息去重**（已落地，2026-09-18 伴侣面形态）：web/desktop `message_store` 按服务端 message_id 丢弃重复追加（实时 notify 与 SEND_MESSAGE_RESP 确认/历史回灌同 id 只渲染一次）；C++ core 本地存档同 key 归一。服务端无状态（按 (channel, message_id) 天然幂等）。
- **连接保活**（已落地，2026-09-14/20 分批）：C++ core 25s 心跳（pong 回声校验 + 连续未答判死）、500ms→15s 带抖动指数退避重连、KICK 终态不重连（sdks/core/src/backoff.h）；SDK 四端同形（TS/C#/native）。消息补发：服务端离线队列（hybrid store pending 副本带 expires_at，上线补投、迟到 ACK 清理 `hybrid_message_store.cc:368`）+ 客户端 OfflineSendQueue（Swift/Kotlin 原生协议核，断网期间发送排队重试）。

## 客户端渲染决策矩阵

客户端收到消息后，按 `(channel_type, msg_type, priority)` 三维度决策渲染方式：

```
优先级判断：
  priority == URGENT && ttl_seconds > 0 → 屏幕顶部走马灯
  priority == HIGH && channel_type == SYSTEM → 系统弹窗

频道路由：
  channel_type → 对应 tab/窗口（私聊 tab、公会 tab、世界 tab、...）

消息渲染：
  msg_type == TEXT → 文本气泡
  msg_type == ITEM_LINK → 可点击物品卡片
  msg_type == SKILL_LINK → 可点击技能卡片
  msg_type == ACHIEVEMENT → 成就分享卡片
  msg_type == NPC_DIALOG → NPC 对话气泡（绿色）
  msg_type == TRADE_STATUS → 交易状态卡片（橙色）
  msg_type == SYSTEM → 系统消息（黄色，居中）
```

## Proto 对应关系

| 文档概念 | Proto 字段 |
|---|---|
| 频道类型 | `ChannelType`（PRIVATE/TEAM/GUILD/WORLD/SYSTEM_CHANNEL/MARQUEE） |
| 消息类型 | `MsgType`（TEXT/ITEM_LINK/SKILL_LINK/ACHIEVEMENT/NPC_DIALOG/TRADE_STATUS/...） |
| 优先级 | `Priority`（PRIORITY_LOW/NORMAL/HIGH/URGENT） |
| 发送者类型 | `SenderKind`（SENDER_USER/SYSTEM/NPC/SERVICE） |
| 物品数据 | `ItemMetadata`（item_id/item_name/quality/icon_url/count/attrs） |
| 技能数据 | `SkillMetadata`（skill_id/skill_name/level/icon_url/description） |
| 成就数据 | `AchievementMetadata`（achievement_id/achievement_name/description/icon_url/rarity） |
| 交易数据 | `TradeMetadata`（trade_id/status/amount/item_name/item_count） |
| NPC 对话数据 | `NpcDialogMetadata`（npc_id/npc_name/dialog_id/options） |
