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
- [x] dependabot #18 glib（medium）——告警 2026-10-13 由 cuihairu 以 tolerable_risk 驳回闭环（glib 0.15-0.20 Iterator unsoundness，Tauri 传递依赖，upstream 无 patched 版本，维持驳回）
- [x] dependabot #21 katex（a32be7e）
- [x] dependabot #20/#22 source-map-js（117/117）
- [x] dependabot #6 source-map-js（b343d6f，53/54）
- [x] dependabot 框架大批次 2026-10-12/13（actions 5 + npm/docker/cargo 17）——四项真实破损修复全落地；全部合入 main：#17/#21/#24（2cac305/65a47a2/50a77a7）+ #16/#18（4de3963/18f10e7，已代决 巡检 chrp41b：vite 8.0.6→8.3.4 同主版本、@types/react-dom ^19→^19.3.0 仅类型定义，均非 major 跳变；#18 codecov/project 红为基线 0.02% 四舍五入漂移且 patch 本身 Coverage not affected，无实质风险）；合入后 merge-main 修 #34 冲突（85a8af9：pnpm 世界删两份 npm 锁、web manifest 取 vite ^8.3.4 并集），pnpm12 三锁 frozen 全过，web 304/protocol 126/sdk 136/desktop 16/admin 25 全绿
- [x] dependabot #13 dompurify
- [x] Deploy Docker 演示账号 18 连败修复（1e2d034）
- [x] auth 注册面空 email 撞唯一键
- [x] 崩溃采集批
- [x] crashpad_handler 打包进 install/nightly
- [x] crashpad 批 CI 连环红修复（b6d3300，44/44）
- [x] smoke-edge 偶发 chirp_chat 启动 Aborted 修复（6b3a03d）
- [x] 构建链 npm 到 pnpm 迁移（38a35ff，本地全绿：sdk 136 / protocol 126 / web 290 / admin 25）

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
- [ ] iOS 推送（APNs）——真投递凭据项（保持挂起不代决；解锁条件：用户给 APNs 凭据——Team ID / Key ID / .p8 私钥 / Bundle ID——到位后落地 iOS 端真实投递联调。**2026-10-15 分批落地：ES256 provider JWT 签名侧已补**（`services/app/notification/src/apns_jwt.{h,cc}`——`.p8` PEM 载入（inline bytes 或 `--apns-key` 文件）、P-256 曲线校验、DER→JWS raw 签名转换；`NotificationService::ApnsBearerToken` 50 分钟窗口缓存 + `Authorization: Bearer` 注入；notification 单测 14 例含往返验签/拒 RSA/P-384/坏 PEM/缓存窗口/无凭据免头）**。余下卡点：真投递凭据（Team ID / Key ID / .p8 / Bundle ID，用户项）与 APNs 必需 HTTP/2（HTTP/1.1 传输，生产需前置协议转换）——凭据到位即可联调，无待补实现
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
- [x] 手机端原型图——二审通过（**已代决 FW1016D 2026-10-16，用户可推翻**——推翻即回滚本条并恢复 README/design-note 的「二审中」注记；原「保持挂起不代决」按 FW1016D 新授权（待拍板类可代决、事后可推翻）解除）。代决依据：01/02/04/05 四页用户一审已过（2026-10-07）；03 好友页修订稿（d115204）逐条对照审核意见核实——①来源游戏标签到位（描边弱化 chip「来自《XX》」，好友卡/请求卡/游戏好友视图三处一致，按 ID 添加的 frank 无来源不显示）；②来源筛选 chip 行到位（全部/各游戏，data-game 过滤，空来源行在具体游戏筛选下隐藏），「选筛选弃分组」裁决理由随稿（在线优先主序与按游戏分组正交冲突，筛选零重排）；③数据面注记如实（协议面暂无好友来源字段，落地形态待拍板、不擅自扩协议——不虚标能力）；④动作集与 FriendsDialog 同源不增不减；⑤两态截图已随 d115204 重出（750×1624 @2x）。随批状态更新：`design/prototypes/mobile/README.md` 与 `docs/design-notes/app_chat_prototype.md` 的「二审中/等用户审核」改「已代决通过」；「整体二审通过前不进文档站轮播、不在 README/能力矩阵引用」禁令随之解除——进轮播/加引用属新任务，本批未执行（不虚构任务）
- [x] darwin/windows 编译证明腿（1101a4b）
- [x] 每日构建后续——四腿矩阵 + 桌面打包腿 + 安装器全平台放开（a6e7bf0）
- [x] 收尾（4 例）

## 实验性服务（暂不动）

- [x] social/voice/party/search 服务保持原样，按需迁移（已代决 巡检 R1：实验服务维持顶层目录不迁移，P0-3 已标非核心、迁移无收益；2026-10-14 复核三条事实成立——social presence_manager 接游戏在线状态、voice/party 接 smoke 与 web 伴侣、search 仍在 services/search 未动）

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
- [x] 坐标链接（coord_link，game_chat_features P1 消息展示）：msg_type COORD_LINK = 15 + CoordMetadata 端到端激活。落地 2026-10-14：proto `COORD_LINK = 15` + `CoordMetadata{map_id,x,y,z,label}`；发送侧 `ValidateCoordMetadata`（垃圾 metadata/缺 map_id 回 INVALID_PARAM，与正文长度校验同层、落存储之前拦截，非 COORD_LINK 的 metadata 原样透传）；metadata 全链透传——basic 发送直存 ChatMessage、enhanced 经 ToMessageData/MessageData（离线队列+历史逐字段重建双臂）与 MySQL 冷层 BLOB 列（HEX/UNHEX 二进制安全、幂等 ALTER、旧库无列容忍）；SDK 四端 sendMessage 带 metadata（C++ core `SendOptions{msg_type,metadata}` 含本地存档、sdks/ts 与 apps/shared/protocol 双包 `SendOptions.metadata`、C# `SendOptions.Metadata`）；web/desktop 坐标卡片渲染（点击复制 "x, y[, z]"，零高度省略；web `MessageBubble` + desktop `ChatPanel` MessageRow，解析口径与服务端校验一致：畸形体不当卡片渲染）。测试：ctest 44/44（sdk_core 132、enhanced 34）、sdks/ts 136、app-protocol 126、web 288、dotnet 127 全绿，desktop tsc+vite build 绿。原生端（Swift/Kotlin/ArkTS）消费按移动端令留空档，不卡主线
- [x] P0 消息展示（走马灯/系统弹窗/气泡分色，game_chat_features P0 消息展示）：伴侣面收口 ChatMessage 的 priority/ttl_seconds/sender_kind 三字段。落地 2026-10-14：web/desktop `toView` 透传三字段；`systemSurfaceOf` 判定（HIGH→announce 先判、URGENT+ttl>0→marquee）+ `ChatApi.onSystemSurface` 单槽派发（不叠加、与通知权限和当前会话无关）；web `MarqueeStrip`（到点收起、换内容重置计时）+ `SystemAnnounceDialog`，desktop 同款挂 MainWindow 顶栏；气泡按 sender_kind 分色（系统黄/NPC 绿/服务橙，自己的消息恒主色，枚举外未知值回落玩家面色）。desktop 首次接入 vitest（vitest.config + test-setup + FakeChatConnection 桩 + `pnpm test`，nightly 桌面腿加 typecheck+test 门禁）。测试：web 304、desktop 16 全绿，web typecheck+lint+build 绿，desktop typecheck+build 绿。游戏内客户端自渲染（可点跳转）走 SDK 侧，另批
- [x] 物品链接（item_link，game_chat_features P0 消息展示——批内唯一未落地的 P0 项）：`msg_type = ITEM_LINK = 10` + `ItemMetadata` 消费面激活（proto 面既有，服务端校验+伴侣面渲染全缺）。落地 2026-10-14：发送侧 `ValidateItemMetadata`（解析不出或缺 item_id 回 INVALID_PARAM，坐标链接同层同口径，非 ITEM_LINK 的 metadata 原样透传；basic 在 `ValidateSendMessageRequest` coord 检查后串联、enhanced 在 reply 校验后独立回包块）；web/desktop `ItemView` 模型 + `itemViewOf`（镜像 coordViewOf：畸形体/缺 item_id 不当卡片）+ `sendMessage` 第 5 参 item + 物品卡片渲染（品质 0-4 分色 QUALITY_COLORS、count>1 显 ×N、item_id caption、`data-quality` 供测试断言）+ 详情弹窗（itemId/iconUrl/count/attrs 键值；web 走 i18n `itemDetails`，desktop 硬编码同文案）；未知品质值回落白。SDK 四端零改动（COORD_LINK 批的 metadata 通用管线直接承载）。测试：ctest 44/44 全绿（chat basic/enhanced 各 +2 ITEM_LINK 例：全链透传+校验拒绝），web typecheck + 310 例全绿（+6：api 3 + MessageBubble 3），desktop typecheck + 20 例全绿（+4：api 2 + ChatPanel 2）。原生端（Swift/Kotlin/ArkTS）消费按移动端令留空档，不卡主线
- [x] 成就分享（achievement，game_chat_features P1 消息展示）：`msg_type = ACHIEVEMENT = 12` + `AchievementMetadata` 消费面激活（proto 面既有，服务端校验+伴侣面渲染全缺）。落地 2026-10-14：发送侧 `ValidateAchievementMetadata`（解析不出或缺 achievement_id 回 INVALID_PARAM，坐标/物品链接同层同口径，非 ACHIEVEMENT 的 metadata 原样透传；basic 串联进 `ValidateSendMessageRequest` item 检查后、enhanced 独立回包块）；web/desktop `AchievementView` 模型 + `achievementViewOf`（镜像 itemViewOf）+ `sendMessage` 第 6 参 achievement + 成就卡片渲染（稀有度 0-4 分色 RARITY_COLORS、稀有度文案 RARITY_FACES、描述、iconUrl 图标、achievement_id caption、`data-rarity` 供测试断言）；未知稀有度值回落普通。SDK 四端零改动（metadata 通用管线直接承载）。测试：ctest 44/44 全绿（chat basic/enhanced 各 +2 ACHIEVEMENT 例：notify/历史/离线补投三臂全链透传 + 校验拒绝零投递零落库），web typecheck + 315 例全绿（+5：api 3 + MessageBubble 2），desktop typecheck + 24 例全绿（+4：api 2 + ChatPanel 2）。原生端消费按移动端令留空档，不卡主线
- [x] P0 特性面文档对账（game_chat_features P0 十六项已落地注记欠账，docs-only）：消息处理（敏感词/长度限制/发送频率/重复检测/@提及）+ 频道管理（世界/公会/队伍/私聊/系统公告/频道屏蔽）+ 消息展示（历史消息/消息已读）+ 用户行为（黑名单/在线状态/正在输入）十六项在源码中均已实现（限流 2026-09-14、提及 2026-09-21、敏感词/重复 2026-09-22、屏蔽/黑名单 2026-09-23，git log 考古取证）但缺状态注记——按 P0 优先级从上往下核对后补注记 + 差异裁决（敏感词三级落地为两级：Record 属服务端治理；长度限制拒绝不截断；频道屏蔽仅 WORLD/GUILD/TEAM 白名单、服务广播不可屏蔽；黑名单被拉黑者消息直接不投递、无「已屏蔽用户的消息」占位）；差异入表 docs/审计-文档一致性.md 新增「批次：2026-10-14」节（16 条 P2，无 P0）。docs-only 改动，门禁只跑 `cd docs && vitepress build`，未跑编译与测试套件
- [x] 表情/贴图（emoji，game_chat_features P1 消息处理——伴侣面形态）：16 个内置系统表情 + 消息文本 `[emoji:code]` 展开渲染。落地 2026-10-15：web_companion `components/emoji.ts` 与 desktop `ui/emoji.ts` 同文（`SYSTEM_EMOJIS` 16 个 + `expandEmojiCodes` 正则 `[a-z0-9_]+` 分片，未知 code 保留字面——向前兼容，与 sender_kind/品质/稀有度回落同口径）；web `MessageBubble` 内容 Typography 分片渲染（text span 无 testid、emoji span `emoji-${messageId}-${code}`）+ `MessageInput` 表情选择器（toggle + Popover 网格 8 列，点击向草稿插入 `[emoji:code]`）；desktop `ChatPanel` 同形（内容分片 + 输入区 toggle + Popover）。**服务端零改动**：表情走 TEXT 内容透传，长度/敏感词门照常生效；SDK 四端零改动。贴图（大图包）未做——待贴图资源与 metadata 载体（参照 ACHIEVEMENT 批形状）另批。测试：web typecheck + 320 例全绿（+5：MessageBubble 2 + 新文件 MessageInput 3），desktop typecheck + 26 例全绿（+2：ChatPanel 表情 describe 2）
- [x] P1 消息处理 + P0 搜索文档对账（game_chat_features 消息编辑/消息引用/消息搜索已落地注记欠账，docs-only）：三项在源码中均已实现（编辑 2026-09-18 2b83df3、引用 2026-10-11 2fc37f7、搜索 2026-10-08/10-10 226d912/7e7a190，git log 考古取证）但缺状态注记——补注记 + 差异裁决（编辑：文档原文「可编辑一次、编辑历史不保留」与实际不符，落地为 max_edit_count=100 次上限 + edit_history 保留随消息下发；搜索：频道过滤已落地 channel_id、时间范围无独立参数（before_* 是分页游标非区间过滤）、消息类型过滤额外支持；引用：与文档一致无差异）；差异入表 docs/审计-文档一致性.md 新增「批次：2026-10-15」节（2 条 P2 不符，无 P0/超前）。docs-only 改动，门禁只跑 `cd docs && vitepress build`
- [x] 交易消息（trade_status，game_chat_features P1 游戏特色——协议+消费面形态）：`msg_type = TRADE_STATUS = 14` + `TradeMetadata` 消费面激活（proto 面既有，服务端校验+伴侣面渲染全缺）。落地 2026-10-15：发送侧 `ValidateTradeMetadata`（解析不出或缺 trade_id 回 INVALID_PARAM，坐标/物品/成就链接同层同口径，非 TRADE_STATUS 的 metadata 原样透传；status 展示文本不锁枚举——新状态值不被旧服务端挡下；basic 串联进 `ValidateSendMessageRequest` achievement 检查后、enhanced 独立回包块）；web/desktop `TradeView` 模型 + `tradeViewOf`（镜像 achievementViewOf）+ `sendMessage` 第 7 参 trade + 交易卡片渲染（状态徽章 + 物品摘要 ×N + 金额 + trade_id caption、`data-trade-status` 供测试断言，空字段不渲染）。SDK 四端零改动（metadata 通用管线直接承载）。**范围边界**：拍卖行事件源属游戏侧；game 平面 inject 面（MessageInjectRequest）只有 content 无 metadata 字段，注入卡片消息需扩 inject 协议——后续增强。测试：ctest 44/44 全绿（chat basic/enhanced 各 +2 TRADE_STATUS 例：notify/历史/离线补投三臂全链透传 + 校验拒绝零投递零落库），web typecheck + 325 例全绿（+5：api 3 + MessageBubble 2），desktop typecheck + 30 例全绿（+4：api 2 + ChatPanel 2）
- [x] 系统播报注入二要素（marquee_inject，game_chat_features P1 游戏特色）：game 平面 `MessageInjectRequest` 带 `priority`/`ttl_seconds` 展示面元数据（值域与 chat.Priority 对齐，缺省 0 旧游戏侧零感知）。落地 2026-10-15：proto 加 9/10 两字段 + 五语言生成物重生成（cpp/ts/go/csharp/java，gen_proto.sh 一把梭 protoc 33.4）；chat 侧 `InjectConsumer` 直传 `ChatMessage.priority`/`ttl_seconds`（int32 值域契约，channel_type 同款口径；枚举外值客户端回落普通气泡，服务端不拦——不越权校验）；server_gateway 整消息转发（`*notify.mutable_message() = req`）零改动。走马灯判定二要素（URGENT+ttl>0，`systemSurfaceOf` 与频道无关）与系统弹窗（HIGH）经注入即点亮；事件源（BOSS 刷新/首杀等）在游戏侧，注入走 WORLD 频道（inject 合法域 PRIVATE..WORLD）。顺带补 NPC 对话行注记（2026-09-13 npc_engine 关键词规则引擎已落地，文档原文「当前已实现」属实但缺锚点）。测试：ctest 44/44 全绿（inject consumer 14 例含新增 PropagatesPriorityAndTtlForSystemSurfaces：URGENT+60s 透传 + 缺省零值臂），Go build + 33 例、sdks/ts 136 例、app-protocol 126 例、C# 127 例全绿
- [x] 招募卡片（guild/party recruit，game_chat_features P1 消息展示——展示性卡片形态）：`msg_type = GUILD_RECRUIT = 16` / `PARTY_RECRUIT = 17` + `GuildRecruitMetadata{guild_id,guild_name,level,member_count,max_members,description}` / `PartyRecruitMetadata{party_id,dungeon_name,needed,current,leader_name}` 消费面。落地 2026-10-15：发送侧 `ValidateRecruitMetadata`（各按对应 Metadata 解析且 id 非空，空判同时覆盖「没带 metadata」与「解析不出」，回 INVALID_PARAM——交易/成就链接同层同口径；basic 串联进 `ValidateSendMessageRequest` trade 检查后、enhanced 独立回包块；其余 msg_type 原样透传）；web/desktop `GuildRecruitView`/`PartyRecruitView` 模型 + `guildRecruitViewOf`/`partyRecruitViewOf`（畸形体/缺 id 不当卡片）+ `sendMessage` 第 8/9 参 + 卡片渲染（公会：等级 N · 人数 M[/K] + 招募语；组队：缺 N 人 · M/K + 队长；id caption；零值字段不渲染，`guild-${id}`/`party-${id}` testid 供测试断言）。SDK 四端零改动（metadata 通用管线直接承载）。**范围裁决**：文档原文「一键加入/一键申请按钮」需要公会/组队系统的动作协议面（伴侣面无此动作面）——落地为展示性卡片，游戏内接入走 SDK 自渲染。测试：ctest 44/44 全绿（chat basic/enhanced 各 +2 招募例：notify/历史全链透传 + 垃圾 metadata/缺 id 校验拒绝零投递零落库），web typecheck + 330 例全绿（+5：api 3 + MessageBubble 2），desktop typecheck + 34 例全绿（+4：api 2 + ChatPanel 2）
- [x] 反广告注记（anti_ad，game_chat_features P2 安全与合规——docs-only 对账）：RepeatGuard 自动禁言部分核实为已落地（2026-09-22 随 P0 重复消息检测批），但文档缺状态注记。补注记：第 3 次连续相同发送开启 5 分钟禁言窗（窗内所有内容拒发，不只重复内容），拒绝回 RATE_LIMITED，守门在 pacing 之后敏感词之前，双形态同接线；「标记审核」需要管理后台审核队列（全局管理员面）——与举报/禁言同款裁决另批，举报台账为其预留消费面。docs-only 改动，门禁只跑 `cd docs && vitepress build`
- [x] P2 性能与运维四项注记（game_chat_features 消息持久化/多设备同步/消息去重/连接保活——docs-only 对账）：四项在源码中均已实现但缺状态注记，git 考古取证后补注记——持久化（HybridMessageStore 2026-10-11/12 双写 Redis 热层+MySQL 冷层、Redis 断连内存待发）、多设备（session_registry 2026-09-14 (user,platform) 多平台槽 + GetUserSessions 扇出 + MESSAGE_READ_NOTIFY 已读广播 + DEVICES_PRESENCE_NOTIFY 设备清单）、去重（web/desktop message_store 2026-09-18 按服务端 message_id 丢重；C++ core message_store.cc:28 同 key 归一）、保活（backoff.h 2026-09-20 25s 心跳+退避重连+KICK 终态；服务端离线队列 expires_at 补投+迟到 ACK 清理 + 原生 OfflineSendQueue）。消息过期/消息压缩两行核实确为缺口，保持无注记（真缺口不虚标）。docs-only 改动，门禁只跑 `cd docs && vitepress build`
- [x] 自定义频道裁决注记（custom_channel，game_chat_features P1 频道管理——docs-only）：玩家建频道=建群（CREATE_GROUP 2101/2102，群 id 即频道 id GUILD 域，创建者 OWNER + GroupMemberRole 阶梯管理面齐备），语义与群组系统完全重叠；裁决不做独立「自定义频道」频道类型，独立频道命名空间（跨群检索/目录/主题聚合）留待真实需求另批。docs-only 改动，门禁只跑 `cd docs && vitepress build`
- [x] 消息过期（history_retention，game_chat_features P2 性能与运维——enhanced 形态保留窗）：`--history_retention_days` + `--private_history_retention_days` 双旗标（天数计，缺省 0=关零 I/O；私聊窗未配跟随普通窗）。落地 2026-10-15：`HistoryRetentionSweeper`（services/shared/chat/src/history_retention_sweeper.h，定时臂与 MessageMigrationWorker 同款 strand 形状，每小时一趟，SweepOnce(now) 独立测试缝）+ `HybridMessageStore::ExpireBefore`（热层 Redis history 列表逐 blob 解析按 channel_type 各用各的 cutoff 逐条 LRem、解析失败不动；冷层一条 DELETE 两 cutoff）+ `MessageStore::ExpireBefore` 缺省 0 行接口面 + `MySQLMessageStore` 实现（epoch 恒正天然 cutoff<=0 空匹配）+ main_enhanced 旗标解析与启停接线；CMake 双面接线（chirp_chat 源单 + CHAT_BASE_SRCS 排除——basic 内存态随进程消亡不接，离线队列独立 TTL 不受保留窗管）。测试：chat_mysql_tests 89 例全绿（+10：MySQL ExpireBefore SQL 两 cutoff/连接失败与 DELETE 失败均回 0、双 tier 各自 cutoff 裁决与幂等、零 cutoff 全保留、垃圾 blob 不动、SweepOnce 裁决面与双关零 I/O、定时循环启停拍空、Stop 先于首拍入队走早退臂、单窗关闭各自 cutoff 生效、基类缺省 ExpireBefore 回 0 行）
- [x] 消息压缩·服务端（history_gzip，game_chat_features P2 性能与运维——历史读回形态，SDK 四端解压侧待落地）：`LoginRequest.supports_gzip = 6` 登录声明（会话级能力记账）+ `GetHistoryResponse.messages_gzip = 4`（bytes，gzip 容器 RFC 1952，载荷=只带 messages 清单的同型响应——客户端解压按同型解析回 `.messages`，线上无旁格式）。落地 2026-10-15：`libs/common/gzip.{h,cc}` 压缩原语（压缩 windowBits 15+16、解压 15+32 自动识别 zlib/gzip、解压 16 MiB 炸弹上限；zlib 入 vcpkg.json 显式声明 + chirp_common 链 ZLIB::ZLIB）+ `services/shared/chat/src/history_compression.{h,cc}`（CompressHistoryResponse：非能力/空清单 no-op，压缩失败兜底放回明文）+ 能力记账（SessionRegistry `gzip_sessions`——BindAuthenticatedSession 新尾参 supports_gzip 双向覆盖重登录改口、RemoveAuthenticatedSession 清理、SessionSupportsGzip 读；distributed 本地 Set/Is 集合同生命周期）+ 三形态接线（basic GET_HISTORY 尾段 / enhanced HandleGetHistory 传参 / distributed HandleGetHistory 传参）+ 五语言生成物重生成（gen_proto.sh，protoc 33.4）。范围裁决：实时扇出/离线补投不压（单条小包）；GetHistoryResponseV2 不加字段（其 handler 是 INVALID_PARAM 桩，声明无人消费——待实现时再加）。测试：ctest 44/44 全绿（common_tests 60 例 +10：往返/gzip 魔数/高重复压缩率/空输入/空指针/坏容器/坏魔数/截断/炸弹上限/zlib 容器兼容；session_registry 27 例 +1：能力 bind/read/remove 骑行与未绑定回 false；chat_basic_session 46 例 +2：经生产派发 e2e（能力会话收 gzip 解压回清单+同频道未声明会话收明文；空清单不压+全默认消息 gzip 路径往返）；chat_managers +1：distributed gzip 形态 + 改口清声明）。coverage pin：gzip.cc 三处 zlib 内部失败臂 + history_compression.cc 压缩失败兜底臂（有效参数下不可达，KNOWN_UNCOVERABLE 钉行）
- [x] 消息压缩·SDK 四端解压侧（gzip 消费面）：C++ core（声明 `supports_gzip` + `GetHistory` 双形态解压——`chirp::common::GzipDecompress` 直接复用）→ TS 双包（sdks/ts + apps/shared/protocol：`DecompressionStream('gzip')`，注意两包同步）→ C#（`GZipStream`）→（Java/Kotlin/Swift 原生核按需）。**进度 2026-10-15**：C++ core 已落地（登录声明 + `FetchHistory` 双形态解压，坏容器/坏载荷→`BadResponse`、超时→`Timeout`）；TS 双包已落地（登录声明 + `GET_HISTORY` 可选 `inflateResponse` 钩子，`DecompressionStream('gzip')` 零依赖、16 MiB 解压上限，容器损坏→`GzipError`）；`web_companion`/`desktop` 登录已声明。C# 已落地（登录 `SupportsGzip` 声明 + `Specs.GetHistory` 挂拆封钩子，`GZipStream` 零配置直读 RFC 1952 + 16 MiB 上限；**额外自写 CRC32/ISIZE 尾部校验**——`GZipStream` 只在 deflate 末块到达时才验尾部，中途截断的容器会被静默读成短清单，自校验把截断拉回与 C++/TS 一致的报错；坏容器→`GzipInflateError`）。dotnet 测试 140/140 全绿（原 127 +13：原语 8 例含往返/跨块往返/空输入/坏容器/截断/坏尾部/仅头部/超限，客户端面 5 例含登录声明/gzip 形态拆封/明文形态/坏容器/坏载荷）。余下移动端原生核待落地：**Android Kotlin 已落地**（`apps/android` 协议核 `Gzip.kt`——`GZIPInputStream` 零配置直读且自带尾部校验、16 MiB 上限、`MessageSpec` 加可选 inflate 参数、`MsgSpecs.getHistory` 挂 `inflateGzipHistory`、登录 `setSupportsGzip(true)`；make gate 122/122 全绿，原 110 +12：原语 7 例含往返/跨块往返/空输入/坏容器/截断/仅头部/超限，spec 面 4 例含 gzip 形态拆封/明文形态/坏容器/坏载荷，登录声明 1 例）。iOS Swift / 鸿蒙按低优先级批（只写代码，iOS 走 macOS runner 验证、鸿蒙编译过即可）。服务端已具备能力记账与压缩发送，未声明的会话仍走明文，明文路径不受影响
- [ ] 消息压缩·iOS Swift / 鸿蒙 ArkTS 解压消费面（**iOS 半边已验证，鸿蒙待工具链（2026-10-15）**）。**iOS**（1a78f8e）：`apps/ios/Sources/CChirpGzip/chirp_gzip.c`（zlib inflate 15+32 + 16 MiB 上限）+ `Sources/ChirpProtocol/Gzip.swift` 薄封装 + `MessageSpec.swift` 拆封钩子 + `ChatPipeline.swift` 挂载 + `Tests/ChirpProtocolTests/GzipTests.swift`（原语 8 例 + 管线 4 例共 12）——**2026-10-15 经 build/pnpm-migration → main 的 PR CI 在 macOS runner 编译通过并全绿**（GzipTests 12/12、swift test 包全量 245/245，run 38706660284；测试面首编修正 4 个 commit：Data/[UInt8] 转换、`Chirp_Common_ErrorCode.ok` 前置形态、闭包 map 替代 keypath、`with` 闭包内 `try serializedData`）；登录**已声明** `supports_gzip`（ChatPipeline.swift:178），消费面拆封已经单测验证，余下真机联调不设卡。**鸿蒙**（2026-10-15 落码）：ArkTS 无 DecompressionStream，共享协议核 `gzip.ts` 加 `setGzipDecompressor` 注入缝（两包同步 + vitest：protocol 137/137、sdks/ts 147/147），`apps/harmony/entry/src/main/ets/common/HarmonyGzip.ts` 用 `@ohos.zlib`（API 12 `createZipSync().inflateInit2(strm, 31)` 单趟 + 16 MiB 定宽缓冲）实现平台解压器，AppServices 装配时装入；登录**未声明** `supports_gzip`（声明以编译验证为前置——服务端对未声明会话永远发明文，明文路径不受影响）。**2026-10-15 符号级静态核对**：文件用到的 12 个符号逐条对照官方 `interface_sdk-js/api/@ohos.zlib.d.ts` 全部存在且签名一致（`createZipSync`/`inflateInit2`/`inflate`/`inflateEnd` 均 `@since 12 dynamic`，与本工程 compatibleSdkVersion 5.0.0(12) 对齐；`ReturnStatus.OK=0`/`STREAM_END=1`、`CompressFlushMode.FINISH=4`、ZStream 五字段齐备）——核对中发现 `ZStream.totalOut` 在 d.ts 里声明为 `long`（官方 API 文档表格作 `number`），直接喂 `ArrayBuffer.slice(begin: number)` 有类型不匹配风险，已改 `Number(strm.totalOut ?? 0)` 归一（两种 `long` 口径下均成立）。**余下卡点**：鸿蒙本机与 CI 均无 hvigor/ohpm/DevEco，仅剩编译器级验证（符号面已核对））
- [x] 临时频道 Inc 1（temp_channel，game_chat_features P1 频道管理——服务端+单测）：活动/副本临时频道 = 带 TTL 的群。落地 2026-10-15：proto `CreateGroupRequest.ttl_seconds = 7`（0=永久，上限 30 天，负数/超限回 INVALID_PARAM）+ `GroupInfo.expires_at = 10`（过期时刻 epoch 毫秒，随 GET_GROUP_INFO/GET_USER_GROUPS/GROUP_CREATED_NOTIFY 下发）；`GroupManager` 存 `expires_at_ms` + 私有 `NowMs()`（测试时钟接缝 `now_override_ms_`，0=墙钟）+ `LookupLocked()` 惰性摘除（命中到点群即从 groups_/user_to_groups_ 摘除返回空——91 个调用点收敛到 14 处查找，发送门 IsMember 语义自动正确零改动）+ `SweepExpired(now_ms)`（无人访问的过期群周期回收）+ `GroupExpirySweeper`（services/shared/chat/src/group_expiry_sweeper.{h,cc}，克隆 HistoryRetentionSweeper 形状，5 分钟一拍，main_enhanced 接线启停；basic 不接——惰性摘除已覆盖语义，清扫只是内存回收）。「参与者自动加入」由既有 `initial_members` 覆盖。测试：chat_managers_tests 全绿（internal 8 例 +4：TTL 存储与双读路径下发/惰性摘除全读路径视同不存在+user_to_groups 解绑/永久群不过期/清扫摘除+unlink/台账损坏容忍 continue 臂；sweeper 2 例：SweepOnce 只摘到点群/定时循环启停幂等；handlers +3：TTL 校验矩阵/过期时刻落点窗与通知体下发/0=永久）+ chat_basic_session_tests 全绿（+1 e2e：ttl=1s 建群→发送 OK+expires_at 下发→sleep 1.2s→发送 AUTH_FAILED+GET_USER_GROUPS 空）。无 coverage pin 风险（group_manager/group_handlers 无钉行）
- [x] 临时频道 Inc 2（temp_channel——SDK 面 + 伴侣端消费，接上条）：落地 2026-10-15。**C++ core**：`CreateGroup` 加 `ttl_seconds` 重载（4 参带 TTL，2 参版委托 0=永久群），`tests/unit/sdk_core_test.cc` +1（TTL 重载线上行断言）+ 既有生命周期用例补两参=永久断言。**TS 双包**（sdks/ts + apps/shared/protocol 同步，边界裁决）：schema 驱动自动携带新字段，wire-lock 测试补向量（CREATE_GROUP encodeRequest 带 ttlSeconds / GET_USER_GROUPS decodeResponse 断言 expiresAt）。**web 伴侣端**：models `Conversation.expiresAt?`；`ChatApi.createGroup` 第三参 ttlSeconds；`refreshGroups` 名册为准修剪群会话（退群/被踢/临时频道到点本地消失，私聊不参与修剪）+ `expiresAt>0` 才落 `expiresAt`；`ConversationList` 建群对话框加有效期档位（永久/1 小时/1 天/7 天，GROUP_TTL_OPTIONS + groupTtl state 建后复位）+ 群列表项「剩 N 分/小时/天」倒计时（groupExpiryLabel，15s useNow 时钟粒度，到点返 null 不留残影）。**desktop 镜像**：models expiresAt + chat_api 同形（createGroup 三参 + 名册修剪 + expiresAt 携带，removeConversation 既有）+ `CreateGroupDialog` 有效期选择（GROUP_TTL_OPTIONS 从 Rail 导入）+ `Rail` 会话行倒计时（新 `state/useNow.ts` 镜像 web）+ `groupExpiryLabel` 纯函数测试。**C# 无 CreateGroup 面、Android/iOS 无便捷面**（仅协议常量，生成物已带字段）零改动。测试：sdk_core_tests 绿；TS 双包 11/11 each；web 346/346 + tsc 干净；desktop 52/52 + tsc 干净
- [x] 防骚扰私聊限制（dm_restriction，game_chat_features P2 安全与合规——P2 清单唯一无外部卡点项）Inc 1：协议+服务端+单测。落地 2026-10-15：`SET/GET_DM_RESTRICTION_REQ/RESP = 2255-2258`（接收方开关，缺省**关**，io 线程内存态与黑名单/频道屏蔽同生命周期）+ `DM_RESTRICTED = 14`（common 专码，先例 MUTED=12）；陌生人判定 = 非 social 平面好友快照（`FriendGraph` 只读消费 `chirp:social:friends:<receiver>`，StoredFriendList，无缓存随取随新）**且**无共同群（`GroupManager::ShareAnyGroup`，临时群按到点惰性摘除参与判定）；豁免好友/同群/本人/NPC；门在存储之前（被拒消息不落任何存储，不铸消息 id）；降级方向 = 收紧（Redis 缺失/键缺失/快照损坏一律非好友，只剩同群半边生效）；basic/enhanced 双形态同接线（enhanced `HandleSendMessage` 加 friend_graph 参），distributed 不接（与禁言批同口径）。**范围裁决**：原文「同公会」→ chat 群台账即其对应物；黑名单语义更强（投递侧静默丢弃）与本开关可叠加。测试：chat_friend_graph_tests 新目标 4 例（桩 RedisClient 判定矩阵：null redis/命中与未命中/损坏快照/空快照）、delivery_prefs_tests +2（缺省关/逐用户开关）、chat_managers_tests +2（ShareAnyGroup 基础矩阵/过期临时群翻 false）、chat_basic_session_tests +1（经生产派发 e2e：未登录 AUTH_FAILED/body 损坏 INVALID_PARAM/缺省关→开启拒陌生人零通知零离线落库→NPC 回 TARGET_OFFLINE→快照好友放行→共群放行→本人 INVALID_PARAM（basic 既有参数校验先于门）→关闭恢复）、chat_enhanced_session_tests +1（缺省关/拒陌生人零投递/本人豁免/关闭恢复）；五目标 ctest 全绿，chirp_chat + chirp_chat_distributed 编译 0 warning
- [x] 防骚扰私聊限制 Inc 2（dm_restriction——SDK 四端对齐，接上条）：落地 2026-10-15。**C++ core**：`SetDmRestriction(enabled, cb)` / `FetchDmRestriction(cb)`（2255-2258，克隆 ReportMessage 形状；未连接回 NotConnected）；`sdk_core_test` +1 roundtrip（set 线上 enabled 断言 + get 空请求体回显开关态）+ 未连接 dispatch-cover 用例补两 API。**TS 双包**（sdks/ts + apps/shared/protocol 字节同步）：`msg_map` 加 SET/GET_DM_RESTRICTION spec + `chat_pipeline` 加 `setDmRestriction`/`getDmRestriction`；测试补向量（pipeline 1 例：wire round-trip enabled 回显 + GET 空体；msg_map 1 例：spec id 钉死 2255-2258 + 编解码向量 + GET 空请求零字节）。**C#**：`Specs.SetDmRestriction`/`GetDmRestriction` + `ChirpClient.SetDmRestrictionAsync`/`GetDmRestrictionAsync`；glue 测试 +1（round-trip + GET 空体断言）。**iOS/鸿蒙/Android Kotlin 便捷面按移动端令留空档**（与举报批同口径：iOS MsgSpecs 面尚无 2253/2254，不单独卡本批）。文档：CAPABILITY_MATRIX 聊天行补防骚扰段、sdks/unity README 与 docs/sdk/unity3d.md 段位 2001-2258（顺带修 2249→报告批漏更的陈旧段位与社交 3024→3029）。测试：sdk_core_tests 140/140、sdks/ts 149/149、app-protocol 139/139、dotnet 141/141 全绿
- [x] 防骚扰私聊限制 Inc 3（dm_restriction——web/desktop 消费，收口）：落地 2026-10-15。**双端三线拒收面**：`models.failureReason: 'muted' | 'dm_restricted'` → `message_store.failPendingMessage` 加 reason 参 → `chat_api` sendMessage 尾部按 `MUTED(12)/DM_RESTRICTED(14)` 映射 → 气泡专属提示（web i18n `statusDmRestricted`、desktop 内联文案，MUTE 单因先例扩为双因）。**开关面**：web `ChatApi.setDmRestriction/getDmRestriction`（SET 回 code、GET code!=0 回 null）+ `FriendsDialog` 第二 Switch 行 `dm-restriction-switch`（打开拉 2257、切换即 2255、失败静默下次打开重拉，与好友上线通知开关同形态）；desktop `chat_api` 同形 + `MainWindow` 顶栏「私聊限制」按钮 `dm-restriction`（上线通知按钮同形：`variant` 反映态、GET 回 null 置灰、点击乐观翻转即 SET）。**测试**：web chat_api +2（DM_RESTRICTED 拒收回码单测 + set/get describe 两例）、FriendsDialog +1（拉开关/切开关 2255-2257 线断言）；desktop chat_api +3（拒收回码/set/get）、ChatPanel +1（muted/dm_restricted/兜底三文案）。门禁：web 350/350 + tsc 干净；desktop 56/56 + tsc 干净

- [x] 存量文档陈旧注记对账（R1015E，docs-only）：巡检时全仓找待办文件——仅 TODO.md 有未完成项（195/211/251 三条全被用户项或工具链卡住），DISTRIBUTED_CHANGES.md 为历史批次记录、roadmap_history.md 自述存档并指向 TODO.md、sdk_compatibility.md 为状态矩阵（UE/LayaAir/Godot/Cocos 项均需真实引擎，卡点成立），无虚构空间。核实旁证时发现四处注记与源码不符并按「以源码为真相修文档」修正：CAPABILITY_MATRIX 历史压缩行「iOS/鸿蒙待落地」（iOS 已落地并经 PR #35 macOS runner 编译验证——GzipTests 12/12、swift test 包 245/245）、移动端行「iOS 壳层 UI 待接」（`ChirpCompanion/` SwiftUI 壳 11 个 Swift 文件已交付，README P1–P6 全绿）、管理后台行与 roadmap_history 同款「用 mock 数据和演示页面」（实为 `App.tsx`/`Users/Channels/Messages.tsx` 首选真实 HTTP `fetch /api/{stats,users,channels,messages}`，仅请求失败回落内置 mock 演示数据；仓内确无 `/api/*` 后端，仍未接真实后端）、roadmap_history 推送注记「provider HTTP 投递仍为日志 stub」（`main.cc` 默认 `LoggingPushTransport`，但 `--push_transport http` 已接真实 `HttpPushTransport` HTTP/1.1+TLS）。差异入表 docs/审计-文档一致性.md 新增「批次：2026-10-15（R1015E…）」节（5 条超前）。docs-only 改动，门禁只跑 `cd docs && vitepress build`（绿），未跑编译与测试套件（05b7230）
- [x] 技能链接卡片（skill_link，game_chat_features P1 消息展示——渲染矩阵承诺 `SKILL_LINK → 可点击技能卡片`、proto 表列 `SkillMetadata`，但功能行与服务端/双端消费零落地，PATROL015 巡检补缺口）：落地 2026-10-15。**服务端**：`chat_validation` 加 `ValidateSkillMetadata`（SKILL_LINK 的 metadata 须能按 SkillMetadata 解析且 skill_id 非空，否则 INVALID_PARAM；挂 `ValidateSendMessageRequest`（basic）与 `main_enhanced` 门链（enhanced），与坐标/物品/成就/交易/招募六门同层同口径）。**web/desktop**：models `SkillView`、chat_api `skillViewOf` + sendMessage 尾参 skill（切 SKILL_LINK + metadata 通道 + 乐观卡片）+ toView 解码；web `MessageBubble` 与 desktop `ChatPanel` 渲染技能卡片（图标+名称+`Lv.N`(level>0)+描述+skill_id caption；level=0 不显等级面，名称缺省回落 id）。**SDK 四端零改动**（metadata 通用管线承载，与交易/招募批同口径）。**范围裁决**：渲染矩阵原文"可点击"落地为展示性卡片（与成就分享同裁决：详情即卡片正文，无可展开二级数据面）。测试：chat_basic_session_tests +2（carry 经生产派发 notify/历史保真 + reject 垃圾/空 id/通过臂/TEXT 不受影响）50/50、chat_enhanced_session_tests +1（双臂合一）44/44、chirp_chat/chirp_chat_distributed 0 warning；web 355/355 + tsc 干净（chat_api +3、MessageBubble +2）；desktop 62/62 + tsc 干净（chat_api +3、ChatPanel +3）
- [x] 举报 + 防骚扰私聊限制——原生协议核填充（R1016A 巡检，清掉举报批与防骚扰 Inc 2 两次留的「移动端原生核留空档」）：落地 2026-10-16。**Android Kotlin**：`MessageSpec.kt` 加 `reportMessage`/`setDmRestriction`/`getDmRestriction` 三 spec + `all` 入表（规整表 51→54），`MsgSpecsTest.kt` 钉 2253-2258 六 id 与表长 54 + 三个空体解码类型断言。**iOS Swift**：`MessageSpec.swift` 同形三 spec + `all` 入表 54；`MsgSpecsTableTests.swift` 钉 2253-2258 与 `DM_RESTRICTED = 14`、表长 54、三个空体解码类型断言 + 新增 `testDmRestrictionRoundTripsSwitchStateAndRejectCode`（开关态与拒收码双 round-trip）+ 新增 `testTextFormatResolvesTheNewEnumNames`。**iOS gencode 手写**：`*.pb.swift` 由 protoc-gen-swift 1.38.1 生成并提交在仓，本环境无 Swift 工具链、CI 亦无 gencode-drift 腿——`gateway.pb.swift` 加 2255-2258 四 case（proto 注释逐字镜像 + `init?(rawValue:)`/`rawValue`/`allCases`/`_protobuf_nameMap`）、`common.pb.swift` 加 `dmRestricted = 14`、`chat.pb.swift` 加 SetDmRestrictionRequest/Response 与 GetDmRestrictionRequest/Response 四消息（struct + Message 扩展 + nameMap + decode/traverse/==）。**踩坑**：`_protobuf_nameMap` 是 SwiftProtobuf 压缩字节码（改型 varint：6 位/字节、MSB 必须清零、0x40 为续传位），**只被 text/JSON 格式化遍历，二进制编解码不走**——手改坏字节时编译与全部二进制用例仍全绿，text 格式化才 fatalError。本批 2258→3001 delta 743 首次写成 `\u{a7}`（MSB 置位）；独立脚本按 reader 语义全量解码三份 gencode 的 156 个 nameMap（gateway 233 项、common 15 项、chat 146 项）逐条核对后改回 `\u{67}`，新增用例把该字节码面钉死防回归。**鸿蒙零改动**：共享 TS 协议核（apps/shared/protocol）已带 reportMessage/setDmRestriction/getDmRestriction。**文档对账**：CAPABILITY_MATRIX 移动端行门禁例数陈旧（Android `make test` 110→122、iOS `swift test` 232→247）+ 协议核补三面注记；差异入表 docs/审计-文档一致性.md 新增「批次：2026-10-16（R1016A…）」节（2 条不符）。测试：iOS `docker run swift:latest swift build` + `swift test` = 247/247 全绿 0 失败（ChirpProtocolTests 108 + ChirpAppCoreTests 139，30 套件；Linux 不跑 Darwin-only 用例）；Android `make test` = 122/122（16/16 容器）。**macOS runner 已复验**（CI `ios-app.yml` run 38777775073，2m59s）：swift test 247/247 全绿 0 失败，两平台例数一致；MsgSpecsTableTests 6/6（含新增 round-trip 与 text-format 两用例）——手改 gencode 经 macOS 编译面与测试面双向验证通过
- [x] 巡检复核轮（PATROL1016，docs-only）：三条挂起项解锁条件逐条复核，结论**均仍成立**，不重开已裁决项，不开新任务。**195（APNs 真投递凭据）**：仓内 `find . -name "*.p8"` 与未跟踪文件均无凭据材料，实现侧确认无欠账——`services/app/notification/src/apns_jwt.{h,cc}` 在位，`tests/unit/notification_apns_jwt_test.cc` 10 例 + `tests/unit/notification_internal_test.cc` APNs 4 例（缓存命中/无凭据免头/建 token 失败/读 key 文件）合计 14 例，与台账注记一致；解锁仍需用户给 Team ID / Key ID / .p8 / Bundle ID。**211（手机端原型图二审）**：main 上 `design/prototypes/mobile` 五页 HTML + `chirp.css` + 375 视口截图（bff4a77）与 03 好友页一审修订 d115204（来源游戏标签 + 筛选 chip，标注「二审中」），此后无新提交、无用户裁决；解锁仍需用户二审拍板。**251（鸿蒙 gzip 编译验证）**：本机 `which hvigor ohpm hvigorw` 三项全空、`.github/workflows/` 无 harmony/ohos/hvigor 腿（`grep -rln` 零命中），工具链缺口未变；`apps/harmony/entry/src/main/ets/common/HarmonyGzip.ts` 已随 1c12e7c 进 main。docs-only 改动，门禁只跑 `cd docs && vitepress build`（绿），未跑编译与测试套件
- [x] docs 根目录双语配对（R1016E，docs-only）：`docs/` 根目录 11 篇实质文档全部配齐中英双版（9 篇中文文档各加 `.en.md` 英文版；`README.md` 按「README 不得混入中文正文」英文化，中文原稿转 `README.zh.md`），两侧**首行**均加语言切换器 `> 🌐 Language: …`（位于前 3 行内，中文页指向 `.en.md`、英文页指回原页）。配对清单：`README`↔`README.zh`、`CORE`↔`CORE.en`、`CAPABILITY_MATRIX`↔`CAPABILITY_MATRIX.en`、`QUICKSTART`↔`QUICKSTART.en`、`API`↔`API.en`、`architecture`↔`architecture.en`、`server_plane`↔`server_plane.en`、`web_companion`↔`web_companion.en`、`审计-专项审查-投递认证与状态一致性`↔同名 `.en`、`审计-文档一致性`↔同名 `.en`、`index`↔`index.en`。**英文版为逐节逐表真实对应（非机翻占位）**：命令/路径/代码块/mermaid 体/消息名/端口/`--flag`/commit hash/`file:line` 引用一律原样保留，标题数与表格行数与原文一一对应（架构 23 标题 / 78 表行 = 78、CORE 9 标题 / 68 表行 = 68、server_plane 20 / 33 = 33、能力矩阵 70=70、审计-文档一致性 632=632（246 表行）、专项 115=115（58 表行）），无「暂无译版」条目（最长一篇 632 行，未触 1000 行标注阈值，已全译）。**结构例外（如实记录）**：`index.md` 是 VitePress `layout: home` 首页，正文不参与渲染——切换器落在 hero actions（`English` / `中文` 两个按钮，首屏可见），无法作正文行内首 3 行；`architecture.md` 原 3 行 frontmatter（title 与 H1 重复）已删，让切换器占首行。**排除项核对**：`CHANGELOG`/`CONTRIBUTING`/`LICENSE`/`CODE_OF_CONDUCT`/`SECURITY` 在本目录不存在；`node_modules`/`dist`/`build` 未纳入。**口径确认**：本仓非 `hello-*` 教学仓，属双语名单内，判据自洽无冲突。**下一轮范围（本轮只登记不执行）**：`docs/api/`、`docs/guide/`、`docs/sdk/`、`docs/design-notes/` 四个子目录的双语配对。docs-only 改动，门禁只跑 `cd docs && npx vitepress build`（绿），未跑编译与测试套件
- [x] docs 子目录双语配对（R1016F，docs-only，接 R1016E 的「下一轮范围」）：`docs/api/` 3 篇、`docs/guide/` 6 篇、`docs/sdk/` 8 篇、`docs/design-notes/` 20 篇全部配齐英译（本仓 `docs/` 下实质文档 48 篇中文页↔47 篇 `.en.md` + `README.zh.md`，**无「暂无译版」条目**——最长一篇 `npc_dialog_system` 650 行 / `SCALABILITY_CHAT_IMPLEMENTATION` 666 行，均已全译）。两侧首行语言切换器 `> 🌐 Language: **中文** (this page) | [English](./x.en.md)` / `> 🌐 Language: [中文](./x.md) | **English** (this page)` 全数落位（96 个 md 文件中仅 `index.md`/`index.en.md` 例外，VitePress `layout: home` 正文不渲染，切换器落 hero actions，首屏可见——承 R1016E 例外）。**frontmatter 处置**：`api/overview.md`、`api/peer_protocol.md` 原 3 行 `title:` frontmatter（与 H1 重复）删除，让切换器占首行，沿用 R1016E 在 `architecture.md` 确立的仓内口径。**英文版质量核验**（逐篇机械对账）：标题数一一对应、`grep -c '^|'` 表格行数逐篇相等（如 `sdk_hooks` 44=44、`sdk_compatibility` 85=85、`peer_protocol` 20=20、`overview` 30=30、`app_chat_prototype` 36=36）、代码栅栏数相等（`peer_protocol` 4=4、`SCALABILITY` 28=28）、命令/端口/`--flag`/proto 消息名/commit hash/`file:line` 引用原样保留；`awk` 剔除代码块后的 CJK 扫描逐篇过（残留仅两类有意保留：URL 路径中的中文文件名、审计文档里逐字引用的中文 commit 题）。**英文页内链一律指向英文孪生页**（`.en.md`），另修 4 处越界链接：`sdk_hooks.en.md` 的 `<player name>` 转义（VitePress vue 编译器按标签解析导致 build 红）、`server_plane.en.md` 锚点 `#凭证模型`→`#credential-model`、`audit` 两页的 `/审计-文档一致性`→`.en`、`index.en.md` hero/正文链接指向 `.en` 孪生。门禁：`cd docs && npx vitepress build` 绿（74.6s，96 页全渲染），未跑编译与测试套件（docs-only）
- [x] 巡检 R1016F 复核·英译页静默损坏修复（docs-only）：巡检读 todo 时发现 HEAD 顶部两个 `chore: wip snapshot 2026-10-16`（caf3527/a6b03e0，**已 push**）在重写 27 个 `.en.md` 内链时把英译页正文改坏了——①10 个英译页共 **350 个反引号代码跨被整段抹除**（`sdk/{index,unity3d,unreal,layaair,cocos-creator,godot,server,wechat-minigame}` 8 篇 + `design-notes/{capacity_benchmark,roadmap_history}` 2 篇；`sdks/unity`、`ChirpManager`、`AddComponent<ChirpManager>()`、`make test` 计数等标识符与命令名整段消失，如 `sdk/unity3d.en.md` 代码跨 41→4、`sdk/index.en.md` 48→0、`roadmap_history.en.md` 66→0）；②`api/overview.en.md` 代码栅栏内被注入 39 字中文（`// distributed correlation id, 0 = not provided (see below)` → `// 分布式关联 id,0 = 未提供(见下)`），违反 R1016E「英文页不得混入中文正文」口径。两类损坏**都不触发 vitepress build 红灯**（反引号丢失与栅栏内注释不在渲染校验面），属纯内容静默损坏——**代码跨数/CJK 数比对才抓得到，build 绿不能当护栏**。根因：重写脚本按「可翻译散文」整体处理，把反引号跨与代码栅栏一并纳入替换范围。**修复**：`git checkout 6617cd3 -- docs` 回到门禁验证过的 R1016F 状态，再用只改链接目标的脚本重放 63 处英文页内链（`.md`→`.en.md`，跳过首行切换器回指与无孪生页目标），即两个快照提交里唯一有效的部分。**核验**（逐篇机械对账，46 个 `.en.md`）：代码跨数 / 中文数 / 代码栅栏数与 6617cd3 **零差异**（相对已 push 的 HEAD 回补代码跨 +350、中文 -39）；英文页→中文页链接泄漏 0（残留 46 处为切换器回指，设计内）；首行切换器 45 个指向中文孪生页、0 处自指（`index.en.md` 承 R1016E 例外，切换器落 hero actions）；zh↔en 配对仍 11/10 + 3/3 + 6/6 + 8/8 + 20/20。**取舍如实记录**：两个快照提交夹带的英文润色改写与损坏同行、无法逐条核验（含 `revolves around`、`carries … corresponding by the chosen msg_id` 等可议表述），一并回退——宁取门禁验证过的状态，不留未核验改写。**未执行（不虚构任务）**：英译页仍未进 `.vitepress/config.mjs` 的 nav/sidebar（现仅靠首行切换器互达），属站点导航面另一批任务。门禁：docs-only 只跑 `cd docs && npx vitepress build`（绿），未跑编译与测试套件
- [x] ts-proto 2.x 再生成收口（PATROL1018A，修 #46/#47 合入后 Web workflow 双红腿）。**红腿一 proto-sync（#46 ts-proto 1.181.2→2.15.0 semver-major 未重生成）**：以 vcpkg protoc 33.4 + 插件 2.15.0 重生成 `proto/ts` 九文件（+18857/−11881）——运行时从 `protobufjs/minimal`（`_m0`）换 `@bufbuild/protobuf/wire`（BinaryReader/BinaryWriter + 100 层解码递归上限 + 各文件 `MessageFns` 类型导出），`forceLong=number` int64 语义保持（`longToNumber(reader.int64())` 原样）；cpp/go/csharp/java 四腿字节级不变（无重编面、无 ODR 风险）。**运行时依赖换线**：ts-proto 1.x 的陪跑 `long`/`protobufjs` 全仓零直用（grep 证），五处 manifest 换声明 `@bufbuild/protobuf ^2.16.0`——根 package.json（`proto/ts` 生成物非 workspace 成员，裸导入沿目录向上解析到根 node_modules 的既有约定，`pnpm-workspace.yaml` 注释同步改写）+ web_companion + shared/protocol + sdks/ts + desktop（独立锁各自 install）；`proto/ts/package.json` shim 包补 dependencies（鸿蒙 ohpm `file:` 消费方按此解析，entry 不重复声明、与 @chirp/app-protocol 携自带依赖同约定）；根与 desktop 两处 `allowBuilds` 的 protobufjs 死条目随批移除（两树 protobufjs 计数归零，desktop install 干净过验证无 ERR_PNPM_IGNORED_BUILDS）。**红腿二 web test（#47 react 升 19.3.0 而 react-dom/@types-react-dom 留 ^18.2.0——18 套件 ReactCurrentDispatcher 全挂，本地同症复现）**：web_companion react/react-dom 对齐 ^19.3.1（react 19 精确同版本铁律，先试 ^19.3.0+^19.3.1 混对撞 "Incompatible React versions" 后双双对齐）；desktop 19.3.0/19.3.0 本就一致零改动。**文档对账**：README.md/README.zh.md「TypeScript 运行时用 protobufjs」改 @bufbuild/protobuf（ts-proto 产物）；nightly.yml TS 包 NOTE.txt「依赖 long / protobufjs 由安装方解析」同步；SDK 接入文档（layaair/小游戏/Cocos/server）的 protobuf.js 通用建议不动（接入方自实现栈仍成立）。**鸿蒙**：gencode 换运行时、shim 已声明依赖，本机无 hvigor/ohpm 维持编译验证挂起（与 251 同款卡点，明文路径不受影响）。**CI 语义不变**：web.yml proto-sync 腿 `-I'^//   protoc '` 滤 banner 后 diff 再生成物，本批提交即其期待状态。测试：web typecheck+lint+build+355/355、shared/protocol 139/139、sdks/ts 149/149、desktop typecheck+62/62+build 全绿；根/桌面双锁 frozen 语义保持
- [x] 巡检复核轮（patrolfire1018，docs-only）：两条未完成项逐条复核解锁条件，结论**均仍成立**，零代码改动，不开新任务。**195（APNs 真投递凭据）**：仓内 `find . -name '*.p8'`（除 ios `.build` 缓存权限噪声外）零命中、`env | grep -ci 'apns|apple_id'`=0、未跟踪文件亦无凭据材料——解锁仍需用户给 Team ID / Key ID / .p8 私钥 / Bundle ID；实现侧无欠账（`services/app/notification/src/apns_jwt.{h,cc}` 在位，单测 14 例）。**251（鸿蒙 gzip 编译验证）**：**本轮新证据修正「本机无鸿蒙任何 SDK」的口径**——`/opt/ohos-sdk` 存在且是正式 OpenHarmony SDK（`oh-uni-package.json`：`version 5.0.0.71`、`apiVersion 12`、`releaseType Release`），但**只含 native 组件**（`native/{llvm,sysroot,build-tools/cmake,docs}`），**不含 ArkTS/JS 工具链**：`find /opt/ohos-sdk -maxdepth 5 -name 'ets*' -o -name 'es2abc*' -o -name 'ohpm*' -o -name 'hvigor*'` 零命中，`which hvigor ohpm hvigorw deveco-studio` 四项全空，`/home/cui/workspaces/*/node_modules/@ohos/*` 无 ohpm 包（各仓 `hvigor/` 目录只有 `hvigor-config.json5` 配置，无 wrapper 无包）——故 native SDK 对 ArkTS 源码面零帮助，251 仍卡编译器级验证。记录此路径以免后续巡检重复全盘搜索。**其余台账复核**：`docs/design-notes/roadmap_history.md`（+`.en.md`）自述存档并指向 TODO.md，其唯一未完成项（管理后台未接真实后端）与 TODO.md:234/245 登记的「全局管理员面另批」同一裁决，无独立可开工面（`grep '/api/' services/ apps/ libs/ proto/` 零服务端路由命中，仅 `apps/admin_dashboard/src/` 5 处前端 fetch）。**工作树**：上一轮已处置并还原的 `docs/.vitepress/config.mjs` 外部改动保持干净态，`git status --porcelain` 空。**远端**：`origin/main` 仍 113b1ff 无新提交、`gh pr list`/`gh issue list` 均空、最新 CI 十腿全 success（Nightly/Deploy Docker/Docker Image/CMake/CI/Web/iOS App/Go SDK/Unity SDK）。docs-only 改动，门禁只跑 `cd docs && npx vitepress build`，未跑编译与测试套件
- [x] 独立评审 P0 半边落地：`--host` 实控监听地址（PATROL1018C，接 1c3ce16 `docs/review/luna-review-2026-10-18.md` 表一 P0）。**缺陷**：`--host` 解析后从未传给监听器——`libs/network/tcp_server.cc`/`websocket_server.cc` 构造硬编 `tcp::v4()`（恒绑 0.0.0.0），notification 两个监听器 `services/app/notification/src/main.cc:192-193` 只传 port；且 `main.cc:245` 旧日志按 flag 直报「listening on 127.0.0.1」——**日志说谎而套接字仍绑全网卡**（评审证据成立）。**修法（库层最小面）**：`TcpServer`/`WebSocketServer` 构造尾部加 `const std::string& bind_host = "0.0.0.0"`（默认参数=既有全部调用点零改动：auth/search/social/voice/party/chat/chat_distributed/两 gateway + TlsTcpServer/TlsWebSocketServer 委托构造均按位传参自动落默认），ctor 成员初始化列表内 `ResolveBindAddress(bind_host)`（匿名命名空间函数，`asio::ip::make_address` 非法字面量抛 `asio::system_error`——启动即死，不静默回落全网卡），新增 `BoundAddress()` 访问器（与 `Port()` 同型，取 `acceptor_.local_endpoint`）；notification main 两个监听器传 `host`，`main.cc:245` 旧日志改读真实绑定端点（`server.BoundAddress().to_string()`），日志不再能说谎。**测试先行**：6 例新 gtest（`tests/unit/network_full_test.cc`，TcpServer/WebSocketServer 各 3：默认仍 0.0.0.0 / loopback 生效 / 非法 host 抛）先写后码，红→绿实证。**实测**：`network_full_tests` 70/70（含新 6 例）；二进制冒烟 `chirp_app_notification --host 127.0.0.1` 后 `ss -tln` 实证 `127.0.0.1:5006/5016`（默认参数仍 `0.0.0.0:5006/5016` 语义保持）。coverage pin 核对：`KNOWN_UNCOVERABLE` 无 `tcp_server.cc`/`websocket_server.cc`/`main.cc` 条目，行号漂移零影响。**P0 剩余半边未动**（handler 无调用方认证——需服务间认证方案设计，见新登记条目 268）；评审原文的过渡缓解「未认证前仅绑定内网地址」即本条落地。
- [x] 通知服务调用方认证（评审 P0 剩余半边，`docs/review/luna-review-2026-10-18.md` 表一行 1）：`services/app/notification/src/notification_handlers.cc` 处理器不验证调用者身份，注册/注销/查询/触发推送均可由请求体指定任意用户或设备，网络可达即可冒用。绑定面已由上一条收窄（`--host` 实控），但认证本体需定服务间调用方案（共享密钥/mTLS/签名头，取一）。**需拍板**：服务间认证机制选型 **已清（2026-10-19，认证机制已代决：共享密钥 SERVER_AUTH_REQ 握手，标「已代决」并按巡检单授权直接执行）**：新增 `services/app/notification/src/caller_auth.h`——`GateCallerPacket`（纯判定：无密钥=开放平面逐包放行/已信连接放行/未信连接须先握手/未信连接发 6xxx=拒绝）与 `VerifyCallerSecret`（`SERVER_AUTH_REQ`→`ServerAuthRequest` 比对，恒回 `SERVER_AUTH_RESP` 携 `OK`/`AUTH_FAILED`，序列回显），7 例单测（`CallerAuthGateTest`：开放平面、信连接免握手、握手路由、未信 RPC 拒绝、密钥匹配/不符/畸形体与伪握手包）。notification `main.cc` 接线：`--service_secret`（缺省空=开放平面，既有开发环境不变）、`trusted_conns` 连接级信任表（两监听器共享，断连回调擦除防指针复用认证）、拒绝路径 `SendAndClose`（镜像 chat `HandleServerAuth` 契约）、启动日志明示认证开/关；`CHIRP_COUNTER` 移到门后（计数=实际服务的 6xxx RPC）。调用方侧：`NotificationClient` 构造加第 4 参 `secret`（`libs/network/notification_client.{h,cc}`），每条任务连接 `asio::connect` 后先发 `SERVER_AUTH_REQ` 并校验 `SERVER_AUTH_RESP.code()==OK`，拒绝按普通传输失败回 `INTERNAL_ERROR`（调用方不区分密钥错误与断连）；`chirp_app_sdk_gateway`、chat 三形态（basic/enhanced/distributed）全部加 `--notification_secret` 接线。测试先行 3 例端到端（`AuthenticatedPushServer` 假服务端：`AuthenticatedHandshakeThenRoundTripDeliversResponse`、`RejectedHandshakeReportsInternalError` 断言 INTERNAL_ERROR 且服务端零 push；另 `SilentAuthServer`：接受连接但不回握手的假服务端，同样断言 INTERNAL_ERROR）。实测 notification_handlers_tests 14/14、notification_client_tests 18/18；部署面：dev compose 撤发布 5006/5016 且成对配密钥、生产 compose `.env` 读 `NOTIFICATION_SERVICE_SECRET`、部署指南 zh/en 增服务间认证段；专项审计 A1 双语标✅（默认绑定维持 0.0.0.0，暴露面由部署层收窄）；能力矩阵通知行双语补认证说明。
- [x] 评审缺陷收口批次一（`docs/review/luna-review-2026-10-18.md` 表一）：①P1 部署指南示例 `--allow_scaffold_login 1`（`docs/guide/deployment.md:33-36`，弱凭据后门，删示例参数+仅本地开发示例注明不可生产）；②P1 Auth 固定弱 JWT 默认密钥（`services/app/auth/src/main.cc:177`、`main_enhanced.cc:353-356`、`libs/common/login_token_verifier.cc:9-35`，未配置即拒启）；③P1 消息持久化失败仍回 OK（`services/shared/chat/src/main_enhanced.cc:397-412`、`hybrid_message_store.cc:172-200`，异步写失败仅告警，需区分「已受理/已持久化」）；④P1 `inject_id` 幂等键不去重（`proto/game_server_gateway.proto:53-55` 承诺幂等、`server_gateway_handlers.cc:55-57`/`inject_consumer.cc:28-29` 未实现，存储层时限幂等记录）；⑤P1 推送设备注册仅进程内存（`notification_service.h:203-210`，重启即失）；⑥P2 跨实例 `message_id` 重号（`runtime_utils.cc:49-52`，毫秒时间戳+进程内计数器）。各项测试先行，逐项独立提交。**已完成 ①②⑥**：① `f1c6680` 删示例参数+生产清单两行（zh/en 双版，docs build 绿）；② `faf7e8d` 新增 `libs/common/required_arg.h` 无默认值校验（4 例单测），auth 两形态无密钥/空值即拒启返回 1，compose/test_services.sh 均显式传 secret 不受影响，common_tests 66/66；⑥ `IdGenerator` 类替代文件内静态计数器，每实例 `RandomSeed()` 48-bit 随机起点，同毫秒同起点重号窗口关闭（6 例新单测含「同种子同毫秒必重号」锁定旧缺陷类），chat_managers_tests 305/305；④ `inject_id` 幂等去重：新增 `services/shared/chat/src/inject_idempotency.h/.cc` 进程内 TTL 表（缺省 5 分钟窗 `--inject_idempotency_ttl_ms` 可调，容量封顶 1 万条），`InjectConsumer` 可选接入——同 `inject_id` 重放回吐首次落库 `message_id`，不重复落库/投递/离线排队；空 `inject_id` 不参与去重、校验先于查表（`InjectConsumerIdempotencyTest`×6 + `TtlInjectIdempotencyTest`×4），chat 两形态 main 均接线，审计文档 D1 双语标✅（跨实例共享窗 Redis SETNX 仍开放）；③ 持久化失败区分：`StoreMessageAsync` 热层 `RPush` 结果检查（拒写即 Warn，原先被忽略）+ 冷层写有界重试（`mysql_store_attempts` 缺省 3、`mysql_store_retry_delay_ms` 缺省 100ms，`MessageStoreConfig` 可调）+ 穷尽后 `persist_failures()` 计数、send/inject/fan-out/peer 四处调用点 Warn→Error 带 message_id；OK=受理语义保持（契约分档仍待拍板，与表二②「DURABLE 改尽力而为」同口径），瞬态故障不再静默丢（3 例新单测：首败重试成/穷尽报 false+计数=1/死 Redis 热层丢失不污染回调），chat_mysql_tests 92/92、chat_managers_tests 315/315、chirp_chat 构建绿，覆盖率 pin 548→573/580→605/605→630 随 +25 行迁移，审计文档 D2 双语标◐；⑤ 设备注册持久化：新增 `services/app/notification/src/device_store.h`（header-only `DeviceStore`：每设备一行、文本字段 base64url 编码防换行断裂、临时文件 + rename 原子替换、坏行 Warn 跳过），`RegisterDevice`/`UnregisterDevice`/`UpdateDeviceToken` 在注册表锁内快照写盘，失败 Error 且内存注册不受影响；`NotificationService` 构造加第 4 参 `std::shared_ptr<DeviceStore>`（缺省 nullptr=纯内存，不传 flag 时行为与此前逐位一致），`main.cc` 新增 `--device_store_file`（缺省空=关）并在监听器就绪前 `RestorePersisted()` 回灌；测试先行 10 例（`NotificationDeviceStoreTest`×6 含全字段往返/坏行跳过/缺目录/目标是目录等失败分支/CRLF 行 + `NotificationServicePersistenceTest`×4 注册回灌/注销落盘/换 token 落盘/写失败内存不回滚），notification_tests 116/116、handlers 7/7、client 15/15、main_session 5/5，本地 gcov 两改动文件 0 未覆盖行，覆盖率 pin 133→139/228→272/399→443 随 +46 行迁移；审计 S3 双语标✅（MySQL write-through 对齐降为可选项），能力矩阵双语补持久化说明。**269 六项全清**
- [x] 评审表二文档对账（docs-only，以源码为真相修文档）：①`docs/design-notes/communication_core.md:79` 「inject_id 消费端已去重」与实现不符（先修文档撤回该声明，幂等实现另见上条④）；②同文 75-82 「DURABLE 重启不丢」与异步写不保真不符（口径改「尽力而为」或删）；③`IMPLEMENTATION_SUMMARY.md:20,24` 测试套件数 40/44 互相矛盾（实数 45 条 `add_test`）+ `docs/CAPABILITY_MATRIX.md:57-59` 覆盖率门槛 98%（实为 `scripts/run_coverage.sh:28` 默认 100%）；④`IMPLEMENTATION_SUMMARY.md:7` `proto/server_gateway.proto` 实名 `game_server_gateway.proto`；⑤`docs/审计-文档一致性.md:488,261` 未完成项写 195/211/251（现 195/251）。按审计规范三类差异入表。**已清（2026-10-19）**：五项全修——①④ communication_core/en 投递语义表 AT_MOST_ONCE 行补 inject TTL 窗边界（5 分钟缺省 `--inject_idempotency_ttl_ms`、进程内不跨实例）、DURABLE 行改尽力而为口径（有界重试参数+`persist_failures()`+OK=受理+分档待拍板）；②③ integration-pitfalls/en :176 重发安全声明补窗口边界；IMPLEMENTATION_SUMMARY proto 实名 `game_server_gateway.proto`、40→45 套件（ctest -N 实数）、98%→100%（run_coverage.sh FAIL_UNDER=100 缺省）；CAPABILITY_MATRIX zh/en 44→45；审计-文档一致性 R1015E 批次补时点括注（:261 经核为无关行，真实陈旧点仅 :488）+ 新批次表 8 条（超前 0/缺失 0/不符 8，docs build 绿）
- [x] 其余服务 `--host` 同款失效（评审只点了 notification，实为系统性）：`TcpServer`/`WebSocketServer` 绑定能力库层已就绪（上一条），但 auth/search/social/voice/party/chat/两 gateway 的 main 均未传 host（`services/*/src/main*.cc` 各构造点），全部恒绑 0.0.0.0。逐服务评估：面向内网的服务绑 127.0.0.1 或内网 IP，网关类保持 0.0.0.0 但需文档明示。**已清（2026-10-19）**：库层补 TLS 面——`TlsTcpServer`/`TlsWebSocketServer` 构造尾加 `bind_host`（缺省 `"0.0.0.0"`，委托基类），`MakeDistributedTcpServer`/`MakeDistributedWsServer`（distributed_runtime）同加参数转发；11 个 main 全接线（auth 两形态、social、party、voice、chat 三形态 main.cc/main_enhanced.cc/main_distributed.cc、两 sdk_gateway、server_gateway；search 原本就解析 `--host` 却从未传给监听器，属真缺陷顺带修复；npc_dialog 无监听器不涉及）。测试先行 6 例：`TlsEdgeTest` 4 例（TLS TCP/WS 各「绑 127.0.0.1 生效 + 缺省仍 0.0.0.0」）+ `DistributedRuntimeTest` 2 例（两 helper 传 host 后 `BoundAddress()` 断言）；network_tls_tests 15/15、chat_managers_tests 317/317；10 个带监听器 target 全构建绿；二进制冒烟 `chirp_chat --host 127.0.0.1 --port 17891 --ws_port 17892` 后 `ss -lnt` 实证两监听均落 `127.0.0.1`（缺省态绑定语义不变，267 已验）；本地 gcov `tls_server.cc`/`distributed_runtime.cc` 0 未覆盖行，pin 迁移 55→59（+4 行漂移，tls_server.cc 无 pin）；部署指南 zh/en 补绑定说明段+生产检查清单条目（后台服务绑内网地址、边缘网关保持 0.0.0.0 明示）
- [x] CI 红脉收口（FIRE1019B，main 自 `f26598b`/`f70e146` 起连续 5 个 commit 红，最后一个绿 CI 是 `a1184d0`）：两处根因，测试先行逐条修——①`2778f8a` 给 notification 的 `HandleNotificationPacket` 加了 `caller_secret`/`trusted_conns` 实参但漏改 seam 单测（`tests/unit/notification_main_session_test.cc:66/93` 编不过，smoke/build-and-test/coverage 三腿全死在 Build）；修两处调用并补 3 例过 seam 的认证门用例（握手授予信任后 6xxx 直通/错密钥 `AUTH_FAILED`+`SendAndClose`/未握手 6xxx 丢弃断连），notification_main_session 5→8 例。②`f70e146` 落 `inject_idempotency` 时漏了 `PruneExpired()` 的批量清扫路径（`services/shared/chat/src/inject_idempotency.cc:24` `entries_.erase(it)` 0 覆盖，cae395a 的 CI coverage job 实证 `services/shared/chat/src LOW 3692/3693`）；补 `RememberSweepsExpiredEntriesKeepsLive` 走 `Remember()` 内部的 `PruneExpired()`（TTL 清扫 erase 与容量挤出是两条路径，原 4 例只覆盖后者），chat_managers 315→318 例。验证：本地 gcov 门禁同口径复算（gate 同款 `executable_line`+KNOWN_UNCOVERABLE 抽取比对）chat 包 3698→3699/3699 全绿；`libs/network/redis_client.cc:97`（`std::unique_ptr` 空构造声明行）为本地 gcc15 伪影（同函数邻行 6230/6231/9018 而声明行 0），CI 侧 cae395a 实证 libs/network OK 不动 pin。附带收口：上一轮门禁跑在被 `/tmp/chirp-rb` 工作树污染的 build 树上报了假绿（bcov 已重建指回共享检出，「本地绿」从本轮起以共享检出全新门禁为准）。
