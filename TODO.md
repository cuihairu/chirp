# Project Chirp Roadmap

> 早期 roadmap(基础设施、各服务初版、SDK/应用初版)与 2026-03 更新日志已归档至 [docs/design-notes/roadmap_history.md](docs/design-notes/roadmap_history.md)。本文件只保留当前要做的事。

## Current Focus(2026-09,最高优先级)

> 决策:**优先把"游戏客户端 SDK 接入 + 游戏服务端接入"端到端跑通,其余(含下方架构债)等基础功能完成后再做。**
>
> 注意:当前 smoke 验证的是**过渡路径**——SDK 直连 chat + 脚手架登录(token 即 user_id)。最终拓扑(game_gateway 吸收 chat 直连入口、统一登录语义)依赖 P1,不要把脚手架当成终点。

- [x] **游戏客户端 SDK 进程级 E2E**:`sdks/core`(`chirp::sdk::ChatClient`)直连 chat 的登录/双向收发/离线队列链路,`test_services.sh --smoke-sdk`
- [x] **游戏服务端接入 E2E**:`test_services.sh --smoke-npc`(server_gateway 注入 + NPC 事件回环 + 离线队列 refill)
- [x] **smoke 纳入 CI**:(2026-09 完成)`ci.yml` 新增 `smoke` job——ci preset(Debug)构建后依次跑 `test_services.sh` 的五项进程级 smoke(`--smoke` auth+gateway、`--smoke-chat`、`--smoke-sdk`、`--smoke-npc`、`--smoke-redis` docker 版跨实例 kick)。

## Architecture Debt(2026-09 架构评审)

> 来源:架构评审(见 `docs/architecture.md`)。按风险排序,完成一项勾掉一项。本节整体让位于 Current Focus。

### P0 — 公共代码沉淀(消除跨服务私有耦合)

- [x] **抽取公共 session registry**:(2026-09 完成)两份逐行重复的实现合并为 `libs/network/session_registry.{h,cc}`(`chirp::network::SessionRegistry` + Bind/Get/Remove 自由函数),gateway / chat / app_gateway 共用;`RemoveAuthenticatedSession` 统一为富签名(bool + 可选 user_id 出参,chat 的 void 版调用点兼容)。专用测试合并为 `session_registry_tests`,chat_validation_tests 里 4 个重复用例删除。覆盖率保持 100%。
- [x] **消除跨服务直接编译对方源码**:(2026-09 完成)session registry、`auth_client`、`redis_session_manager`、`notification_client` 全部沉淀进 `libs/network`(chirp_network 并 PUBLIC 链接 chirp_protos,保住 .pb.h 的构建顺序依赖);gateway / chat / app_gateway / notification 四个服务的 CMake 不再编译对方的 .cc,全部只链接 chirp_network。namespace 保留原样(`chirp::gateway` / `chirp::notification`,反映对话平面)。附带:`run_coverage.sh` 新增 `--fresh`(源文件移动/删除后必须用,否则孤儿 gcda 混入统计),并在用法注释中写明 CI 同款 `CMAKE_ARGS` toolchain 约定。

### P1 — 登录语义统一(对应 migration path 第 2/4 步)

- [ ] **统一登录/会话语义**:gateway 与 chat 各自处理 `LOGIN_REQ`,且 chat 版是 "token 即 user_id" 脚手架(`services/chat/src/main.cc`)。目标拓扑中 chat 应成为内部服务,由 `game_gateway` 吸收直连入口,客户端只认边缘。(2026-09 已定方案并落地 chat 侧验签:HS256 HMAC 本地验签——`--token_secret` 非空时,chat 的 `LOGIN_REQ` 要求 token 为带 `exp`(必填)、`sub` 的 JWT,用 `libs/common` 的 `JwtSignHS256/JwtVerifyHS256` 本地校验,过期/签名/缺 claim 一律回 `AUTH_FAILED`;secret 为空则保持脚手架模式,协议无变化。无新服务依赖、无 Redis/MySQL 也可运行;吊销靠短 TTL,后续混合方案可无缝叠加。gateway 侧仍走 `AuthClient` RPC,未动。)剩余:gateway 吸收直连入口的拓扑决策、不透明 token/混合模式(见 `docs/architecture.md` 凭证模型一节)。
- [ ] **设备级会话核心**:Redis session registry 从 "user → instance" 升级为 "user → device → edge instance",支撑 app 边缘与跨端语义(跨设备投递、kick 策略、统一未读数)。
- [ ] **玩家聚合平面模型(app_gateway 的目标形态)**:两类玩家边缘定位不同——游戏边缘(SDK/game_gateway/server 平面)**面向游戏接入、不做聚合**(一个接入可覆盖同一运营方的多款游戏,身份是游戏级);app 边缘是**玩家聚合平面**:player 身份(平台级)↔ 多个 game 身份的绑定注册(由游戏后端经服务器平面主张绑定)、跨游戏频道订阅与聊天 fan-in(统一未读)、跨游戏语音组队(语音身份=玩家)。当前 `app_gateway` 只有连接骨架 + 6xxx 转发,聚合模型未实现。与设备级会话核心同批设计;需一并决策"app 平面如何触达各游戏 chat 数据"(共享多租户核心 + 游戏命名空间 vs 联邦桥接),见 `docs/architecture.md`「Game-facing plane vs player aggregation plane」。

### P1 — 测试与构建一致性

- [x] **auth 单测**(从 P2 上调:auth 是 Supported 服务且在登录关键路径上,零单测风险高于构建洁癖):(2026-09 完成)`auth_stores_tests` / `auth_service_tests` 覆盖 user_store / session_store / rate_limiter / brute_force 等全部 enhanced 路径,auth 包行覆盖 100%。
- [x] **修复 chat 增强构建功能缺失**:(2026-09-15 完成)MySQL 增强分支(main_enhanced)与 basic/distributed 的能力差异全部补齐——① 服务器平面集成:`inject_consumer.cc` + `server_gateway_peer.cc` + `npc_uplink.cc` 编入增强构建,`--server_gateway_host` 拨号 hub,注入经 `InjectHooks` 适配到 hybrid store(在线 `SendChatNotify`+Acknowledge,离线 `AddOfflineMessage`+推送,群组走 router 广播),`--npc_service_id` 玩家→NPC 私聊发布事件;② 推送桥:`--notification_host` 接 `PushBridge`,离线入队即触发;③ 登录验签:`--token_secret` 的 HS256 JWT 本地验签与 distributed 同款;④ 离线链路:`SendChatMessageCount` 按投递计数入队,Redis 不可用落内存兜底(单测 `OfflineQueueFallsBackToMemoryWhenRedisDown`)。背景:该缺失曾让 CI 的 `--smoke-npc` 挂死(注入无人消费,listen 无超时),探测修复见 `test_services.sh` 的 hub 认证探测。
- [x] **推送桥覆盖全部 chat 构建**:(2026-09-15 完成)basic 与 distributed 之外,`main_enhanced` 也已接入(离线私聊与注入离线入队触发 `NotifyOffline`,`--notification_host` 配置通知服务,未配置时为 no-op)。
- [x] **proto 改为链接 `chirp_protos` 静态库**:(2026-09 完成)10 个服务、benchmark 工具与单测目标全部改为链接 `chirp_protos`(PIC 静态库,可链入 SDK 动态库);`sdks/core` 保留 TARGET 守卫——树外独立构建仍编译自带 gencode。

### P2 — 功能缺口与边缘硬化

- [x] **server plane 进程级 E2E**:已由 `--smoke-npc` 覆盖(注入 + 事件回环 + 离线 refill)。如需通用注入(非 NPC)场景的 smoke,再单独立项。
- [x] **chat 直连入口的限流/安全模型**:(2026-09 完成)`ChatRateLimiter` 固定窗口计数——登录按客户端 IP(30/分钟)、消息发送按用户(120/分钟),Redis 计数、任何故障一律 fail-open;超限回 `RATE_LIMITED`(common.proto 新增错误码)。阈值可配(`--login_rate_limit_per_min` / `--send_rate_limit_per_min`),无 `--redis_host` 时不生效。多级窗口/封禁列表等留给统一登录(P1)之后。
- [x] **NPC 回复去重**:(2026-09 完成)`NpcResponder` 按事件 id(= `inject_id`)去重——回复注入成功才记入 1024 条的近期窗口;已答事件重投只补 ack 不再回复,注入失败的事件不记录(重投必须重试),窗口满驱逐最旧。见 `docs/server_plane.md` NPC dialog 一节。
- [ ] **app 边缘 TLS**:`app_gateway` 的 WS/TCP 监听尚无 TLS(docs 多处标注 "TLS planned")。
- [ ] **真实推送传输**:(2026-09 推进)`HttpPushTransport` 落地——真实 HTTP/1.1 客户端(URL 解析/请求构建/状态与响应解析/整请求 deadline/响应体上限),`--push_transport http` 启用,默认仍为 logging stub;TCP 连接工厂在 `HttpConnectionFactory` 接缝后,单测以脚本化连接全覆盖 + loopback 真连回环。**剩余**:TLS 握手与 APNs HTTP/2(需 OpenSSL/nghttp2;本机 vcpkg 的 ncurses 端口在 gcc 15 下构建失败,阻塞 openssl 安装),FCM HTTP v1 的 OAuth2(RS256)同因缺 OpenSSL 未做;三者都是接缝替换点,协议代码无需再动。
- [x] **app_gateway / voice 单测**:(2026-09 完成)social 已由 `social_presence_tests` 覆盖;本批补齐剩余两个——`voice_tests`(房间创建/加入/满员/换房/离开/心跳/断连 + ICE/SDP 定向中继,22 例)、`app_gateway_tests`(scaffold 登录/登出/踢下线/心跳 + 设备消息经真实 NotificationClient 转发到 loopback 服务,17 例)。顺带修 voice 三个缺陷:满员 join 先拒后改状态(原会污染前房映射)、ICE/SDP 按 `to_user_id` 定向(原永远广播)、join 成功时绑定会话(原广播与断连清理永远找不到会话)。两服务均为 Experimental,main.cc 不在覆盖率测量范围。

### P3 — 暂缓项与杂项

- [ ] **Web 版伴侣 app**(低优先级,已加入计划):浏览器端 Discord 式 web app(现状:`apps/mobile_companion` 是移动雏形,`sdks/` 无 web SDK)。不排期;开始做 app 端时**先 web 后 Android/iOS**——web 一套代码即可在桌面/移动浏览器复用,且不依赖应用商店审核,验证成本最低。前置依赖:P1 统一登录/会话语义(web 端走 app_gateway 边缘,而非直连 chat)。
- [ ] **MsgID 去中心化**(暂缓):单一全局枚举意味着任何服务加消息都要改 `proto/gateway.proto`,但当前规模下中心化枚举天然防号段冲突,是优点;多团队并行开发时再评估按平面拆分。
- [ ] **容量基准实测**:旧 roadmap 的 "10k+ 并发" 宣称需实测证据后方可对外使用(见 `docs/architecture.md`)。
- [x] **命名冗余/历史包袱**:(2026-09-16 完成)`websocket_utils.h`(客户端 inline 函数)并入 `websocket_util.{h,cc}`,双名消除;`presence_manager_v2` 改名 `presence_manager`/`PresenceManager`(v1 从未存在,KNOWN_UNCOVERABLE 条目同步);sha256 "双实现"经核实不成立——全项目唯一实现是 `libs/common/sha256.{cc,h}`(JWT HS256、token 摘要),libsodium 只负责 auth 的 Argon2id 密码哈希与随机 token 字节,边界已写入 `libs/common/sha256.h` 头注释。`presence_manager_v2` 之外的文档(roadmap_history.md)为历史归档,保持原样。
- [x] **清理覆盖率产物**(2026-09 完成):`.gitignore` 早已覆盖,但 `coverage_html/` 与 `coverage-packages.csv` 曾被提交入库,本次连同根目录未跟踪的 `*.gcov` / `*.gcov.json.gz` / `build-cov/` 一并删除(入库部分以 git 删除提交)。
- [x] **移除死代码**(2026-09 完成):`services/router/` 空目录及顶层 CMakeLists 中被注释的 `add_subdirectory(services/router)`。
