---
title: 接入避坑指南
---

# 接入避坑指南

面向第一次接入 chirp 的游戏客户端/服务端团队。这里的每一条都是**服务端会拒收或静默改变行为**的语义,客户端不处理就会表现成"消息丢了""发不出去""莫名被踢"。阈值与默认值以代码为准,本文标注对应默认值;部署侧可用启动参数覆盖,见各服务 `--help`。

消息帧格式、msg-id 总表与错误码总表见 [API 总览](../api/overview.md)。

## 发送侧:四道防线一条链

`SEND_MESSAGE_REQ` 在服务端按固定顺序过防线,任何一道拒收都会回专码。**拒收的消息不会投递,也不会进离线队列**;客户端按码决定重试策略。

```
登录校验 → 模糊闸 → 内容长度 → 频道节奏 → 重复禁言 → 敏感词 → 投递
```

### 1. 模糊限流:`RATE_LIMITED`

- 默认**每用户 120 条/分钟**(发送)、**每 IP 30 次登录/分钟**(登录),basic 形态默认开启;`--send_rate_limit_per_min` / `--login_rate_limit_per_min` 可调,0 = 关闭。
- 这是"总量闸":不区分频道、不区分内容。多实例部署 + Redis 时是全局口径。
- **坑**:压测/机器人脚本最容易触发。收到 `RATE_LIMITED` 应指数退避,不要立刻重发——重发同样计数,会一直被拒。

### 2. 内容长度:`CONTENT_TOO_LONG`

按**频道类型**限码点数(UTF-8 字符数,不是字节数;CJK 3 字节算 1 个字):

| 频道 | 上限 |
| --- | --- |
| 私聊 PRIVATE | 200 |
| 世界 WORLD | 100 |
| 系统公告 SYSTEM | 500 |
| TEAM / GUILD / MARQUEE | 不限 |

- **坑一**:服务端**不做截断**,超长直接拒。客户端要在输入框层面限制,或收到 `CONTENT_TOO_LONG` 后提示用户精简。
- **坑二**:按码点计数,`content.size()`(字节数)判断会误伤中文——200 个汉字是 600 字节。

### 3. 频道发送节奏:`RATE_LIMITED`

同一用户在同一频道有最小发送间隔:

| 频道 | 最小间隔 |
| --- | --- |
| 世界 WORLD | 5 秒 |
| 公会 GUILD | 2 秒 |
| 私聊 PRIVATE | 1 秒 |
| TEAM / MARQUEE / 系统公告 | 不限 |

- 窗口锚定在**最后一次成功放行**的发送:窗口内被拒的重试**不会**延长等待时间。所以正确姿势是:被 `RATE_LIMITED` 拒后等剩余窗口再发,而不是连续重试。
- 这是**进程内**计数:多实例部署下每个实例独立计时(模糊闸才是全局口径)。

### 4. 重复消息禁言:`RATE_LIMITED`

- 同一用户**连续第 3 条内容完全相同**的消息触发 **5 分钟禁言**,且触发的那条本身也被拒收(不让刷屏的第三条到达任何人)。
- 禁言期内**任何内容**都拒(同码 `RATE_LIMITED`);发送不同内容立即重置连击计数。
- **坑**:客户端"重发"按钮连点三次同文案 = 禁言。要么禁用重试,要么提示用户改文案。

### 5. 敏感词:`WORD_FILTERED`(或静默替换)

- 服务端策略三选一(`--word_filter_policy`):`replace`(默认,命中区段折叠为 `**` 后放行)/ `reject`(拒收回 `WORD_FILTERED`)/ `record`(放行,仅记 Warn 日志)。
- 词库 `--word_filter_file` 每行一词、`#` 注释、大小写归一,文件 mtime 变化 5 秒内热更新;词库缺失/不可读时 **fail-open**(不挡聊天)。
- **坑**:`replace` 策略下客户端回显的内容可能和发送的不一样(被折叠),这是预期行为,不要当成 bug 回报。
- C++ SDK 侧 `WordFilterInterceptor` + `ParseWordLexicon` 提供同一套词库格式与语义,客户端可与服务端共用一份词库文件做本地预检,省一次往返。

## 接收侧:静默语义

这几条**不回错误码**,是服务端的静默过滤/静默成功。排查"消息没收到"先看这里:

- **消息黑名单**(2239-2244):A 拉黑 B 后,B 发给 A 的私聊**回 `OK`**(B 侧看起来发送成功)、B 的群/世界消息按成员过滤,A 收不到。拉黑前已入离线队列的消息照发。解除幂等。与社交面好友黑名单(3011)互相独立,只影响消息投递。
- **频道屏蔽**(2235-2238):屏蔽世界/公会/队伍频道后,实时推送跳过、离线队列不再入队,但**历史仍然可以拉取**(GET_HISTORY 不过滤)。MARQUEE/系统公告是服务端广播,不可屏蔽;对这些频道发 SET_CHANNEL_MUTE 回 `INVALID_PARAM`。
- **离线队列**:私聊接收方不在线时 `SEND_MESSAGE_RESP` 回 `TARGET_OFFLINE`(不是错误——消息已入队),接收方下次登录补投。内存兜底**每用户 200 条**,超出丢最旧;Redis/MySQL 形态容量见部署配置。**坑**:把 `TARGET_OFFLINE` 当失败提示给用户是错的,消息其实已经收下了。

## 撤回(消息编辑/删除面,2225-2232)
`DELETE_MESSAGE_REQ`(`is_hard_delete = false`)对**发送者本人**就是"撤回":服务端在撤回窗口内把它软删,向频道成员广播 `MESSAGE_DELETED_NOTIFY`(客户端据此把气泡换成"消息已撤回"墓碑),并回收接收方离线队列里已入队的副本。

| 项 | 默认值 | 启动参数 |
| --- | --- | --- |
| 撤回窗口 | 120 秒 | `--recall_window_sec`(秒,0 = 不限) |
| 可撤回频道 | 私聊 PRIVATE + 公会 GUILD | `--recall_channels`(逗号分隔,空 = 全频道不可撤回) |

回码语义(`DELETE_MESSAGE_RESP.code`):

| 回码 | 含义 | 客户端应对 |
| --- | --- | --- |
| `OK` | 已撤回,已广播 delete notify | 把本地气泡换成墓碑 |
| `AUTH_FAILED` | 不是发送者本人;或请求 `is_hard_delete` 但没有版主权限;或 `user_id` 与认证身份不符 | 隐藏撤回入口,别重试 |
| `INVALID_PARAM` | 超窗 / 该频道不可撤回 / 已经撤回过 | 提示"超过撤回时间",**不要重试**(重复撤回不会二次广播) |
| `USER_NOT_FOUND` | 本进程没有这条消息的台账(例如消息早于本进程存在) | 同上,按"撤回失败"处理 |

- **版主删除不是撤回**:频道版主(`is_hard_delete`,公会角色 ≥ MODERATOR)走治理路径,**不受窗口和频道限制**,`is_hard_delete=true` 会永久擦除台账。运营要放开某频道的撤回请改 `--recall_channels`,不要靠版主通道。
- **渲染墓碑而不是删气泡**:`is_hard_delete=false` 且 `deleted_by == 该消息的 sender_id` → 显示"消息已撤回"灰色占位(保留行高,否则聊天列表会跳);`deleted_by` 不是作者 → 版主删除,按"消息已删除"渲染。
- C++ SDK 用 `RecallMessage(message_id, cb)`(`DeleteMessage(id, false, cb)` 的语义别名,名字更贴游戏侧"撤回"按钮);服务端回 `OK` 才更新 UI。
- **坑一(存档无墓碑)**:撤回只作用于**实时链路 + 离线队列**。存档层(Redis 历史/MySQL)没有撤回墓碑,重新 `GET_HISTORY` 仍会拿到原文。要做到"撤回后原文不可再被拉回",客户端自己维护 `message_id` 撤回名单并在拉历史时过滤(服务端墓碑已在后续批次排期)。
- 撤回(或版主删除)会**顺手回收**接收方离线队列里已入队的副本(按 `message_id`),所以对方下次登录不会再收到这条原文;需要给"已撤回但对方从未见过"的消息留痕的客户端,得自己在撤回成功时记账。
- **坑二(`BULK_DELETE` 不受窗口约束)**:批量删除是治理接口(只删自己的或版主有权删的),不查撤回窗口、也不回收离线副本。需要严格窗口语义的客户端不要用它做撤回。
- **形态差异**:整个编辑/删除/撤回面(2225-2232)目前只在 **basic 形态**(`services/shared/chat/src/main.cc` 的直连入口)接线;enhanced 形态(`main_enhanced.cc` 走 `DispatchDistributedPacket`)没有这些 handler,请求会**超时**而不是回错码。跑 MySQL/Redis 的增强部署前先确认这一点(或把撤回挪到别的 RPC 面)。

## 会话与连接

### 心跳是客户端的责任

- 服务端**不会**主动踢静默连接(chat 平面当前没有连接级 idle 计时器):不发 ping 也能挂着,TCP 半开连接要等到内核 keepalive/写失败才暴露。
- 官方 SDK 的默认:25 秒发 `HEARTBEAT_PING`,pong 必须回显 ping 的非零 sequence 才算已答;连续 2 次未答判定连接死亡 → 进入自动重连。
- **裸协议接入方必须自己实现等价逻辑**,否则断网后客户端会停留在"假在线"状态收不到任何消息,直到下一次写失败。

### 顶号(KICK)语义

- 同 `(user_id, device_id)` 重新登录会顶掉旧会话:旧连接收到 `KICK_NOTIFY`(reason 如 "login from another device"),新连接 `LOGIN_RESP.kick_previous = true`。
- 同用户**不同设备**共存,各自收消息(App 平面多端语义)。
- **坑**:收到 `KICK_NOTIFY` 后不要再重连——顶号是终态,重连+重登会无限互相顶。官方 SDK 对 KICK 不再自动重连,自研客户端要遵守同一约定。

### 断线重连(官方 SDK 默认)

- 指数退避 500ms → 15s,±20% 抖动,默认无限重试(`max_reconnect_attempts = -1`);显式 `Disconnect()`/`Logout()` 取消重连。
- 重连成功后**必须重新 LOGIN**——连接与认证是两个状态(`Connected` ≠ `LoggedIn`),重连只回到 `Connected`。
- **坑**:重连窗口内发出的请求立刻失败(`NotConnected`/`Closed`);SDK 单请求默认 10 秒超时,业务侧要有"重连后补发"的语义(本地队列或 UI 重试),SDK 不代发。

### fire-and-forget 发送后不要立刻断开

- 便捷版 `SendMessage(receiver, content)` 是 fire-and-forget:发送被 post 到 SDK 内部 io 线程,**不等到回执**。
- **坑**:发送后马上 `Disconnect()`(或进程退出)会停掉 io_context——还排在队列里的发送 lambda 被直接丢弃,消息在**客户端侧无声消失**,服务端零痕迹、零日志。高负载/慢机器上窗口更容易被踩中,表现为"偶现丢消息"且无法复现。
- 需要确认送达或即将退出时,用带回执的类型化重载,等 `SEND_MESSAGE_RESP` 回调到达后再断开:

```cpp
SendOptions opts;                      // channel_type/receiver_id/...
client.SendMessage(opts, content,
  [&](const std::error_code& ec, const chirp::chat::SendMessageResponse& resp) {
    // 走到这里说明请求已真实发出并收到服务端应答,此时断开是安全的。
  });
```

- 别忘了 `resp.code()` 的两形态分歧:私聊发给离线用户时 basic 形态回 `TARGET_OFFLINE`(6,已入离线队列),enhanced 形态回 `OK`——两者都算发送完成。

### 回调线程

- C++ SDK 所有回调在**内部 io 线程**触发。回调里直接碰 UI/引擎对象会崩;引擎适配层负责派发回游戏线程(Unreal 壳用 `AsyncTask`,Unity 壳内已 marshal)。

## 业务码与传输错误是两回事

- SDK 的 `ec`(传输层:`NotConnected`/`Timeout`/`Closed`/`Kicked`/`BadResponse`)== OK **不代表业务成功**;必须再读 `resp.code()`(上表错误码)。
- 反过来,`resp.code() != OK` 时 `ec` 也是 OK——传输没问题,是服务端业务拒绝。

```cpp
client.SendMessage(opts, "gg", [](const std::error_code& ec,
                                  const chirp::chat::SendMessageResponse& resp) {
  if (ec) { /* 传输层:重连后补发 */ return; }
  switch (resp.code()) {
    case chirp::common::OK:             Use(resp.message_id()); break;
    case chirp::common::CONTENT_TOO_LONG: PromptTooLong(); break;   // 提示精简,别重发
    case chirp::common::RATE_LIMITED:   ScheduleRetry(); break;     // 等窗口,别立刻重发
    case chirp::common::WORD_FILTERED:  PromptFiltered(); break;    // 改文案
    default:                            LogAndDrop(); break;
  }
});
```

## 跨平面(伴侣 App [游戏]

- App 玩家回复游戏频道:向 app_chat 发消息时,目标频道写成 **`<game_id>:<裸频道id>`**(如 `game_zx:world`),hub 会路由到对应游戏的 game_chat 注入。前缀写错 → `INVALID_PARAM`(玩家没绑定该游戏)或 `SERVER_UNAVAILABLE`(该游戏无在线 spoke)。
- 该路径**消耗发送预算**(在发送限流之后拦截),与普通发送共享模糊闸/节奏。
- 扇回 App 玩家的是无前缀私聊副本,不会构成回环。

## 服务端接入(游戏后端 [5xxx]

- 拨**两个**连接:`chirp_game_server_gateway`(8100,注入/事件)与 app_chat 主端口(7000,身份绑定/频道订阅/未读)。RPC 发错目标会因对端不处理而**超时**,不是报错——这也是"卡 10 秒才失败"的最常见原因。
- 事件投递是 **at-least-once**:处理完 `EVENT_DELIVER_NOTIFY` 必须回 `EVENT_ACK_REQ`,否则 hub 重连后重投——消费逻辑要按 `event_id` 幂等。
- 注入消息带 `inject_id` 幂等键:同 id 重发不会产生重复消息,重试安全。
- 注入回 `OK` 只表示服务平面受理,不确认玩家侧送达;离线玩家走离线队列。
- 认证被拒表现为周期性重试告警(固定延迟,无限重试),不会主动退出——凭据配错要盯启动日志。
- Go SDK 语义与 C++ 参考实现逐项对齐,细节见 [服务端 SDK](../sdk/server.md)。

## 快速自查清单

接入联调时按这个顺序过一遍,能覆盖 90% 的"消息 mysteriously 丢失":

1. `LOGIN_RESP.code == 0` 了吗?(token 签发方/密钥/算法一致吗——游戏平面 HS256,`--token_secret` 两端同值)
2. 心跳在发吗?pong 的 sequence 回显校验了吗?
3. 发送失败读的是 `resp.code()` 还是只看了 `ec`?
4. `RATE_LIMITED` 是节奏(等窗口)还是模糊闸(退避)?重试逻辑区分了吗?
5. `TARGET_OFFLINE` 被当成失败处理了吗?
6. 拉黑/屏蔽了对方吗?(`GET_BLOCKED_SENDERS` / `GET_CHANNEL_MUTES` 查一下)
7. 频道前缀 `<game_id>:` 写对了吗?玩家绑定关系建了吗?
8. 服务端事件 ack 了吗?`inject_id` 幂等键带了吗?
9. 撤回入口的可见性按服务端规则算了吗(发送者本人 + 窗口内 + 可撤回频道),失败回码区分了吗?

## 相关文档

- [API 总览](../api/overview.md)——帧格式、msg-id 总表、错误码总表
- [服务器平面](../server_plane.md)——5xxx 契约与语义
- [peer 注册协议](../api/peer_protocol.md)——跨平面 spoke/hub 握手
- [SDK 钩子接口](../design-notes/sdk_hooks.md)——拦截器/存储/认证提供方
- [游戏聊天功能清单](../design-notes/game_chat_features.md)——功能设计原案
