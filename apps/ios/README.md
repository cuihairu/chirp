# chirp iOS — companion app + protocol core package (Swift)

SwiftPM package porting the pure protocol core of the former Flutter app
(`apps/mobile_companion`, removed 2026-09-29 once both native packages
carried its full test-vector groups), same migration path the Android
package took (its M1→M4 batches, minus the app shell). Gate: `swift test`
— **221 tests, all green** (92 protocol core incl. the 11 lexicon-sync
vectors + 129 app core; verified on the CI macOS runner since the P5 batch
— no local iOS verification, see the companion-app section).

## Layout

| Path | Ported from | Role |
|---|---|---|
| `Sources/ChirpProtos/proto/*.pb.swift` | `proto/*.proto` | committed gencode, `protoc-gen-swift` 1.38.1 with `Visibility=Public` (cross-target access; the runtime dep is pinned to the same 1.38.1). Regenerate: `protoc --plugin=<path>/protoc-gen-swift --swift_out=apps/ios/Sources/ChirpProtos --swift_opt=Visibility=Public -I . proto/*.proto` from the repo root |
| `Sources/ChirpProtocol/Frame.swift` | `lib/protocol/frame.dart` | u32-BE length-prefix framing, 16 MiB cap, stream decoder, byte-array in/out |
| `Sources/ChirpProtocol/MessageSpec.swift` | `lib/protocol/msg_map.dart` | all 48 req/resp MsgID↔type pairs with protobuf decoders + the type-erased `all` table (41 through P4c + the 2026-10-03 voice seven from P4d) |
| `Sources/ChirpProtocol/ChatConnection.swift` | `lib/protocol/chirp_client.dart` | full state machine (`idle/connecting/connected/waitingReconnect/kicked/closed`), sequence correlation, typed request futures (`Promise`), request deadlines, missed-pong heartbeat (`maxMissedPongs=2`), auto reconnect with jittered exponential backoff, KICK terminal semantics, clock offset |
| `Sources/ChirpProtocol/WordFilter.swift` | `WordFilter.kt` (Android M3) | server lexicon parsing, ASCII-only case folding, mask spans with adjacent-run collapse, REPLACE/REJECT; matching runs on UTF-16 code units so mask spans line up with the Kotlin/dart ports; `WordFilterLoader` reads the server lexicon wire format (CRLF/LF/CR) |
| `Sources/ChirpProtocol/WordFilterSync.swift` | `WordFilterSync.kt` (lexicon-download batch 2026-10-01) | WORD_FILTER_FETCH conditional GET (`known_version` hit omits text), version gate drops stale payloads, update-notify swap-in, fetch never throws (failure keeps the local fallback lexicon), server-disabled clears the local pre-check |
| `Sources/ChirpProtocol/Hooks.swift` | `Hooks.kt` (Android M4) | pipeline seams: `SendOptions`, `MessageInterceptor`, `AuthProvider`, `MessageStore` + `MemoryMessageStore`, `ChatEventListener`, `CommandHandler`, `ChirpArgumentError` |
| `Sources/ChirpProtocol/ChatPipeline.swift` | `ChatPipeline.kt` (Android M4) | login token chain (explicit > provider > userId, one AUTH_FAILED renewal), send validation order (connection state first), `/`-command routing, interceptor rewrite/block, archive, push fan-out (KICK delivered once per connection), re-entrant start/stop |
| `Sources/ChirpProtocol/OfflineSendQueue.swift` | `OfflineSendQueue.kt` (Android M4) | at-least-once replay: any server response confirms; CLOSED/TIMEOUT keeps the entry and stops the batch (tail stays queued in order); BLOCKED/argument errors drop; clientId dedupe; beyond `maxQueued` (50) the oldest is evicted |
| `Sources/ChirpProtocol/DeviceRegistrar.swift` | `DeviceRegistrar.kt` (Android M3.5) | device-plane registration (app_gateway WS 5201) with the async `PushTokenSource` seam: exactly-once delivery guard, throwing source contained, empty token still registers (dart degradation), server ErrorCode passthrough, connection RequestError passthrough; Swift-side addition `PushTokenSlot` (fcm/apns/pushKit — Kotlin only ever fills fcm; iOS writes `apns_token`, the server stores all slots verbatim) |
| `Sources/ChirpProtocol/WsTransport.swift` | `lib/protocol/ws_transport.dart` | transport seam: open/onBinary/onClosed/send/close |
| `Sources/ChirpProtocol/WsTransportDarwin.swift` | `OkHttpTransport.kt` (Android M2) | Darwin real transport: `URLSessionWebSocketTask` adapter; text frames dropped; onClosed announced exactly once after open succeeds; pre-open failure reports through open's result |
| `Sources/ChirpProtocol/Scheduler.swift` | dart event-loop timers | time seam; tests drive a `ManualScheduler` virtual clock |
| `Sources/ChirpProtocol/Promise.swift` | dart `Future`/Kotlin `CompletableFuture` | settle-once future; combinators `map`/`flatMap`/`handle` are the thenApply/thenCompose/handle mapping |
| `Sources/ChirpProtocol/RequestError.swift` | `lib/protocol/errors.dart` | timeout/closed/kicked (+ server/blocked used by the pipeline) |
| `Tests/ChirpProtocolTests/` | the Kotlin test files | same vector groups — conformance with the Kotlin package (the dart suite was removed with the Flutter app on 2026-09-29; this batch backfilled its four only-there vectors): Frame 6, ChatConnection 16, MsgSpecs table 3, ChatPipeline 27, OfflineSendQueue 9, Hooks 6, WordFilter 7, WordFilterSync 11 (lexicon batch), DeviceRegistrar 7; plus `Tests/ChirpAppCoreTests/` 129 for the app core (ChatSessionService 28, SessionIndex 14, SessionChannel 3, GroupIndex 5, FriendIndex 9, PresenceIndex 7, OnlineDeviceIndex 5, ReactionIndex 5, TypingIndex 4, PartyIndex 4, PartyPlaneService 5, VoiceIndex 5, VoicePlaneService 6, ApnsRegistration 12, PushRoute 8, P1 units 9) |

Dart's single event loop becomes one recursive lock (transport callbacks and
scheduler ticks arrive on foreign threads; `close()` re-enters through the
synchronous down event, so a plain `NSLock` would self-deadlock). `Scheduler`
and `RandomSource` are injectable — every backoff/heartbeat/deadline test is
deterministic on the virtual clock.

Kotlin runtime hook semantics map to Swift as follows: every hook method is
declared `throws` and every call site wraps it in `try?`, so "hook threw"
degrades exactly like the Kotlin catch blocks (drop / block / decline) and
the same vectors stay runnable.

## Gate

```sh
cd apps/ios && swift test    # 221 tests, XCTest
```

CI: `ios-app.yml` (macos-latest) runs the package tests on the real
macOS toolchain and builds the generated app project for the iOS
Simulator (see the companion-app section below). **Since the P5 batch the
gate is CI-only** — no local iOS/Xcode verification; code lands, the run
watches, red gets fixed to green. Local toolchain of record for the
earlier batches: Swift 6.4 at `~/swift/usr/bin` (swift.org linux
tarball). The gencode plugin was built from the same swift-protobuf tag:
`git clone --branch 1.38.1 /tmp/swift-protobuf &&
swift build -c release --product protoc-gen-swift`.

## Assumptions (noted per non-interactive rules)

- **Transport seam + Darwin adapter** — `URLSessionWebSocketTask` is Darwin-only
  and does not exist in swift-corelibs-foundation. The seam (`WsTransport.swift`)
  plus the scripted fake drive the Linux gate (`swift test`); the Darwin adapter
  (`WsTransportDarwin.swift`) drops in on Apple platforms with zero
  connection-layer changes, mirroring the Android `OkHttpTransport.kt` shape
  (frame bytes moved verbatim; text frames dropped; onClosed announced exactly
  once after a successful open; pre-open failure reports through open's result).
- **Language mode .v5** (Package.swift tools 6.0 + `swiftLanguageMode(.v5)`):
  the lock-serialized port needs no Sendable surgery; strict mode 6 would
  demand `@unchecked Sendable` boilerplate without adding safety the lock
  already provides.
- **protoc on PATH is the system 3.21.12** (not the vcpkg 6.33.4 the C++
  build pins): the Swift plugin speaks the protoc plugin protocol over
  stdin, so version coupling is loose — the generated wire format is
  identical. Committed gencode keeps the repo convention (like `proto/java`).
- **Swift 6.4 gencode quirks**: types are package-prefixed
  (`Chirp_Gateway_Packet`), `device_id` renders `deviceID`; the spec table
  pins the wire numbers so a renumber cannot slip through.
- **Login platform is `"ios"`** (the Kotlin pipeline reports `"android"`);
  per-platform identity semantics, no vector depends on it. Same for the
  registrar's RegisterDeviceRequest platform field (its Kotlin vector
  asserts "android", the Swift one "ios" — the only intentional divergence).
- **APNs token source is a seam only** — the real adapter needs Apple
  credentials and lives on the Darwin side; `PushTokenSource` conformers
  that report nil (or throw) degrade to empty-token registration, the same
  placeholder-credentials posture as Android M3.5's default build.

## Companion app (ChirpCompanion, started 2026-10-03)

The app shell is SwiftUI over this package; the Xcode project is
**generated** by [XcodeGen](https://github.com/yonaskolb/XcodeGen) from
`project.yml` (the `.xcodeproj` is never committed — the spec is the source
of truth).

| Path | Role |
|---|---|
| `project.yml` | XcodeGen spec → `ChirpCompanion.xcodeproj` (`xcodegen generate`) |
| `ChirpCompanion/` | SwiftUI shell: `App/` (entry, root observable), `Views/` (login / sessions / chat / group settings / friends / party-voice / devices screens) |
| `Sources/ChirpAppCore/` | UI-free app logic inside the package: `DeviceIdentity` / `HostConfig` / `LoginDraft` (P1), the P2 closed loop — `ChatSessionService` (login→sessions→DM send/receive wiring: acks, offline queue replay, word-filter sync, event stream) and `SessionIndex` (conversation list state) — the P3 push pieces — `AwaitedPushTokenSource` (offer-then-fetch APNs token waiter, timeout degrades to nil) and `DevicePlaneService` (device-plane login → RegisterDevice on 5201) — the P4a pieces — `ReactionIndex` (quick-reaction tallies: notify increments, ADD/REMOVE_RESP aggregate/decrement, empty slots dropped) and `TypingIndex` (per-channel typing TTL 6s, self excluded) — the P4b-d plane pieces — `OnlineDeviceIndex` / `FriendIndex` / `PresenceIndex` (device mirror, friend roster, presence snapshots with TTL), `PartyIndex` / `PartyPlaneService` (7501 party plane) and `VoiceIndex` / `VoicePlaneService` (9001 voice plane), game presence via `DevicePlaneService` — and the P4e group/session-key pieces — `SessionChannel` ('p:'/'g:' navigation keys, web conversation-key isomorphic) and `GroupIndex` (group-roster mirror: replace-all + stale-row pruning) — and the P5 receive-side piece `PushRoute` (APNs userInfo → conversation-key router + foreground banner policy; the shell's AppDelegate keeps the UNUserNotificationCenter thread hops) — and the P6 persistence seam (`SessionIndex.SnapshotIO`: session-list snapshot hydrate-on-construct / save-on-mutation, shell writes UserDefaults per user; DM row swipe-to-delete lives in the shell) — so `swift test` covers it on Linux **and** macOS |
| `.github/workflows/ios-app.yml` | CI leg (macos-latest): xcodegen → simulator build (unsigned) → package tests on the real macOS toolchain |

```bash
# build (macOS): xcodegen generate, then open ChirpCompanion.xcodeproj
# tests (any host with Swift, incl. Linux):
swift test
```

### Phased plan (at a glance)

| Phase | Scope | Status |
|---|---|---|
| P1 | 工程结构 + CI 构建腿(macos-latest:xcodegen + iOS Simulator 编译证明 + 包测试上 CI);SwiftUI 三屏骨架(登录/会话/聊天,静态);`ChirpAppCore` 入包(DeviceIdentity / HostConfig / LoginDraft) | ✅ 2026-10-03 |
| P2 | 登录+会话+收发最小闭环:`ChatSessionService`(蓝本 MainActivity.kt 接线——ChatConnection/ChatPipeline/MemoryMessageStore/WordFilterSync 拦截器/OfflineSendQueue/MESSAGE_ACK 回执先于渲染/事件流)+ `SessionIndex` 会话列表 + SwiftUI 三屏真接线(连接横幅/KICK 踢下线/离线入队重放提示);服务级单测 11 例(假 transport+虚拟时钟,对拍协议包手法) | ✅ 2026-10-03 |
| P3 | 推送 APNs 客户端注册链:`AwaitedPushTokenSource`(offer-then-fetch 等待器,10s 超时 nil 降级)+ `DevicePlaneService`(设备面 5201 独连→LOGIN(GetAuthenticatedSession 守卫)→RegisterDevice)+ `DeviceRegistrar` 加 `PushTokenSlot`(iOS 写 `apns_token` 槽,Swift 侧增量)+ 壳层 `UIApplicationDelegateAdaptor` 桥(didRegister→hex token→offer,didFail→nil 降级)+ 通知授权请求;服务端投递链核对(herald ES256 `.p8` provider token/cert 兜底/topic=bundle id——服务端既有,无需改动) | ✅ 2026-10-03 |
| P4a | 快捷反应 + 输入状态 + 服务端历史:`ReactionIndex`(八枚 QUICK_REACTIONS,notify 增量/应答聚合覆盖,操作者不在 notify 扇出面 web 同款)+ `TypingIndex`(6s TTL,排自己)+ `ChatSessionService` 四方法(add/remove/sendTyping/loadServerHistory)+ 壳层接线(长按快捷反应、chips 切换、正在输入横幅、3s/5s 输入上报节流、打开会话拉服务端历史按 messageID 去重合并) | ✅ 2026-10-03 |
| P4b | 多端在线镜像 + 设备面板:`OnlineDeviceIndex`(platform 键控/offline 留 last-seen)+ `DEVICES_PRESENCE_NOTIFY` 接线 + `DevicePlaneService.loadDevices` + `DevicesView`(设备面挂只降级不伤聊天) | ✅ 2026-10-03 |
| P4c | social 好友名册 + 在线状态面:`FriendIndex`/`PresenceIndex` + 请求面六方法(字段逐条对齐 web social_api.ts)+ `FriendsView` + 登录即 `setPresence(.online)` | ✅ 2026-10-03 |
| P4d | 组队/语音房间/游戏状态面:voice 七对 spec(41→48)+ `PartyIndex`/`PartyPlaneService`(7501)+ `VoiceIndex`/`VoicePlaneService`(9001)+ 游戏状态走设备面 + `PartyVoiceView` 合并面板 | ✅ 2026-10-03 |
| P4e | 群面板 + 历史分页:`SessionChannel`('p:'/'g:' 导航键)+ `GroupIndex` + 群请求面六方法(refresh/create/invite/kick/leave/members,web chat_api.ts 同款字段与 notify→重拉口径)+ `GroupSettingsView` + 会话行群/DM 分桶 + beforeTimestamp 翻页(顶部「加载更早」+尾锚自动滚动) | ✅ 2026-10-04 |
| P4 | 余下功能面对齐 web_companion:多平面(social/party/voice/device/game_presence)、好友/群/设备/在线设备/组队/语音面板 | ✅ 2026-10-04(P4a-e 全落地;真机走查随 APNs 凭据项) |
| P5 | APNs 接收面:`PushRouteParser`(userInfo→会话键路由,只认 message/mention,chirp_data 反解——click_action 不进 APNs 载荷)+ `PushForegroundPolicy`(正看该会话才抑制横幅)+ 壳层 `UNUserNotificationCenterDelegate`(willPresent 裁决/didReceive 深链,回调跳主线程)+ `handlePushTap`(未登录暂存、登录成功应用)+ `SessionsView.onAppear` 补消费挂载前跳转 | ✅ 2026-10-04(真机投递随 APNs 凭据项) |
| P6 | 会话列表持久化 + 会话删除:`SessionIndex.SnapshotIO` 快照缝(load/save 闭包,DeviceIdentity 同款)——构造回灌 v1 JSON、变更即落盘(载入过键结构门 + DM 含自己侧的成员门、kind/peerId 从键反解、负未读钳 0、破损/异版本当全新)+ `ChatSessionService.sessionSnapshot` 注参 + 壳层 UserDefaults 按用户分键 + 列表行 DM 滑动删除(群行走退群/被踢,名单引导会重建故不开放) | ✅ 2026-10-04 |

Out of scope until credentials exist: a signed build with the
aps-environment entitlement (real device token), provisioning for physical
devices, and the server-side `.p8`/cert in herald — the P3 structure keeps
all of it degradable without them (empty-token registration, server falls
back to log delivery).

