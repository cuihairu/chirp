> 🌐 Language: [English](../../analysis/video-support-2026-11-13.md) | **中文** (this page)

# 视频支持调研：路线、复用、缺口与体量

> 2026-11-13/14 调研，对应视频立项工单（`chirp-voice-video` B 部分）。这是决策文档：涉及 Chirp 自身的每条论断都锚定源码或测试证据；涉及外部项目的论断于 2026-11-14 对照其仓库核验，最终以其 LICENSE 文件为准。

## TL;DR

- **推荐：视频走既有 P2P mesh（路线 A）。** 语音信令面转发的是不透明 SDP 体，视频 m-line 与会中重协商原样穿过。游戏开黑的真实房间规模（2–5 人）下，mesh 视频是唯一「不加有状态媒体服务就上视频」的路线。
- **路线 B（SFU）是独立立项，不是现有面的改造。** 它需要具体触发条件（房间规模超 ~6 人、单向广播、录制），且有复用形态截然不同的两种做法（见 [SFU 路线](#路线-b-sfu)）。
- **骨架已落地（experimental）：** 走未改动的语音面的真浏览器视频探测（`apps/web_companion/scripts/voice_media_e2e.mjs`，`E2E_VIDEO=1`），加 `proto/voice.proto` 的协议占位。没有发布任何视频功能；本文任何内容都不应读作「已支持视频」。
- 体量速览：**S** mesh 视频探测/产品开关（天级）、**M** 四端视频客户端面（周级）、**L** 基于 mediasoup 的 SFU 服务接入 chirp（专人一个季度）、**XL** 分布式 SFU 集群带录制/转码（在游戏真需要之前不立项）。

## 语音面现状（事实）

语音服务是**纯 SDP/ICE 信令中继**，媒体从不经过它：

| 事实 | 锚点 |
| --- | --- |
| ICE/offer/answer 三个 relay 都是经 `to_user_id` 定向 1:1 转发，门禁是房成员资格+绑定身份（`RelaySenderAllowed`） | `services/voice/src/voice_dispatch.cc`（`RelaySenderAllowed` 与 4007/4008/4009 各 handler） |
| 成员两两对连（全 mesh）；服务端没有 RTP/RTCP 端口，也不解析任何 SDP | 同文件；`services/voice/CMakeLists.txt` 无媒体依赖 |
| TURN/STUN 随 join 以 coturn REST 短期凭据下发 | `voice_dispatch.cc` 的 `MakeTurnUsername`/`MakeTurnCredential`/`AppendIceServers`；部署 runbook 见[部署指南](../../guide/deployment.md) |
| `media_tier`/`rtcp_feedback_ms` 只是**策略字段**（≤12 人 FULL_MIX/1000ms，>12 人 FORWARD_BYPASS/2000ms），字段背后没有服务端混音或转发 | `voice_dispatch.cc` 的 `MediaTierForRoom`/`RtcpFeedbackMsForTier`；如实拆分记账在 `todo.md`（VOICE-RT 条目） |
| web 客户端跑真实媒体链路且有真浏览器 E2E：假麦 chromium ×2 过真 UI，SDP 落定、远端音轨活、入向字节增长；host 候选与 coturn relay 两形态均验（选中对 `relay->relay`） | `apps/web_companion/scripts/voice_media_e2e.mjs` |
| SDK（core C++、Unity/C#）只是信令封装（请求构造+notify 回调）；无采集/编码/播放 | `sdks/core`、`sdks/csharp`（VOICE-RT ③，记账见 `todo.md`） |

对视频而言承重结论：**中继不检查 SDP**，所以服务端的「视频支持」要么是*零改动*（mesh），要么是*另一个服务*（SFU），没有中间态。

## 路线 A：既有 mesh 上的视频

按工单点名的关注面逐项过：

- **协商。** 视频轨走同一条 SDP offer/answer relay（4008/4009）。join 后加轨是重协商——同一对消息再来一轮 offer/answer；服务本就转发它从不解析的报文体。骨架探测端到端验证了这一点（`E2E_VIDEO=1`）。
- **带宽自适应。** P2P 对内 Chrome 的 transport-wide 拥塞控制与接收端反馈照常工作；每个发送端自动降级其唯一个空间层。不存在的是服务端按接收者切层——那是 mesh 的固有形态，不是缺失功能。
- **关键帧。** PLI/FIR 这类 RTCP 请求端到端直达；mesh 中途新加入的成员从既有发送端拿关键帧，服务端无需参与。
- **Simulcast。** mesh 里没有「按接收者挑层」的角色，所以 simulcast 在这里不是「缺口」——是无意义（只会双倍上行）。如实说：simulcast 是 SFU 能力，想要它就是该转路线 B 的信号。
- **真正的天花板。** 上行是 O(N−1)/端（每个成员向其他每个成员各编一份传一份），外加每端的编码 CPU。舒适区就是小房间——恰好是开黑场景。大房间先在客户端带宽上压垮 mesh，轮不到服务端限制。

## 路线 B：SFU

SFU 收每客户端一路上行，向其余成员转发（挑选后的）RTP 层。它是新的有状态媒体服务，自带扩展模型（一房一 SFU 或分片；房间跨节点很难做便宜）。两种形态：

### B1 — LiveKit 做旁挂

- Apache-2.0，Go，自述「Scalable, distributed WebRTC SFU」，基于 Pion（2026-11-14 核验）。
- 自带**客户端 SDK 与自己的 WebSocket 信令**，客户端鉴权用 JWT access token。
- 复用评估：**chirp 的语音信令面在媒体房间不复用**——它退化为 token 签发方（chirp_auth 已持有 HS256 密钥，LiveKit token 可共用签名形态）加一份花名册镜像。`chirp_voice` 的房间/成员/TURN 工作对语音面仍然有效，但视频房间会活在另一套并行栈里，有自己的生命周期、部署和故障面。
- 用最少代码买到最多能力，代价是再运维一套实时系统。

### B2 — 基于 mediasoup 自建

- ISC 许可；C++ 媒体 worker（libuv）+ Node.js/Rust 服务端 API；明确信令无关（2026-11-14 核验）。
- 复用评估：**语音面可按形态复用**——房间、定向 relay 纪律、coturn REST 凭据铸造、分级策略字段终于能管真实的旁路。但 chirp 从此自持难点：RTP 路由、simulcast 选层、关键帧节奏、带宽管理、worker 生命周期。这是媒体团队的立项，不是增量功能。

### SFU 买到什么（两种形态同）

每客户端一路上行（mesh 的 O(N−1) 天花板消失）、真实的服务端分级（`media_tier` 字段不再是空承诺）、按接收者选层的 simulcast、录制、观众级房间规模。

## 游戏场景契合

| 场景 | 路线 | 理由 |
| --- | --- | --- |
| 开黑语音 + 偶发视频，2–5 人 | **A（mesh）** | 不加服务；此规模上行无虞；管线已有 E2E 证据 |
| 小队到 ~8 人带视频 | A 边缘 | 8 人房每端 7 路上行；只在低分辨率/低码率下可行 |
| 观赛 / 直播 / 赛事叠加层 | B（L+） | 一 sender 多 receiver——典型 SFU 用例 |
| 大房间语音（>12 人，FORWARD_BYPASS 档） | B（L+） | tier 字段已在承诺一个不存在的旁路；要么做出来要么撤掉承诺 |
| MMO 就近语音 | 不在本文范围 | 空间音频+海量小房间是另一类架构问题；mesh 死于上行，SFU 死于房间换入换出——需另立项调研 |

## 体量

- **S — mesh 视频，最小版（天级）。** 已作为骨架落地（探测见下）。产品化意味着：web 弹窗加视频开关、轨级静音处理、E2E 扩到断言视频帧——全在客户端；服务端零改动。
- **M — 四端视频客户端面（周级）。** 各端采集/播放/设备管理（web 在 S 完成；core C++ 与 Unity/C# 各需一层媒体抽象——「SDK 只是信令封装」的限制在这里才真咬人）、重协商 UX、逐端测试。
- **L — mediasoup SFU 接入 chirp（专人一个季度）。** 新服务二进制 + worker 生命周期、chirp_voice 桥（房间/凭据/花名册）、真实分级实现、运维面（指标、SFU 的 TURN、容量）。只在有具体游戏需求时立项。
- **XL — 分布式 SFU 集群（录制、转码、多节点房间）。** 不做范围；等 L 存在且场景逼出来再说。

## 已落地骨架（experimental，本批）

1. `apps/web_companion/scripts/voice_media_e2e.mjs` 增加 `E2E_VIDEO=1`：两端额外采集假摄像头并把视频轨加进同一条 pc；测试断言远端视频轨活且视频帧真的在解码（`framesDecoded` 增长、`frameWidth` 为正）。这是对路线的探测，不是功能——UI 没有视频开关，也没有任何东西交付给用户。
2. `proto/voice.proto` 带一条占位注释，勾出未来视频能力字段的形状（标注 experimental，暂不预留字段号）。真正的字段等有客户端真消费时再加。

## 来源

- LiveKit 仓库：Apache-2.0，基于 Pion 的 Go SFU，JWT access token，自带 SDK/信令（2026-11-14 取证）。
- mediasoup 仓库：ISC，C++ worker + Node/Rust API，信令无关的 SFU 库（2026-11-14 取证）。
- 仓内证据：`services/voice/src/voice_dispatch.cc`、`apps/web_companion/scripts/voice_media_e2e.mjs`、`docs/guide/deployment.md`、`todo.md`（VOICE-RT 口径拆分条目）。
