# Web 伴侣 App(apps/web_companion)

Status: **Experimental** —— 一期已交付:登录、私聊、群组、好友与在线状态。走**过渡路径**(浏览器直连 chat 7001 与 social 8001 的 WS 边缘),不是最终拓扑;统一边缘依赖 P1 登录语义统一(见 [TODO.md](../TODO.md))。

## 是什么

浏览器端 Discord 式伴侣 App(Vite + React 18 + TypeScript strict + MUI 5,中文界面)。与 `apps/admin_dashboard` 同栈,但数据层全真实:两条 WebSocket + Protobuf 二进制帧,无 mock。

| 能力 | 状态 | 说明 |
| --- | --- | --- |
| 登录/互踢 | ✅ | scaffold 模式(token 即 user_id);`--token_secret` JWT 模式同样可登;同账号新设备登录踢旧会话(KICK 弹回登录页) |
| 私聊 | ✅ | 实时收发、历史分页、已读回执、typing、emoji reaction、编辑/删除、未读角标、离线回放 |
| 群组 | ✅ | 建群、添加成员、踢人、退群、群成员名单、群聊(全部经 notify 同步) |
| 好友 | ✅ | pending 制加好友、接受/拒绝、好友列表(localStorage 补位,服务端无列表 API) |
| 在线状态 | ✅ | 上下线徽标(绿点)、状态广播、批量拉取 |
| 语音/组队 | ❌ | 二期,依赖后端补齐(见 roadmap) |

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
npm test -- --coverage        # 180 例;门槛:全局 ≥70%,src/protocol ≥90%
npm run build

# E2E:脚本起真实 chirp_chat + chirp_social 再跑集成套件(9 例)
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

`protocol/` 刻意保持零 React 依赖——它是将来 Flutter 三端用 Dart 重写的蓝本。proto 生成物提交在 `proto/ts/`(ts-proto,`gen_proto.sh` 生成),运行时零工具链依赖;CI 有 proto-sync job 防 `.proto` 与生成物漂移。

## 必须知道的协议语义(代码注释里也有)

1. **发送方无自回声**:自己的消息以 SEND_RESP 上屏,按 message_id 与实时推送去重合并。
2. **TARGET_OFFLINE ≠ 发送失败**:消息已入对端离线队列并触发推送,UI 按"已送达(对方离线)"渲染。
3. **KICK 后必须停自动重连**,否则双端互踢乒乓;客户端置 `kicked` 弹回登录页。
4. **服务端断线不广播 OFFLINE**(social `main.cc` 只更新内存表):presence 靠客户端 70s TTL 过期兜底渲染离线。
5. **FRIEND_ACCEPTED_NOTIFY 只发请求发起者,且 user_id 填发起者自己**:客户端忽略 self echo(否则会"加自己为好友"),接受方的好友记账由 ACTION_RESP 驱动本地完成;**发起者的 roster 缺口**(接受成功但发起者好友列表不更新)是服务端缺陷,列入 social 后端补齐。
6. **GET_FRIEND_LIST(3007)/ REMOVE_FRIEND(3005) 服务端无 handler**:好友列表用 localStorage 按 user_id 持久化补位,权威版等后端补齐。
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
   ├─→ [后端补齐 A:social 可用化] GET_FRIEND_LIST/REMOVE_FRIEND/BLOCK 系实现、好友落库、
   │    ACCEPTED notify 字段语义修复、presence 断线广播、JWT 对齐
   ├─→ [后端补齐 B:voice 媒体面] SDP 定向中继、TURN、mute/deafen、信令认证
   ├─→ [后端补齐 C:party 协议] proto 7xxx 段从零定义(当前组队完全没有协议)
   ├─→ [二期·Flutter 三端] Android/iOS/Windows,协议层以 src/protocol/ 为蓝本纯 Dart 重写
   └─→ [三期] 语音 + 组队客户端(Web 与 Flutter 同步)
```

迁移到 app_gateway 聚合边缘后,本文档的"直连 chat/social"过渡路径即废弃,页面与 store 层基本不动,只换连接装配。
