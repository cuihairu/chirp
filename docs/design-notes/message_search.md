# 消息搜索（message search）定案与落地记录

> 状态：2026-10-08 拍板（用户令：按仓决策记录自行定案并记录后实施）。本文是
> search 面从「进程内索引壳」转正为「服务端搜索服务」的决策记录。落地形态以
> 本文为准；文中的「拍板原文」指 2026-10-07 TODO 记录的定案建议（野火 IM 参考
> 件调研后给出），「落地修订」指实施时依代码事实做出的调整——修订处均写明
> 理由，不回改拍板原文。

## 1. 背景

- `chirp_search`（services/search）转正前是一个 100ms 睡眠壳：io_context 从不
  run、无监听器，却打印 "listening"；`MessageSearchService` 是进程内倒排索引
  库（有单测），全仓没有 search 的 wire 协议——无 proto、无客户端、无任何
  进程拨 5007。
- 参考件调研（2026-10-07，野火 IM）：野火**没有服务端消息搜索协议**
  （`t_messages._searchable_key` 写时落库但社区版从不查询），真搜索 = 客户端
  本地 SQLite。该路线对 chirp 不可达：web / app 客户端均无本地持久化消息库。

## 2. 定案

### ① 拓扑：客户端 → gateway → search 5007

- search 服务监听 TCP 5007（端口归位，与 docs/api/overview.md 端口表一致）。
- 两个客户端边缘（`chirp_game_sdk_gateway` / `chirp_app_sdk_gateway`）各以
  `--search_host` 接入；接线复用 ChatBridge 的 per-client pipe 机制（见 ⑤
  ServiceBridge）：每客户端一条内部连接，SERVER_AUTH_REQ 服务信任门 +
  LOGIN_REQ 原样重放，2248 原样转发、应答原样回流。
- **与 chat 桥的一个行为差异**：chat 桥失败 = 踢客户端（chat 流量全死，踢了
  重连最干净）；search 桥失败 = **降级不踢**——客户端保持在线，2248 得到
  `SERVER_UNAVAILABLE`，search 恢复后下一条查询自动重建管道。搜索是可选增强
  能力，不允许它的可用性牵连聊天主路径。

### ② 协议：2248 / 2249（body 在 chat.proto）

- `SEARCH_MESSAGE_REQ = 2248` / `SEARCH_MESSAGE_RESP = 2249`（2247 词库批之后
  的自由 id 对）。
- `SearchMessageRequest{keyword, channel_id?, content_types[], 复合游标
  before_timestamp+before_message_id, limit≤50}`；`channel_id` 缺省 = 请求者
  全可见面。
- `SearchMessageResponse{code, matches[], has_more}`；match 含
  message_id/channel/channel_type/sender/sender_kind/msg_type/timestamp/content
  **原文**——高亮由客户端做，服务端不回 snippet（避免服务端截断语义进协议）。
- 游标语义：`(before_timestamp, before_message_id)` 复合键，排序
  `timestamp DESC, message_id DESC`，与既有 GET_HISTORY 的方向一致。

### ③ 索引：search 进程内 SQLite FTS5 持久索引

- vcpkg `sqlite3[fts5]`，零新运维件（无 ES、无独立索引进程）。
- 启动时 MySQL `messages` 表全量回填 + 100ms 壳循环改为 `id` 游标 tail 增量
  （转正前打印假 listening 的睡眠循环正好改造成 tail 泵）。
- **召回/撤回消息不索引**：chat 的撤回墓碑写 `is_recalled=1, content=''`，回
  填与 tail 都跳过 `is_recalled=1`；对「先索引后撤回」的窗口，查询时按候选
  message_id 批量核对 MySQL（单条 `IN` 查询），命中撤回即从结果剔除并顺手从
  索引删除（自愈）。MySQL 不可达 ⇒ 查询失败关闭（INTERNAL_ERROR），不降级为
  「按索引裸答」。

### ④ 可见性：同库直读 MySQL 判定

**落地修订（如实记录）**：拍板原文写「DM sender/receiver + 群 group_members
判定」。实施时核实：chat 的群（2101-2121，`GroupManager`）是**纯进程内存**
态——`groups`/`group_members` 两张 MySQL 表在 init_db.sql 里建了但全仓无一行
代码读写，群成员判定无从「同库直读」。而既有的 `GET_HISTORY` 对群/公会/队伍
频道本就无成员门槛（`ValidateGetHistoryRequest` 只对 PRIVATE 校验
`userA|userB` 包含关系）。让 search 比 GET_HISTORY 更严会让两个读路径权限倒
挂。故本批落地为：

- **PRIVATE**：请求者必须是频道 `userA|userB` 的一侧（与 GET_HISTORY 同规
  则），由 MySQL `messages` 行的 sender/receiver 直读判定（权威事实，索引列
  只是镜像）；
- **其余频道**（WORLD/GUILD/TEAM/SYSTEM/MARQUEE 与 chat 群频道）：对齐
  GET_HISTORY 现行基线——已认证即可见；
- 群成员门槛收紧列为 chat 群持久化落地时的后续项（届时 group_members 表成为
  真数据源，search 判定切过去即可，协议不变）。

**实施补充（2026-10-08，服务端落地时定）**：过滤发生在查询时（撤回剔除 +
PRIVATE 门槛），而 2248 的翻页游标按「返回的命中」推进——若一页候选全被过
滤，客户端拿不到任何命中就没有游标可推进，重发同查询会原地打转。故服务端
在**内部翻页**：攒满 limit 条可见命中或索引扫尽才应答，`has_more` = 原始候
选未扫尽。下一页候选至少还有原始命中，客户端游标（ts,id 复合）单调前进，
必然收敛；被过滤最多的那页之后可能是 0 命中 + has_more=false，语义仍是
「确实没有了」。

否决记录：客户端本地搜（无可达成本，见 §1）、ES 外置（运维面不匹配）、经
hub 事件面查询（req/resp 语义不符）。

### ⑤ 顺手把 ChatBridge 更名 ServiceBridge

search 复用 per-client pipe 机制后，「ChatBridge」这个名字对 search 管道就
是错的。更名 `ServiceBridge`（`libs/network/service_bridge.{h,cc}`），chat
与 search 各一个实例；chat 实例保持既有踢人语义（`kKickClient`），search 实
例用降级语义（`kDegrade`）。协议语义零变化。

## 3. 同批项：群昵称 alias（与 search 同批，同属检索/展示面）

- 野火 `t_group_member._alias` 同款：群成员可设自己在群内的显示别名。
- 落地三件套：
  - **schema**：`group_members.alias` 列（init_db.sql 新库 + 存量库升级脚本；
    现状该表无代码读写，本批仍不改这一点——列先行，持久化写入随 chat 群持久
    化走，见 ④ 修订）；
  - **设置面**：`SET_MEMBER_ALIAS_REQ/RESP = 2122/2123` + 全群通知
    `GROUP_MEMBER_ALIAS_UPDATED_NOTIFY = 2124`；权限 = 本人，或 MODERATOR+
    设他人；
  - **渲染两处消费**：成员列表（`GET_GROUP_MEMBERS_RESP` 的 `GroupMember.alias`）
    与群聊消息发送者名（客户端按成员列表映射，协议零改动）。
- 别名存 `GroupManager` 内存态（与群本体同一生命周期——群在重启后本就丢失，
  这是既有边界，不因 alias 假装更持久）。

## 4. 不并批项

- **桌面在线抑制手机推送**（野火 MessagesPublisher isPcOnline 同款）：不随本
  批，留 search 落地后单独排批（用户令 2026-10-08）。
- 原型 03 修订稿二审维持挂起等用户审核，不代审、不动（用户令 2026-10-08）。

## 5. SDK 接线（word_filter 批先例：proto → 三端 SDK → 服务端链路）

| 端 | 范围 |
|---|---|
| C++ core（sdks/core） | `SearchMessages` + alias 设置 API |
| TS（sdks/ts，web/desktop 经 @chirp/app-protocol 消费） | 同上 |
| C#（sdks/unity） | 同上 |

Android / iOS / 鸿蒙不在本批（iOS/鸿蒙按 2026-10-05 令只写代码有空闲才做；
Android 原生管线另有节奏）。

## 6. 验收口径

- 双树构建 0 warning、全量 ctest、既有 smoke 腿全绿；search 新增单测覆盖分词、
  FTS 索引、可见性过滤、游标；alias 新增单测覆盖权限与通知。
- `chirp_search` 转正后：起服真实监听 5007，gateway 接入后客户端 2248 往返
  可查（含 DM 权限剔除与撤回自愈）。
- 文档对账：本文件、docs/api/overview.md（2248/2249、2122-2124、5007 状态行）、
  CAPABILITY_MATRIX、TODO.md 随各增量同步。
