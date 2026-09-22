# Chirp 能力矩阵(Capability Matrix)

最后核对:2026-09-22

> 二进制命名约定:`chirp_<plane>_<service>`。游戏平面:`chirp_game_sdk_gateway`、`chirp_game_server_gateway`、`chirp_npc_dialog`;App 平面:`chirp_app_sdk_gateway`、`chirp_app_auth`、`chirp_app_notification`;共享 chat 二进制为 `chirp_chat` / `chirp_chat_distributed`,由 `services/shared/chat/` 一份代码产出,部署角色由启动参数(hub/spoke)区分。

本文按运行目标(runtime target)描述仓库的当前实现状态,而不是按路线图愿景。

## 状态图例

- `Supported`:纳入默认文档化后端主干,必须保持可构建
- `Experimental`:实现了一部分,或在替代目标 / 特定环境依赖之后
- `Demo`:主要用于展示或本地探索,不是可靠的后端契约
- `Stub`:占位、mock 驱动,或明显未完成

## 后端服务

| 领域 | 运行目标 | 状态 | 说明 |
| --- | --- | --- | --- |
| 游戏边缘登录/会话路由 | `chirp_game_sdk_gateway`(`services/game/sdk_gateway/`) | Supported | TCP 5000 / WS 5001,游戏客户端边缘:登录、心跳、踢线流程的主入口;跨实例踢线按设备作用域(同用户+同设备顶替,不同设备共存)。设置 `--chat_host` 后,会把聊天业务包(2xxx)经一条每客户端内部连接转发给 chat(SERVER_AUTH_REQ 信任门 + 登录重放,双向原样中继;管道断开则踢掉客户端使其重连) |
| App 平面认证基础 token 流程 | `chirp_app_auth`(`services/app/auth/`) | Supported | 默认二进制可用;没有 MySQL/libsodium 时回退到更简单的 token 校验路径。增强模式把 `LOGIN_REQ` 的 token 按 HS256 JWT 校验(`--jwt_secret`,校验 `exp`);scaffold 回退藏在 `--allow_scaffold_login`(默认关)后面,部署上约定该密钥与边缘服务的 `--token_secret` 对齐。**只服务 App 平面**,游戏平面由游戏后端自签 JWT + `chirp_chat --token_secret` 本地验证,不依赖此服务 |
| 聊天基础消息 | `chirp_chat`(`services/shared/chat/`) | Supported | 同一二进制按需部署为 `game_chat` 或 `app_chat`。私聊、历史、离线队列、群管理(建群/入群/退群/踢人/邀请/成员列表)、已读回执、正在输入提示、消息回应、消息编辑/删除(含管理员与批量删除)、@提及解析与自动补全、敏感词过滤(`--word_filter_file` 词库 + `--word_filter_policy replace|reject|record` 三级策略,mtime 惰性热更新,文件缺失 fail-open)、消息长度限制(按频道码点上限:私聊 200/世界 100/系统公告 500 字,TEAM/GUILD/MARQUEE 不限,超长拒回 INVALID_PARAM)、按频道节奏限流(同用户同频道最小间隔:世界 5s/公会 2s/私聊 1s,窗口锚定最后放行的发送,超频回 RATE_LIMITED)、以及直连入口限流(每 IP 登录 / 每用户发送的固定窗口,Redis 支撑,故障放行;enhanced 形态的限流闸默认关,`--login_rate_limit_per_min`/`--send_rate_limit_per_min` 显式开启)都接进了默认二进制。边缘过渡期直连客户端入口(7000/7001)保持开放;设置 `--gateway_service_secret` 后,通过 SERVER_AUTH_REQ 信任门的连接(网关管道)跳过每 IP 登录限流;设置 `--token_secret` 后,登录要求 HS256 JWT,密钥与 auth 的 `--jwt_secret` 对齐 |
| 聊天分布式路由 | `chirp_chat_distributed` | Experimental | 独立目标,不是默认文档化的服务二进制 |
| 聊天 Redis + MySQL 混合存储 | `chirp_chat` / `chirp_chat_enhanced` | Experimental | MySQL 可用时,默认 `chirp_chat` 目标直接构建增强实现;`chirp_chat_enhanced` 是 `chirp_chat` 的兼容别名(CMake ALIAS) |
| App 认证注册 / 刷新 / 防爆破 / 限流栈 | `chirp_app_auth` / `chirp_app_auth_enhanced` | Experimental | MySQL 和 libsodium 可用时,默认 `chirp_app_auth` 目标直接构建增强实现;`chirp_app_auth_enhanced` 是 `chirp_app_auth` 的兼容别名(CMake ALIAS)。`LOGIN_REQ` 接受有效访问 token(HS256 JWT)或活跃会话;scaffold 登录需要 `--allow_scaffold_login 1` |
| 游戏后端注入枢纽 | `chirp_game_server_gateway`(`services/game/server_gateway/`) | Experimental | 服务认证(`service_id` + secret)、面向 chat 的消息注入(inject)路由、可靠事件投递(每服务队列、ack、重连重投);chat 把注入当内部 peer 消费;上游注入也可走 Redis Stream(`--broker_redis_host`,消费者组 + `XAUTOCLAIM` 重放,需要 Redis >= 6.2;下行事件仍只走长连接)。玩家身份绑定(WP-8 切片 1):游戏后端经 5013-5020 RPC 绑定平台 `player_id` <-> `(game_id, game_user_id)`(绑定带 `binding_id` 幂等键,按 id 或键对解绑,按玩家列举,按游戏用户反查);内存注册表 + 直写 Redis 镜像(`--binding_redis_host`,尽力而为,降级为纯内存)——见 `docs/server_plane.md` 的"玩家身份绑定"。玩家频道订阅(WP-8 切片 2):5021-5026 RPC 在 `SubscriptionRegistry` 里登记 玩家 -> (game_id, channel) 意图(三元组唯一,自服务路径由服务端铸造 `subscription_id` 幂等键,按 id 或完整三元组解绑,列举可按 game 过滤;同样是直写 Redis 形态,`--subscription_redis_host`)。扇入投递(WP-8 切片 3):带 `game_id` + 非 PRIVATE 频道的注入,对每个 (game_id, channel_id) 订阅者按玩家各发一份 `SENDER_SERVICE` 私聊副本,走 chat 正常投递尾段(chat 侧零改动);无订阅者静默回 OK,部分失败回 OK,全失败回 `SERVER_UNAVAILABLE`(可安全重放),超过 `--max_fanout`(默认 10000)回 `RATE_LIMITED`;流式 broker 共用同一套语义,见 `docs/server_plane.md` 的"玩家频道订阅"。统一未读(WP-8 切片 4):`UnreadLedger` 按(玩家, 游戏, 频道)给每份成功投递的扇入副本计红点(独立于 chat 已读游标;退订不清零),经 `MARK_CHANNELS_READ`(5027,分层选择器——单频道 / 单游戏 / 全部,幂等)与 `GET_UNREAD_SUMMARY`(5029,按(游戏, 频道)稳定排序 + 过滤后总数)读写;同样是直写 Redis 形态(`--unread_redis_host`,已清空条目直接删除,不会复活);见 `docs/server_plane.md` 的"统一未读" |
| NPC 对话(关键词规则引擎) | `chirp_npc_dialog` | Experimental | 纯 server plane 客户端(无玩家侧监听):chat 把 `npc:` 前缀的私聊转成 `npc.player_message` 事件,服务按关键词表回话,以 `SENDER_NPC` 注回(至少一次;hub 重投可能造成回复重复)。进程级冒烟:`./test_services.sh --smoke-npc` |
| 离线消息推送触发 | `chirp_chat`(默认 + distributed 构建) | Experimental | 离线私聊/群聊消息与 server plane 注入会经 `PushBridge` 入队推送 -> notification(发完即忘,失败记日志)。已接入默认构建与 `chirp_chat_distributed`(后者只在无实例投递成功时才存离线,依据 router 的 PUBLISH 接收方计数);`main_enhanced`(MySQL 构建)尚未接线,等一个能编译增强分支的构建环境 |
| 社交 / 在线状态 | `services/social` | Experimental | 服务代码在,但未作为核心路径验证 |
| 语音信令 / WebRTC 集成 | `services/voice`, `sdks/core/modules/voice` | Experimental | 信令面完整(61 个单测,TSan 干净):定向 SDP offer/answer/candidate 中继、LOGIN 认证门(`--token_secret`;默认 scaffold 自报)、join 响应携带 coturn REST 短期凭证、静音/闭麦及其派生参与者状态、空闲连接清扫。环境依赖重,不在最小验证路径内——还没有真实浏览器/媒体 E2E,WebRTC 媒体面端到端仍未验证 |
| 组队信令(跨游戏组队) | `services/party` | Experimental | 信令面完整(46 个单测,TSan 干净):邀请-接受入队(无加入码,重复邀请幂等)、就绪检查、离队/掉线自动继任队长、最后一人退出静默解散、踢人/转让队长带快照扇出(`PARTY_STATE_CHANGED` 到达包括操作者在内的每个成员)。组队快照直写 Redis(邀请仅存内存,10 分钟惰性过期);最后一台设备断线不解散队伍。玩家面身份与游戏侧语音房间解耦;单实例,无跨实例扇出;web 伴侣端已带组队 UI(快照驱动的第三条 websocket),Flutter/引擎客户端随阶段 3 落地 |
| App 通知投递 | `chirp_app_notification`(`services/app/notification/`) | Experimental | 协议面已在 TCP 5006 / WS 5016 上线(6xxx 设备与推送消息,单测覆盖 100%);进程内设备注册表、按用户冷却与载荷构建都是真实现。`--push_transport http` 打开真实 HTTP/1.1 provider POST 客户端(`HttpPushTransport`,TCP 连接工厂留有可注入接缝,deadline/大小上限,已回环测试);TLS 握手与 APNs HTTP/2 要等构建带上 OpenSSL/nghttp2,默认仍是日志传输 |
| App 边缘(伴侣应用) | `chirp_app_sdk_gateway`(`services/app/sdk_gateway/`) | Experimental | TCP 5200 / WS 5201,外加可选 TLS/wss 监听(`--tls_port`/`--ws_tls_port`,默认关;任一开启需 `--tls_cert`/`--tls_key`,TLS 1.2+):与游戏网关一致的登录/心跳/会话绑定(设备作用域跨实例踢线),外加 6xxx 设备消息转发到 `chirp_app_notification`(要求已认证会话,`user_id` 由服务端钉死);TLS 会话与明文走同一套注册表/认证路径。聊天管道已上线:配置 `--chat_host` 后,聊天业务包(2xxx)经每客户端的 `ChatBridge` 原样中继到 `chirp_chat`(`--chat_service_secret` 必须与 chat 的 `--gateway_service_secret` 一致,否则每次登录都会被 5 秒握手超时踢掉;`--chat_host` 留空则边缘忽略 2xxx)。玩家聚合目标模型(玩家身份关联 N 个游戏,跨游戏订阅/语音/聊天扇入)记录在 architecture.md;WP-8 聚合面(身份绑定 5013-5020 / 订阅 5021-5026 / 未读 5027-5029)2026-09-22 已整体从 `chirp_game_server_gateway` 迁入 `app_chat`(`chirp::chat::PlayerDirectory`,扇入与未读自增随 `FanoutChannelMessage` 落地)。**跨平面回复(2026-09-22)**:App 玩家 `SEND_MESSAGE` 的频道带 `<game_id>:<bare>` 前缀时,hub 侧 `RelayGameReply` 反查在线 spoke(`service_id_for_game`)与发送者游戏身份(`ResolveGameUser`)后经 `PEER_INJECT_MESSAGE_NOTIFY` 注入 game spoke(裸频道 ID、`game_user_id` 发送者,游戏侧铸消息 ID 走离线队列尾段);回码 OK/SERVER_UNAVAILABLE/INVALID_PARAM,拒绝不降级本地频道(详见 server_plane.md「跨平面回复」)。**遗留断链(2026-09-22)**:`--sg_host` 自服务链(5021-5029 经长连 `ServerGatewayPeer` 转发)目标仍是 game 平面的 server_gateway——搬迁后该枢纽不再承载这些 RPC(无响应,调用方超时),此链需在 app_sdk_gateway 对接 app_auth 时一并把转发目标切到 `app_chat`(经 `SERVER_AUTH_REQ` 信任门直发 WP-8 RPC);当前订阅自服务与未读自服务经此边缘不可用 |
| 搜索服务 | `services/search` | Experimental | 代码在树里,尚未确立为已验证路径 |

## SDK 与应用

| 领域 | 目标 | 状态 | 说明 |
| --- | --- | --- | --- |
| C++ 核心 SDK | `sdks/core` | Experimental | `chirp::sdk::ChatClient`:TCP 长连接协议核心(长度前缀 Packet 帧、sequence 关联请求(带响应 msg-id 校验与超时)、notify 订阅、25s 心跳(pong 回声校验 + 连续未答判死)、500ms→15s 带抖动退避重连、KICK 终态)。桌面游戏与 Unreal SDK 共用。73 例 loopback 套件进 CI,外加进程级冒烟(`./test_services.sh --smoke-sdk`)。从未编译过的 `chirp::core` 模块层已在 2026-09 评审中删除;五个钩子接口(拦截器/认证提供者/存储/监听器/命令)已接入 ChatClient |
| Unity SDK | `sdks/unity` | Experimental | 纯 C# 协议栈,2026-09 重写(旧桥从未编译过,已删):`Runtime/Chirp/` 是无 Unity 依赖的协议库——长度前缀 Packet 帧(16MB 上限)、`ChirpClient` 状态机(25s 心跳带 pong 回声校验、抖动指数退避重连、KICK 终态、10s 请求超时、sequence 关联、`Reconnecting`/`Reconnected` 事件)和完整的 Req/Resp 规格表(web msg_map 29 对全等 + 语音面 7 对:建房/进房/退房/房间信息/静音/闭麦)——外加把回调重放到主线程的 `ChirpManager` MonoBehaviour(AutoRelogin 重连后自动重放 LOGIN、聊天高频推送主线程事件、`CreateClient`/`RunOnMainThread` 支撑组队/语音/社交第二连接)。gencode 提交在 `proto/csharp`(Google.Protobuf 3.27)。16 例 xunit 套件在 CI 中脱离 Unity 运行(`unity-sdk.yml`:钉版本 protoc 重生成 + 漂移检查 + `dotnet test`);Unity 集成 = 把 `Runtime/` + `proto/csharp/` 拷进 Assets(见 `sdks/unity/README.md`) |
| Unreal SDK | `sdks/unreal` | Experimental | 覆在 C++ 核心上的薄 UE 插件壳:`UChirpClientSubsystem`(GameInstance 子系统)用 AsyncTask 把所有原生回调编组到游戏线程,暴露 Blueprint 事件(登录结果、聊天消息、踢线、断线、原始 notify)和连接状态枚举。核心在 chirp CI 里编译;UE 层本身需要 UBT(构建契约记录在 sdks/unreal/README.md) |
| Go 服务端 SDK | `sdks/go` | Experimental | 游戏后端拨出连接 `chirp_game_server_gateway` 的客户端(仓库根 Go module 下的 `chirp` 包):service_id+secret 认证握手、服务端指定心跳节奏、sequence 关联 RPC(`InjectMessage` / `PublishEvent` / `AckEvents` / 玩家身份绑定 `BindPlayerIdentity` / `UnbindPlayerIdentity` / `GetPlayerIdentities` / `ResolveGameUser` / 玩家频道订阅 `SubscribePlayerChannel` / `UnsubscribePlayerChannel` / `GetPlayerSubscriptions` / 未读红点 `MarkChannelsRead` / `GetUnreadSummary`,context 超时,非 OK 码转类型化错误)、inject/event 通知处理器、失败挂起重连——语义与 C++ 参考 peer(`libs/network/server_gateway_peer.cc`)一致。重生成的 `proto/go` 包(每个 proto 一个)在 CI 做漂移检查(`go-sdk.yml`:钉 protoc 33.4 + protoc-gen-go v1.36.12,`go vet`,12 例竞态开跑套件,对进程内 fake hub) |
| 移动伴侣应用 | `apps/mobile_companion` | Experimental | 纯 Dart 协议栈上的 Flutter 应用(旧 FFI 桥已删):生成 protos 在 `proto/dart`(包 `chirp_proto`),移植的 `ChirpClient`(长度前缀 Packet 帧、sequence 关联请求/响应、25s 心跳、指数退避自动重连、KICK 终态处理),与 web 伴侣端对称的 store + api 层。登录带跨设备顶号横幅、四 Tab 首页(会话/好友/组队/我的)、私聊 + 群聊含历史分页、已读回执、正在输入、消息回应、编辑/删除、乐观发送与离线排队、后台消息本地通知。四条可降级 websocket 与 web 应用完全一致(chat 7001 / social 8001 / party 7501 / app_gateway 5201)。同一套代码构建 linux/windows/macos 桌面版(CI 出 debug 包;release 打包靠后)。40 例套件(协议/状态/api/组件);android 构建、桌面构建矩阵、analyze 与测试进 CI(`mobile-build.yml`)。仍是过渡期直连拓扑,与 web 一起迁向 app_gateway 聚合边缘 |
| Web 伴侣应用 | `apps/web_companion` | Experimental | Vite + React + TS strict + MUI;经四条可降级 websocket 连真实后端(chat 7001 / social 8001 / party 7501 / app_gateway 5201):登录带跨设备顶号、私聊(历史/已读回执/正在输入/消息回应/编辑-删除)、完整群管理、服务端权威的好友 + 在线状态、快照驱动的组队 UI(建队/邀请/就绪/踢人/转让/离队/解散)、设备管理(经认证门控的 app_gateway 转发路径把浏览器自动注册为推送目标,带列表/注销 UI)、桌面通知(基于实时聊天流的 Notification API;真正的 Web-Push 等后端传输层)。在 app_gateway 聚合边缘就绪前保持直连过渡拓扑;单测 + 组件套件(226 例)带真实后端冒烟脚本 |
| 管理后台 | `apps/admin_dashboard` | Stub | 用 mock 数据和演示页面,没有真实后端集成 |
| CLI 客户端 / 基准工具 | `apps/cli_client`, `tools/benchmark` | Demo | 适合冒烟测试和手工验证 |

## 测试与交付置信度

| 关注点 | 当前状态 | 状态 |
| --- | --- | --- |
| 单元测试 | `tests/unit` 下 34 个套件;凡链接进测试二进制的后端包,行覆盖按 `scripts/run_coverage.sh` 都是 100%(仅限已登记的 `KNOWN_UNCOVERABLE` 豁免)。`chirp_app_sdk_gateway`、`chirp_voice`、`chirp_party` 有套件(`app_sdk_gateway_tests`、`voice_tests`、`party_tests`),但它们的 `main.cc` 不在覆盖测量范围内 | Supported |
| 标准本地构建经 `ctest` 跑测试 | `ctest --preset dev`(gcov 构建用 `--preset coverage`);全新树可构建并通过 | Supported |
| CI 把测试失败当硬失败 | `ci.yml` 跑 Debug + Release 构建 + `ctest`,外加一个 coverage job:任何包行覆盖跌破 98% 即失败 | Supported |
| 进程级冒烟覆盖 | `test_services.sh --smoke / --smoke-chat / --smoke-sdk / --smoke-npc / --smoke-edge / --smoke-jwt / --smoke-redis` 全部跑在 CI smoke job 里(`--smoke-redis` 用 docker redis 验跨实例踢线) | Supported |
| 核心服务的 Docker Compose 路径 | 已具备 | Supported |
| 路线图与默认构建产物一致 | 是——TODO.md 是活的路线图(2026-09 重写);已完成项在 README.md 中划线 | Supported |

## 推荐的对外口径

对外介绍这个仓库时,当前合适的说法是:

- 一条受支持的核心后端主干:`gateway + auth + chat`
- 一个试验场:分布式聊天、更完整的认证、语音、社交、多引擎 SDK 都在这里试
- 移动端/管理端按演示仓库对待,而不是成品套件
