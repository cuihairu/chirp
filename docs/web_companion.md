# Web 伴侣 App(apps/web_companion)

Status: **Experimental** —— 一期已交付:登录、私聊、群组、好友与在线状态。走**过渡路径**(浏览器直连 chat 7001 与 social 8001 的 WS 边缘),不是最终拓扑;统一边缘依赖 P1 登录语义统一(见 [TODO.md](../TODO.md))。

## 是什么

浏览器端 Discord 式伴侣 App(Vite + React 18 + TypeScript strict + MUI 5,中文界面)。与 `apps/admin_dashboard` 同栈,但数据层全真实:两条 WebSocket + Protobuf 二进制帧,无 mock。

| 能力 | 状态 | 说明 |
| --- | --- | --- |
| 登录/互踢 | ✅ | scaffold 模式(token 即 user_id);`--token_secret` JWT 模式同样可登;同账号新设备登录踢旧会话(KICK 弹回登录页) |
| 私聊 | ✅ | 实时收发、历史分页、已读回执、typing、emoji reaction、编辑/删除、未读角标、离线回放 |
| 群组 | ✅ | 建群、添加成员、踢人、退群、群成员名单、群聊(全部经 notify 同步) |
| 好友 | ✅ | pending 制加好友、接受/拒绝、删除、拉黑;好友列表**服务端权威**(登录拉取 + notify 同步) |
| 在线状态 | ✅ | 上下线徽标(绿点)、状态广播、批量拉取;服务端断线即广播 OFFLINE |
| 语音/组队 | ◐ | 后端信令面已就绪(认证门/TURN 凭据/mute-deafen/超时清理,见 roadmap B;组队 invite-accept/ready check,见 roadmap C);客户端 WebRTC 媒体面与组队 UI 属二/三期 |

两条连接彼此独立、可降级:social 断开时聊天完全可用,好友入口隐藏。

## 运行

```bash
# 1. 后端(内存模式即可)
./build/services/chat/chirp_chat --port 7000 --ws_port 7001 &
./build/services/social/chirp_social --port 8000 --ws_port 8001 &

# 2. 前端(npm workspaces:仓库根安装依赖)
npm ci
cd apps/web_companion && npm run dev    # http://localhost:3001
```

开发代理(`vite.config.ts`)把 `/ws/chat` → 7001、`/ws/social` → 8001,页面同源无 CORS。直连后端(生产/绕过代理)用 `VITE_CHAT_WS_URL` / `VITE_SOCIAL_WS_URL` 指向真实地址;反代部署时同样以这两个变量指到 wss:// 入口。

### 测试

```bash
cd apps/web_companion
npm run typecheck && npm run lint
npm test -- --coverage        # 182 例;门槛:全局 ≥70%,src/protocol ≥90%
npm run build

# E2E:脚本起真实 chirp_chat + chirp_social 再跑集成套件(11 例)
bash scripts/web_smoke.sh
```

## 分层(自底向上)

```
src/
├── protocol/    # 与框架无关:帧编解码 / msg_id↔消息映射 / 错误码 / ChirpClient 状态机
├── api/         # chat_api.ts、social_api.ts:各持一条连接,把 notify/resp 翻译成 store 操作
├── state/       # createStore + useSyncExternalStore 的薄 store(auth/conversation/message/typing/presence/friend)
├── pages/       # LoginPage、ChatPage
└── components/  # ConversationList、ChatWindow、MessageBubble、FriendsDialog、GroupDialogs…
```

`protocol/` 刻意保持零 React 依赖——它是将来 Flutter 五端(Android/iOS/macOS/Windows/Linux)用 Dart 重写的蓝本。proto 生成物提交在 `proto/ts/`(ts-proto,`gen_proto.sh` 生成),运行时零工具链依赖;CI 有 proto-sync job 防 `.proto` 与生成物漂移。

## 必须知道的协议语义(代码注释里也有)

1. **发送方无自回声**:自己的消息以 SEND_RESP 上屏,按 message_id 与实时推送去重合并。
2. **TARGET_OFFLINE ≠ 发送失败**:消息已入对端离线队列并触发推送,UI 按"已送达(对方离线)"渲染。
3. **KICK 后必须停自动重连**,否则双端互踢乒乓;客户端置 `kicked` 弹回登录页。
4. **roster 服务端权威**:social 登录后拉 GET_FRIEND_LIST + GET_PENDING_REQUESTS 灌本地镜像;之后完全靠 notify 同步(3022 进件、3023 ACCEPTED 双向、3024 REMOVED)。本地不再持久化好友(localStorage 已退役),刷新即从服务端重建。
5. **ACCEPTED notify 发给双方、user_id 填对端**:两边各自把对端记入 roster;客户端忽略 self echo(纯防御,现服务端不可能发)。
6. **REMOVE_FRIEND/BLOCK 对称且幂等**:服务端删双向关系并推 3024 给对端;BLOCK 额外绝交并清 pending。重复 REMOVE/BLOCK 仍回 OK。
7. **pendingOut(我发出的请求)刷新即丢**:服务端只有进件查询(GET_PENDING_REQUESTS 只回 to_user_id==me),outgoing 靠 ADD_RESP 本地记,属已知局限。
8. **未读数无服务端递增路径**(直连 chat 无 GET_UNREAD_COUNT 的维护面):未读角标为本地计数。
9. **后台标签心跳被节流**:监听 `visibilitychange`,回前台立即补心跳;心跳死链判定 2×(25s+10s)。
10. **scaffold token=user_id 仅限开发**;生产起 chat **与 social、voice** 时加 `--token_secret`(同一 secret,voice 的该开关同时是信令认证门),登录页选 JWT 模式。
11. **social 按单实例部署使用**:roster/pending/黑名单经 Redis write-through 持久化(`--redis_host` 启用,重启自动恢复;不带则纯内存),但 presence 与会话表在实例内存,多实例间无 fan-out——好友落在两台实例上时在线广播与进件推送不通。
7. **未读数无服务端递增路径**(直连 chat 无 GET_UNREAD_COUNT 的维护面):未读角标为本地计数。
8. **后台标签心跳被节流**:监听 `visibilitychange`,回前台立即补心跳;心跳死链判定 2×(25s+10s)。
9. **scaffold token=user_id 仅限开发**;生产起 chat 时加 `--token_secret`,登录页选 JWT 模式。

## CI

`.github/workflows/web.yml`(paths 过滤 `apps/web_companion/**`、`proto/**`,C++ 推送不会空跑):

- **test**:node 24(与开发环境一致)→ `npm ci`(仓库根,workspaces)→ typecheck → lint → vitest --coverage(阈值硬卡)→ build。
- **proto-sync**:重跑 `gen_proto.sh` 后 `git diff` 校验 `proto/ts` 无漂移(protoc 版本注释行除外)。

## Roadmap

```
[一期·已完成] 本文档:Web 伴侣 App(登录/私聊/群组/好友/在线状态)
   ├─→ [后端补齐 A:social 可用化·已完成] GET_FRIEND_LIST/REMOVE_FRIEND/BLOCK 系实现、
   │    Redis 持久化、ACCEPTED 双向 notify、presence 断线广播、JWT 对齐
   ├─→ [后端补齐 B:voice 媒体面·已完成] SDP 定向中继(offer/answer/candidate)、TURN(coturn REST
   │    短期凭据,join 应答带 ice_servers)、mute/deafen 独立双布尔、LOGIN 信令认证、心跳超时踢人;
   │    客户端媒体面(WebRTC 接入)仍属三期
   ├─→ [后端补齐 C:party 协议·已完成] 7xxx 段 + services/party(TCP 7500/WS 7501):邀请-接受制入队
   │    (无邀请码、重复邀幂等)、ready check、leader 离开/断线继位、最后一人静默解散;成员快照 Redis
   │    write-through,邀请纯内存(10min 懒过期);断线即离队(reason="offline"),通知达目标全设备;
   │    客户端组队 UI 仍属三期
   ├─→ [二期·Flutter 五端] Android/iOS/macOS/Windows/Linux,协议层以 src/protocol/ 为蓝本
   │    纯 Dart 重写;五端共享同一套界面代码,每端的增量只在构建矩阵与签名发布。
   │    Flutter 版不接管 Web(React 版已交付,两套 Web 客户端无收益)
   └─→ [三期] 语音 + 组队客户端(Web 与 Flutter 同步)
```

迁移到 app_gateway 聚合边缘后,本文档的"直连 chat/social"过渡路径即废弃,页面与 store 层基本不动,只换连接装配。
