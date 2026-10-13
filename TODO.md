# Chirp 任务清单

> 需求清单，一条一句。完成项括号内为证据（commit + 测试数）；过程叙述见 git log。
> 详细特征：[游戏聊天特征](docs/design-notes/game_chat_features.md)；SDK 兼容性：[docs/design-notes/sdk_compatibility.md](docs/design-notes/sdk_compatibility.md)。


## 游戏平面（P0）


### game_sdk_gateway（原 gateway）

- [x] 基础登录/心跳/踢出/会话 claim
- [x] ChatBridge 转发 2xxx 到 game_chat
- [x] 修复 scaffold 分支不 bind session 的 bug
- [x] 统一 TCP/WS 两份 switch 为 `HandleClientPacket`
- [x] 多端在线策略（24 例）

### game_chat（原 chat，部署为游戏平面实例）

- [x] 基础聊天：私聊、群组、已读回执、正在输入、表情回应、消息编辑/删除、@提及
- [x] 历史、离线队列、可选 Redis/MySQL 存储
- [x] 本地验证 token（`--token_secret`，HS256 JWT）
- [x] trusted peer 信任门（`--gateway_service_secret`，gateway 管道免限流）
- [x] 注册协议
- [x] 频道消息推送
- [x] 接收玩家回复
- [x] 敏感词过滤（11 例）
- [x] 消息长度限制（8 例）
- [x] 词库下发协议（3/3）
- [x] 发送频率限制（8 例）
- [x] enhanced 直连入口补每用户发送模糊闸
- [x] 重复消息检测（6 例）
- [x] 频道屏蔽（6 例）
- [x] 消息撤回（6 例）
- [x] 撤回回收离线队列副本（4 例）
- [x] 撤回墓碑贯通历史存档（0b80b5f）
- [x] enhanced 形态接入撤回端点（4 例）
- [x] 黑名单（5 例）

### game_server_gateway（原 server_gateway，瘦身版）

- [x] 服务凭证认证（`SERVER_AUTH_REQ`）
- [x] 消息注入（`INJECT_MESSAGE_REQ` → `InjectMessageNotify`）
- [x] 事件下发（`EVENT_PUBLISH_REQ` / `EVENT_DELIVER_NOTIFY` / `EVENT_ACK_REQ`）
- [x] Redis Streams broker 回退
- [x] 搬走 WP-8 功能（53 例）
- [x] 瘦身后验证（35/35）

## App 平面（P1，游戏平面稳定后开始）


### app_sdk_gateway（原 app_gateway）

- [x] 基础登录/心跳/踢出/会话 claim
- [x] 6xxx 设备消息转发到 app_notification
- [x] ChatBridge 转发 2xxx 到 app_chat
- [x] 对接 app_auth + 自服务链切到 app_chat
- [x] 游戏在线状态自服务开关（5031/5032）

### app_chat（原 chat，部署为 App 平面 hub）

- [x] 基础聊天能力（与 game_chat 同一二进制）
- [x] hub 模式（9edaca3，53 例）
- [x] 身份映射
- [x] 频道订阅
- [x] 跨平面 fan-out
- [x] 跨平面回复
- [x] 未读计数
- [x] 游戏在线状态 + 好友消息进游戏（96 例）
- [x] 离线推送触发
- [x] enhanced 会话语义修复（12 例）

### app_auth（原 auth）

- [x] 基础 token 验证
- [x] enhanced 模式（MySQL + libsodium）
- [x] 确认只服务 App 平面，game 平面不依赖
- [x] PostgreSQL 存储后端

### app_notification（原 notification）

- [x] 设备注册/注销/token 更新/查询
- [x] 推送协议面（6xxx）
- [x] 修复 namespace 重命名后的构建问题（333/333）
- [x] 真实 APNs/FCM 投递（9 例）

## 跨平面协议（P1）

- [x] 定义 peer 注册协议
- [x] 定义频道消息协议
- [x] 能力位定义
- [x] 版本协商实现（9edaca3）
- [x] 核心抽象收敛为 Communication
- [x] request_id 与 sequence 分离（d70f78b）
- [x] ActorKind 枚举收敛（44/44）

### 收敛路线任务序（communication_core.md §5，P0-P2 转正式任务）

- [x] P0-1 Delivery 语义词汇进 api/overview（9a2ac4a）
- [x] P0-2 网关边界钉进协议文档
- [x] P0-3 social/voice/search 标注非核心
- [x] P1-4 request_id 剩余面（9f92ba9，44/44）
- [x] P1-5 delivery_id 进投递/回执协议 + dedup（AT_MOST_ONCE）落地
- [x] P1-6 Identity binding 文档化
- [x] P2 明确不做

## 构建与验证（P0）

- [x] 目录重构：`services/game/`、`services/app/`、`services/shared/`
- [x] 二进制重命名：`chirp_game_sdk_gateway`、`chirp_game_server_gateway`、`chirp_app_sdk_gateway`、`chirp_app_auth`、`chirp_app_notification`
- [x] proto 包重命名：`chirp.game_server_gateway`、`chirp.app_notification`
- [x] 更新 smoke test
- [x] 更新 CI
- [x] 更新单元测试（34/34）
- [x] 全量构建验证（333/333）
- [ ] dependabot #18 glib（medium，upstream-blocked）
- [x] dependabot #21 katex（a32be7e）
- [x] dependabot #20/#22 source-map-js（117/117）
- [x] dependabot #6 source-map-js（b343d6f，53/54）
- [ ] dependabot 框架大批次 2026-10-12/13（actions 5 + npm/docker/cargo 17）——四项真实破损修复全落地，合并决策待用户
- [x] dependabot #13 dompurify
- [x] Deploy Docker 演示账号 18 连败修复（1e2d034）
- [x] auth 注册面空 email 撞唯一键
- [x] 崩溃采集批
- [x] crashpad_handler 打包进 install/nightly
- [x] crashpad 批 CI 连环红修复（b6d3300，44/44）
- [x] smoke-edge 偶发 chirp_chat 启动 Aborted 修复（6b3a03d）

## 接入方支持（2026-09-25 批次）

- [x] 接入避坑指南
- [x] API 总览重写
- [x] C++ 接入示例
- [x] 压测工具
- [x] TS SDK 幽灵依赖修复
- [x] smoke-sdk 竞态修复
- [x] 测试环境鲁棒性加固
- [x] 覆盖率豁免行重锚定（8252/8252）
- [x] smoke 断言两形态语义对齐

## 文档（P2）

- [x] architecture.md 全文中文，反映新架构
- [x] README.md 更新拓扑图和服务表
- [x] CORE.md 更新架构说明
- [x] server_plane.md 更新二进制名
- [x] 设计并集成 logo
- [x] CAPABILITY_MATRIX.md 更新服务名和路径
- [x] 补充 peer 注册协议的详细文档
- [x] 文档与源码一致性审计（da0262e）
- [x] 专项审查批：消息投递/认证授权/跨实例状态一致性（44/44）

## 覆盖率缺口审计（2026-09-27 批次，#4）

- [x] 六文件 27 条未覆盖分支臂逐条判定（7 例）
- [x] 覆盖率批次5：单文件最大缺口 `hybrid_message_store`（17 臂/7 行）逐臂判定（514/546）
- [x] 覆盖率批次6：notification 两文件 14 行 21 臂逐臂判定（9/8）
- [x] 覆盖率批次7：shared/chat 残余缺口 8 行 20 臂 + 1 未覆盖函数逐臂判定（40/40）
- [x] 覆盖率批次8：陈旧整行豁免钉审计——75 钉降格迁移（113/119）
- [x] 覆盖率批次9：门禁 universe 外最大缺口——`services/shared/chat/src/main_enhanced.cc` 补单测（122/617）
- [x] 覆盖率批次10：门禁 universe 外次大缺口——`services/shared/chat/src/main.cc`（basic chat 直连形态）补单测（0/893）
- [x] 覆盖率批次11：门禁 universe 外缺口——`services/app/auth/src/main_enhanced.cc`（enhanced auth 入口）补单测（9/10）
- [x] 覆盖率批次12：门禁 universe 外缺口——`services/party/src/main.cc`（party 服务装配文件）补单测（47 例）
- [x] 覆盖率批次13：coverage-gaps 台账最后一个已知缺口——`libs/network/chat_peer_hub.cc:319` 竞态族臂收口（1/2）
- [x] 覆盖率批次26：web 平面 URL 解析臂——`src/api/services.ts` 五个 `resolve*WsUrl` 补测（1ef92fd，3 例）
- [x] 覆盖率批次25：全仓覆盖率台账复查轮——四面普查 + web 未登记最大缺口 ChatPage.tsx 补测（505/505）
- [x] 覆盖率批次24：Unity C# 网络胶水层 wrapper 测试 harness + 单测（11 例）
- [x] 覆盖率批次23：chat 三形态 main* 进程级 e2e 补单测（44/44）
- [x] 覆盖率批次22（main* 台账收官）：notification `main.cc` + search `main.cc` 补单测，npc_dialog"3"判定收口（11/20）
- [x] 覆盖率批次21：门禁 universe 外缺口——`services/shared/chat/src/main_distributed.cc`（distributed chat 直连形态装配文件）补单测（16 例）
- [x] 覆盖率批次20：门禁 universe 外缺口——`services/app/auth/src/main.cc`（basic auth 入口）补单测（0/98）
- [x] 覆盖率批次19：门禁 universe 外缺口——`services/app/sdk_gateway/src/main.cc`（app 边缘装配文件）补单测（30 例）
- [x] 覆盖率批次18：门禁 universe 外缺口——`services/voice/src/main.cc`（voice 服务装配文件）补单测（61 例）
- [x] 覆盖率批次17：门禁 universe 外缺口——`services/game/sdk_gateway/src/main.cc`（game 边缘装配文件）补单测（6 例）
- [x] 覆盖率批次16：门禁 universe 外缺口——`services/social/src/main.cc`（social 服务装配文件）补单测（ae18237，37 例）
- [x] 覆盖率批次15：门禁 universe 外缺口——`services/game/server_gateway/src/main.cc`（server-plane hub 装配文件）补单测（0/207）
- [x] 覆盖率批次14：chat_peer_hub 豁免臂组 119 `(2,4,5)` / 420 `(10,11)` 逐臂可触达性分析收口（15/15）
- [x] 维持复核（ee8e2cc，8591/8591）

## 前端组件测试（2026-09-28 批次，admin_dashboard vitest 全绿）

- [x] apps/admin_dashboard 接入 vitest + @testing-library/react（25 例）
- [x] 文档站 GitHub 图标入口修复
- [x] CI 红腿两处修复

## 移动端原生技术栈迁移（2026-09-28 启动，Android M1+M2 完成）

- [x] Android M1 — 原生协议核心（Kotlin/JVM）（16 例）
- [x] Android M2 — 连接层收口（OkHttp Transport + 自动重连，可门禁子集）（28 例）
- [x] Android M3 — 壳层
- [x] Android M4 — chat 管线
- [x] Android M3.5 — 推送接入（80/0）
- [x] Android 接收侧（103/103）
- [ ] iOS 推送（APNs）——真投递凭据项
- [x] iOS — Swift 协议包
- [x] iOS — Swift 协议包（70/70）
- [x] iOS — Swift 协议包（M3.5 核：DeviceRegistrar + 推送 token 缝，2026-09-29）（77/77）
- [x] iOS — 聊天伴侣 App P1（53951c8，101/101）
- [x] iOS — 伴侣 App P2（112/112）
- [x] iOS — 伴侣 App P3（119/119）
- [x] iOS — 伴侣 App P4a（134/134）
- [x] iOS — 伴侣 App P4b（151/151）
- [x] iOS — 伴侣 App P4c（167/167）
- [x] iOS — 伴侣 App P4d（182/182）
- [x] iOS — 伴侣 App P4e（2118/2119）
- [x] iOS — 伴侣 App P4（游戏内聊天集成+功能面对齐 web_companion）
- [x] iOS — 伴侣 App P5（214/214）
- [x] iOS — 伴侣 App P6（221/221）
- [x] HarmonyOS — ArkTS（fe17f8d）
- [ ] 手机端原型图——二审中
- [x] darwin/windows 编译证明腿（1101a4b）
- [x] 每日构建后续——四腿矩阵 + 桌面打包腿 + 安装器全平台放开（a6e7bf0）
- [x] 收尾（4 例）

## 实验性服务（暂不动）

- [ ] social/voice/party/search 服务保持原样，按需迁移到对应平面目录（social 已接游戏在线状态事件；voice/party 已接 smoke 与 web 伴侣；search 未动）

## 近期批次（2026-10-08 起）

- [x] search 接线协议形态——已拍板（226d912，2248/2249）
- [x] 群管理员手动禁言（game_chat_features P1）服务端+单测落地（6b86f63，2250/2251）
- [x] 世界频道等级门槛（game_chat_features P1）服务端+单测落地（b5472bd，12/13）
- [x] web_smoke chat 腿 2 例行为性既有失败收口——enhanced 群频道成员门 + e2e 节奏契约（2 例）
- [x] 好友上线通知（可关闭）：SET/GET_NOTIFY_PREFS 3025-3028 + FRIEND_ONLINE_NOTIFY 3029（OFFLINE→非 OFFLINE 跃迁补发、逐收件人过偏好、缺省全开、Redis 持久化）+ web/desktop toast 与开关（0819100 social_tests 62/62 + ctest 44/44；ef12186 915f993 6f75e5c）
- [x] 消息引用（game_chat_features P1）web/desktop 消费收口（2fc37f7，269/269）
- [x] 文档一致性审计收尾
- [x] RedisClient 同步连接池/复用（6/7）
- [x] hybrid store 启动 PING 有限重试（4 例）
- [x] iOS/Android/鸿蒙 填空补齐：alias/search/mute/level 四端对齐（1e26780，2122/2123）
- [x] 鸿蒙 GroupPage 群管 UI + ChatPage 别名署名
- [x] 站点 530 已恢复——cloudflared 陈旧边缘地址（5a88792，530/1033）
- [x] 举报（report_message，game_chat_features P1 用户行为）：REPORT_MESSAGE_REQ/RESP 2253/2254——玩家举报消息，服务端内存台账 + 按 (message_id, reporter_id) 去重幂等，回 report_count；管理后台审核队列待全局管理员面另批（同群域禁言范围裁决）。落地 2026-10-13：ReportLedger（内存台账，理由封顶 100 条/消息）+ ReportHandlers（未登录 AUTH_FAILED / 空 message_id INVALID_PARAM / 理由超 500 码点 INVALID_PARAM / 台账外消息 USER_NOT_FOUND，与撤回同语义）+ basic/enhanced 双形态派发；SDK 四端（C++ core ReportMessage、sdks/ts 与 apps/shared/protocol 双包 reportMessage、C# ReportMessageAsync）；web 气泡举报入口（Flag 按钮 + confirm）+ desktop 同形入口。测试：ctest 44/44（sdk_core 131）、sdks/ts 135、app-protocol 125、web 282、dotnet 126 全绿，desktop tsc+vite build 绿。期次 P1 下批
