# chirp iOS — companion app + protocol core package (Swift)

SwiftPM package porting the pure protocol core of the former Flutter app
(`apps/mobile_companion`, removed 2026-09-29 once both native packages
carried its full test-vector groups), same migration path the Android
package took (its M1→M4 batches, minus the app shell). Gate: `swift test`
on Linux — **101 tests, all green** (Swift 6.4, x86_64 linux; 92 protocol
core incl. the 11 lexicon-sync vectors + 9 app core).

## Layout

| Path | Ported from | Role |
|---|---|---|
| `Sources/ChirpProtos/proto/*.pb.swift` | `proto/*.proto` | committed gencode, `protoc-gen-swift` 1.38.1 with `Visibility=Public` (cross-target access; the runtime dep is pinned to the same 1.38.1). Regenerate: `protoc --plugin=<path>/protoc-gen-swift --swift_out=apps/ios/Sources/ChirpProtos --swift_opt=Visibility=Public -I . proto/*.proto` from the repo root |
| `Sources/ChirpProtocol/Frame.swift` | `lib/protocol/frame.dart` | u32-BE length-prefix framing, 16 MiB cap, stream decoder, byte-array in/out |
| `Sources/ChirpProtocol/MessageSpec.swift` | `lib/protocol/msg_map.dart` | all 41 req/resp MsgID↔type pairs with protobuf decoders + the type-erased `all` table (40 + the 2026-10-01 lexicon-fetch pair) |
| `Sources/ChirpProtocol/ChatConnection.swift` | `lib/protocol/chirp_client.dart` | full state machine (`idle/connecting/connected/waitingReconnect/kicked/closed`), sequence correlation, typed request futures (`Promise`), request deadlines, missed-pong heartbeat (`maxMissedPongs=2`), auto reconnect with jittered exponential backoff, KICK terminal semantics, clock offset |
| `Sources/ChirpProtocol/WordFilter.swift` | `WordFilter.kt` (Android M3) | server lexicon parsing, ASCII-only case folding, mask spans with adjacent-run collapse, REPLACE/REJECT; matching runs on UTF-16 code units so mask spans line up with the Kotlin/dart ports; `WordFilterLoader` reads the server lexicon wire format (CRLF/LF/CR) |
| `Sources/ChirpProtocol/WordFilterSync.swift` | `WordFilterSync.kt` (lexicon-download batch 2026-10-01) | WORD_FILTER_FETCH conditional GET (`known_version` hit omits text), version gate drops stale payloads, update-notify swap-in, fetch never throws (failure keeps the local fallback lexicon), server-disabled clears the local pre-check |
| `Sources/ChirpProtocol/Hooks.swift` | `Hooks.kt` (Android M4) | pipeline seams: `SendOptions`, `MessageInterceptor`, `AuthProvider`, `MessageStore` + `MemoryMessageStore`, `ChatEventListener`, `CommandHandler`, `ChirpArgumentError` |
| `Sources/ChirpProtocol/ChatPipeline.swift` | `ChatPipeline.kt` (Android M4) | login token chain (explicit > provider > userId, one AUTH_FAILED renewal), send validation order (connection state first), `/`-command routing, interceptor rewrite/block, archive, push fan-out (KICK delivered once per connection), re-entrant start/stop |
| `Sources/ChirpProtocol/OfflineSendQueue.swift` | `OfflineSendQueue.kt` (Android M4) | at-least-once replay: any server response confirms; CLOSED/TIMEOUT keeps the entry and stops the batch (tail stays queued in order); BLOCKED/argument errors drop; clientId dedupe; beyond `maxQueued` (50) the oldest is evicted |
| `Sources/ChirpProtocol/DeviceRegistrar.swift` | `DeviceRegistrar.kt` (Android M3.5) | device-plane registration (app_gateway WS 5201) with the async `PushTokenSource` seam: exactly-once delivery guard, throwing source contained, empty token still registers (dart degradation), server ErrorCode passthrough, connection RequestError passthrough |
| `Sources/ChirpProtocol/WsTransport.swift` | `lib/protocol/ws_transport.dart` | transport seam: open/onBinary/onClosed/send/close |
| `Sources/ChirpProtocol/WsTransportDarwin.swift` | `OkHttpTransport.kt` (Android M2) | Darwin real transport: `URLSessionWebSocketTask` adapter; text frames dropped; onClosed announced exactly once after open succeeds; pre-open failure reports through open's result |
| `Sources/ChirpProtocol/Scheduler.swift` | dart event-loop timers | time seam; tests drive a `ManualScheduler` virtual clock |
| `Sources/ChirpProtocol/Promise.swift` | dart `Future`/Kotlin `CompletableFuture` | settle-once future; combinators `map`/`flatMap`/`handle` are the thenApply/thenCompose/handle mapping |
| `Sources/ChirpProtocol/RequestError.swift` | `lib/protocol/errors.dart` | timeout/closed/kicked (+ server/blocked used by the pipeline) |
| `Tests/ChirpProtocolTests/` | the Kotlin test files | same vector groups — conformance with the Kotlin package (the dart suite was removed with the Flutter app on 2026-09-29; this batch backfilled its four only-there vectors): Frame 6, ChatConnection 16, MsgSpecs table 3, ChatPipeline 27, OfflineSendQueue 9, Hooks 6, WordFilter 7, WordFilterSync 11 (lexicon batch), DeviceRegistrar 7; plus `Tests/ChirpAppCoreTests/` 9 for the app core |

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
cd apps/ios && swift test    # 101 tests, XCTest, Linux-native
```

CI: `ios-app.yml` (macos-latest) runs the package tests on the real
macOS toolchain and builds the generated app project for the iOS
Simulator (see the companion-app section below). Local toolchain:
Swift 6.4 at `~/swift/usr/bin` (swift.org linux tarball;
`PATH` via `~/.bashrc`). The gencode plugin was built from the same
swift-protobuf tag: `git clone --branch 1.38.1 /tmp/swift-protobuf &&
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
| `ChirpCompanion/` | SwiftUI shell: `App/` (entry, root observable), `Views/` (login / sessions / chat screens) |
| `Sources/ChirpAppCore/` | UI-free app logic inside the package (`DeviceIdentity`, `HostConfig`, `LoginDraft`) so `swift test` covers it on Linux **and** macOS |
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
| P2 | 登录+会话+收发最小闭环:`ChatConnection`/`ChatPipeline`/`WsTransportDarwin` 接线;登录(dev 阶段用户名即 token)→ 会话列表 → DM 收发 + `MESSAGE_ACK` + `OfflineSendQueue` + `WordFilterSync` + 连接横幅/KICK;App 层单测(假 transport,对拍协议包手法) | ⬜ |
| P3 | 推送 APNs(对齐 herald 口径:ES256 `.p8` provider token 优先、cert 兜底、topic=bundle id):远程通知注册 → token → `PushTokenSource` 真实现 → `DeviceRegistrar`(platform="ios" + apns_token);凭据缺失走空 token 降级注册(对齐 android nopush);服务端投递链核对 | ⬜ |
| P4 | 游戏内聊天集成 + 功能面对齐 web_companion:多平面(social/party/voice/device/game_presence)、quick_reactions、好友/群/设备/在线设备/组队/语音面板 | ⬜ |

Out of scope until credentials exist: signing/provisioning for physical
devices and the real APNs certificate — the P3 structure keeps both
degradable without them.

