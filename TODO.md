# Project Chirp Roadmap

> 早期 roadmap(基础设施、各服务初版、SDK/应用初版)与 2026-03 更新日志已归档至 [docs/design-notes/roadmap_history.md](docs/design-notes/roadmap_history.md)。本文件只保留当前要做的事。

## Current Focus(2026-09,最高优先级)

> 决策:**优先把"游戏客户端 SDK 接入 + 游戏服务端接入"端到端跑通,其余(含下方架构债)等基础功能完成后再做。**
>
> 注意:当前 smoke 验证的是**过渡路径**——SDK 直连 chat + 脚手架登录(token 即 user_id)。最终拓扑(game_gateway 吸收 chat 直连入口、统一登录语义)依赖 P1,不要把脚手架当成终点。

- [x] **游戏客户端 SDK 进程级 E2E**:`sdks/core`(`chirp::sdk::ChatClient`)直连 chat 的登录/双向收发/离线队列链路,`test_services.sh --smoke-sdk`
- [x] **游戏服务端接入 E2E**:`test_services.sh --smoke-npc`(server_gateway 注入 + NPC 事件回环 + 离线队列 refill)
- [ ] **两条 smoke 纳入 CI**:`.github/workflows` 当前未跑 `test_services.sh` 的任何 smoke

## Architecture Debt(2026-09 架构评审)

> 来源:架构评审(见 `docs/architecture.md`)。按风险排序,完成一项勾掉一项。本节整体让位于 Current Focus。

### P0 — 公共代码沉淀(消除跨服务私有耦合)

- [ ] **抽取公共 session registry**:`services/gateway/src/gateway_session_registry.{h,cc}` 与 `services/chat/src/chat_session_registry.{h,cc}` 几乎逐行重复(仅改名)。沉淀为 `libs/` 下的公共组件,gateway / chat / app_gateway 共用。
- [ ] **消除跨服务直接编译对方源码**:`services/app_gateway/CMakeLists.txt:13-16` 直接编译 gateway 的 3 个 .cc 和 notification 的 `notification_client.cc`;`services/chat/CMakeLists.txt:39,74` 直接 include/编译 notification 的私有源码。将 `auth_client`、`redis_session_manager`、`notification_client` 沉淀为公共库,服务间只通过协议或公共库交互。

### P1 — 登录语义统一(对应 migration path 第 2/4 步)

- [ ] **统一登录/会话语义**:gateway 与 chat 各自处理 `LOGIN_REQ`,且 chat 版是 "token 即 user_id" 脚手架(`services/chat/src/main.cc:265`)。目标拓扑中 chat 应成为内部服务,由 `game_gateway` 吸收直连入口,客户端只认边缘。前置决策:token 验证方案选型(不透明 token 查 Redis / HMAC 签名本地验签 / 混合),见 `docs/architecture.md` 凭证模型一节。
- [ ] **设备级会话核心**:Redis session registry 从 "user → instance" 升级为 "user → device → edge instance",支撑 app 边缘与跨端语义(跨设备投递、kick 策略、统一未读数)。
- [ ] **玩家聚合平面模型(app_gateway 的目标形态)**:两类玩家边缘定位不同——游戏边缘(SDK/game_gateway/server 平面)**面向游戏接入、不做聚合**(一个接入可覆盖同一运营方的多款游戏,身份是游戏级);app 边缘是**玩家聚合平面**:player 身份(平台级)↔ 多个 game 身份的绑定注册(由游戏后端经服务器平面主张绑定)、跨游戏频道订阅与聊天 fan-in(统一未读)、跨游戏语音组队(语音身份=玩家)。当前 `app_gateway` 只有连接骨架 + 6xxx 转发,聚合模型未实现。与设备级会话核心同批设计;需一并决策"app 平面如何触达各游戏 chat 数据"(共享多租户核心 + 游戏命名空间 vs 联邦桥接),见 `docs/architecture.md`「Game-facing plane vs player aggregation plane」。

### P1 — 测试与构建一致性

- [x] **auth 单测**(从 P2 上调:auth 是 Supported 服务且在登录关键路径上,零单测风险高于构建洁癖):(2026-09 完成)`auth_stores_tests` / `auth_service_tests` 覆盖 user_store / session_store / rate_limiter / brute_force 等全部 enhanced 路径,auth 包行覆盖 100%。
- [ ] **修复 chat 增强构建功能缺失**:`services/chat/CMakeLists.txt:44-64` 的 MySQL 增强分支遗漏 `inject_consumer.cc`、`server_gateway_peer.cc`、`push_bridge.cc`、`channel_manager.cc` 等,导致增强构建丢失服务器平面集成与推送桥能力。三个 main(`main.cc` / `main_enhanced.cc` / `main_distributed.cc`)功能应对等或在文档中明确差异。
- [ ] **推送桥覆盖全部 chat 构建**:PushBridge 目前只接入默认 `chirp_chat`,`main_enhanced` / `main_distributed` 未接。
- [ ] **proto 改为链接 `chirp_protos` 静态库**:9 个服务各自 `file(GLOB ...)` 重复编译全部 .pb.cc,应统一链接 `proto/CMakeLists.txt` 已构建的 `chirp_protos`。

### P2 — 功能缺口与边缘硬化

- [x] **server plane 进程级 E2E**:已由 `--smoke-npc` 覆盖(注入 + 事件回环 + 离线 refill)。如需通用注入(非 NPC)场景的 smoke,再单独立项。
- [x] **chat 直连入口的限流/安全模型**:(2026-09 完成)`ChatRateLimiter` 固定窗口计数——登录按客户端 IP(30/分钟)、消息发送按用户(120/分钟),Redis 计数、任何故障一律 fail-open;超限回 `RATE_LIMITED`(common.proto 新增错误码)。阈值可配(`--login_rate_limit_per_min` / `--send_rate_limit_per_min`),无 `--redis_host` 时不生效。多级窗口/封禁列表等留给统一登录(P1)之后。
- [ ] **NPC 回复去重**:hub 重投窗口内可能产生重复回复,需按 `inject_id` / event id 去重(见 `docs/server_plane.md` NPC dialog 一节)。
- [ ] **app 边缘 TLS**:`app_gateway` 的 WS/TCP 监听尚无 TLS(docs 多处标注 "TLS planned")。
- [ ] **真实推送传输**:notification 的 APNs HTTP/2 / FCM HTTP 投递仍是日志 stub(`PushTransport` 接缝已留好,需真实实现 + TLS)。
- [ ] **app_gateway / voice 单测**:(2026-09 更新:social 已由 `social_presence_tests` 覆盖至行覆盖 100%)这两个服务尚未接入任何单测套件(均在 Experimental,可在其转 Supported 前补)。

### P3 — 暂缓项与杂项

- [ ] **Web 版伴侣 app**(低优先级,已加入计划):浏览器端 Discord 式 web app(现状:`apps/mobile_companion` 是移动雏形,`sdks/` 无 web SDK)。不排期;开始做 app 端时**先 web 后 Android/iOS**——web 一套代码即可在桌面/移动浏览器复用,且不依赖应用商店审核,验证成本最低。前置依赖:P1 统一登录/会话语义(web 端走 app_gateway 边缘,而非直连 chat)。
- [ ] **MsgID 去中心化**(暂缓):单一全局枚举意味着任何服务加消息都要改 `proto/gateway.proto`,但当前规模下中心化枚举天然防号段冲突,是优点;多团队并行开发时再评估按平面拆分。
- [ ] **容量基准实测**:旧 roadmap 的 "10k+ 并发" 宣称需实测证据后方可对外使用(见 `docs/architecture.md`)。
- [ ] **命名冗余/历史包袱**:`websocket_util.cc` 与 `websocket_utils.h` 并存;`presence_manager_v2` 只有 v2 没有 v1;libs/common 自研 sha256/base64 与 auth 的 libsodium 两套实现并存(评估统一或文档说明边界)。
- [x] **清理覆盖率产物**(2026-09 完成):`.gitignore` 早已覆盖,但 `coverage_html/` 与 `coverage-packages.csv` 曾被提交入库,本次连同根目录未跟踪的 `*.gcov` / `*.gcov.json.gz` / `build-cov/` 一并删除(入库部分以 git 删除提交)。
- [x] **移除死代码**(2026-09 完成):`services/router/` 空目录及顶层 CMakeLists 中被注释的 `add_subdirectory(services/router)`。
