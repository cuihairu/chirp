> 🌐 Language: [中文](../zh/design-notes/sdk_compatibility.md)) | **English** (this page)

# SDK Engine Compatibility

Last updated: 2026-09-21

## Overview

| Engine | Language | SDK directory | Minimum version | Priority | Status |
|---|---|---|---|---|---|
| Unity | C# | `sdks/unity/` | 2021.2 LTS | P0 | Foundation exists |
| Unreal Engine 5 | C++ | `sdks/unreal/` | 5.1 | P0 | Foundation exists |
| LayaBox | TypeScript | `sdks/laya/` (planned, directory not created) | LayaAir 3.0 | P1 | To develop |
| Godot | GDScript / C# | `sdks/godot/` (planned, directory not created) | 4.0 | P2 | To develop |
| Cocos Creator | TypeScript | `sdks/cocos/` (planned, directory not created) | 3.8 | P2 | To develop |
| Web | TypeScript | `apps/web_companion/` | — | P0 | Foundation exists |
| Android | Kotlin | `apps/android/` | minSdk 26 | P1 | Native protocol core delivered |
| iOS | Swift | `apps/ios/` | Swift 5.9+ | P1 | Native protocol core delivered; `swift test` 232 cases (ChirpProtocolTests 93 + ChirpAppCoreTests 139) |
| C++ desktop | C++ | `sdks/core/` | C++17 | P0 | Foundation exists |
| Go server | Go | `sdks/go/` | 1.21 | P0 | Foundation exists |

## Detailed requirements per engine

### Unity (P0 priority)

| Item | Requirement |
|---|---|
| Minimum version | Unity 2021.2 LTS (C# 9 / netstandard2.1) |
| Recommended version | Unity 2022.3 LTS or Unity 6000.x |
| .NET compatibility | .NET Standard 2.1 |
| Protobuf | Google.Protobuf 3.27.x (Unity release package) |
| Transport | ClientWebSocket (built-in) |
| Threading model | ChirpManager MonoBehaviour main-thread dispatch |
| Package format | Unity Package (UPM) or direct Assets/ copy |

Already present:
- `ChirpClient` protocol core (pure C#, no UnityEngine dependency)
- `ChirpManager` MonoBehaviour thin shell
- `dotnet/` pure .NET test project
- `proto/csharp/` generated code

To complete:
- [x] Hook interfaces (the five-piece MessageInterceptor/AuthProvider/MessageStore/ChatEventListener/CommandHandler set) - both C++ core (2026-09) and .NET/Unity (2026-09, `ChirpHooks.cs`, interface + default methods) are aligned
- [x] Local history message storage - the .NET/Unity side is `FileMessageStore` (2026-09, zero-dependency file persistence: append-only log + startup replay + read cursor, `Compact()` atomic rewrite; not bound to SQLite - if a project needs it, it can wire in a third-party SQLite implementation behind the same `IMessageStore`); the C++ core has the same `file_message_store.h` (2026-09-24, header-only, **the same file format** `CHIRPLOG1` as the C# version, so archives are interchangeably readable; TS uses an in-memory implementation - Web/mobile persistence is the host storage layer's responsibility)
- [x] Sensitive-word filter client precheck - the same `WordFilterInterceptor` in three languages (2026-09: C++ `word_filter.h`, C# `WordFilterInterceptor.cs`, TS protocol layer `word_filter`; the Dart/Flutter SDK was removed on 2026-09-29): lexicon format, ASCII case-insensitive substring matching, and mask-then-rebuild replacement semantics are all aligned with the server `chirp::chat::WordFilter`, and clients and the server can share the same lexicon file; Replace (rewrite, consecutive hits collapse) / Reject (blocked) tiers, no Record (auditing is the server's job); filters the send side only
- [x] Unity Package release configuration - UPM layout ready (2026-09: `package.json` `com.chirp.unity@0.1.0` + the `Chirp.Sdk`/`Chirp.Manager` asmdefs, with `Chirp/`'s zero-engine-dependency property fixed at compile time by `noEngineReferences`); supports manifest `file:` references or local tarball import; registry publishing is not done per the "no releases" red line - to be handled per project need once a game project integrates

### Unreal Engine 5 (P0 priority)

| Item | Requirement |
|---|---|
| Minimum version | UE 5.1 |
| Recommended version | UE 5.4+ |
| C++ standard | C++20 (UE5 default) |
| Transport | Native TCP (chirp core SDK) |
| Threading model | AsyncTask(ENamedThreads::GameThread) dispatch |
| Package format | UE Plugin (.uplugin) |

Already present:
- `UChirpClientSubsystem` (GameInstance subsystem)
- Blueprint events (login result, chat message, kick, disconnect, reconnecting/reconnected, send result, full envelope)
- Connection status / channel type enums, `FChirpChatEnvelope`/`FChirpSendOptions` structs
- `SendChatMessageEx` (command routing/interceptor/store full pipeline + reply quoting), `LoadHistory`/`GetUnreadCount`/`MarkRead`/`CleanupMessages` BP forwarding
- Native core build script

To complete:
- [x] Hook interfaces (C++ virtual base classes passed straight through to core via `NativeClient()` + Blueprint-bindable events; the built-in listener already bridges OnReconnecting/OnReconnected/OnChatEnvelope, 2026-09)
- [ ] UMG chat UI components (optional)
- [x] Blueprint-renderable data structures for item links / achievement shares (`FChirpChatEnvelope`: MsgType/Metadata/ReplyToMessageId all fields BP-readable, 2026-09)
- [ ] UE 5.4+ verification

### LayaBox (P1 priority)

| Item | Requirement |
|---|---|
| Minimum version | LayaAir 3.0 |
| Recommended version | latest LayaAir 3.x |
| Language | TypeScript |
| Transport | WebSocket |
| Runtime | Browser / WeChat mini-game / APP |
| Protobuf | protobuf.js (runtime) or ts-proto (compile time) |

To develop:
- [x] TypeScript protocol core (2026-09-24: provided by the standalone `@chirp/protocol` package - ChirpClient + frame codec + message map, with its own unit tests and a 90% coverage gate)
- [x] WebSocket transport adapter (`wsFactory` injected at ChirpClient construction; the browser's native WebSocket is the default implementation)
- [x] WeChat mini-game platform adapter (2026-09-24: `sdks/ts/src/adapters/wx_socket.ts` bridges `wx.connectSocket`'s SocketTask to `WebSocketLike`, callback registration -> handler properties, ArrayBuffer passthrough in both directions; with fake-socket unit tests and a real-ChirpClient login round-trip test)
- [x] Hook interfaces (the five hooks in `hooks.ts` + ChatPipeline, semantics aligned with C++/C#)
- [ ] Sample project (needs a real LayaAir project)

### Godot (P2 priority)

| Item | Requirement |
|---|---|
| Minimum version | Godot 4.0 |
| Recommended version | Godot 4.3+ |
| Language | GDScript or C# |
| Transport | StreamPeerTCP / WebSocketPeer |
| Protobuf | GDScript protobuf or C# protobuf |

To develop:
- [x] GDScript protocol core (or C# reusing the Unity SDK's pure C# part) - 2026-09-24: took the C# reuse route. `sdks/unity/Runtime/Chirp/` has zero engine dependency (fixed by the `noEngineReferences` asmdef), the transport `ChirpTransport.cs` uses the `System.Net.WebSockets` standard library, so Godot 4 .NET reuses it directly with no adaptation; integration guide at `docs/sdk/godot.md`. No GDScript route (no mature protobuf ecosystem)
- [ ] Godot node wrapper (a ChatClient node; needs verification in a real Godot project)
- [x] Hook interfaces (C# form: reuse the five `ChirpHooks.cs` interfaces directly, listener wiring example in `docs/sdk/godot.md`; no GDScript signal route - same as above, GDScript has no mature protobuf ecosystem, Godot takes the C# reuse route)
- [ ] Sample project

### Cocos Creator (P2 priority)

| Item | Requirement |
|---|---|
| Minimum version | Cocos Creator 3.8 |
| Recommended version | 3.8.x LTS |
| Language | TypeScript |
| Transport | WebSocket (native / mini-game adapter layer) |
| Runtime | Web / iOS / Android / WeChat mini-game |
| Protobuf | protobuf.js or ts-proto |

To develop:
- [x] TypeScript protocol core (shares `@chirp/protocol` with LayaAir, 2026-09-24)
- [x] WeChat mini-game platform adapter (shares `@chirp/protocol`'s `adapters/wx_socket.ts`)
- [ ] Cocos component wrapper (needs a Cocos Creator project)
- [x] Hook interfaces (shares the five hooks in `hooks.ts`)

### Web (P0, foundation exists)

| Item | Requirement |
|---|---|
| Runtime | Modern browsers (Chrome 90+, Firefox 90+, Safari 15+, Edge 90+) |
| Language | TypeScript (strict) |
| Transport | WebSocket |
| Framework | React 18 (web_companion sample) |
| Build | Vite |

Already present:
- `ChirpClient` protocol core (TypeScript)
- WebSocket transport
- React component samples
- Hook interfaces and `ChatPipeline` (`sdks/ts/src/hooks.ts` + `chat_pipeline.ts`, pure TypeScript with zero React dependency; the five hooks MessageInterceptor / AuthProvider / MessageStore / ChatEventListener / CommandHandler, semantics aligned with the C++ core and C#: '/' commands pass through with zero registration, an interceptor returning false or throwing = intercepted, AUTH_FAILED renews at most once)

To complete:
- [ ] Standalone npm package publishing
- [x] Framework-agnostic core package split out (2026-09-24: `sdks/ts` = the `@chirp/protocol` workspace package with its own tsconfig/vitest and a 90% coverage gate; web_companion references it via `@chirp/protocol/*`, red line unchanged - registry publishing still not done)

### Mobile native (Android/iOS, replacing Flutter since 2026-09-29)

The Flutter app (`apps/mobile_companion`) was removed with the native migration: the protocol cores were ported to native packages along the Android M1->M4+M3.5 and iOS phased paths, with the test vectors reused for three-way cross-checks (the Flutter-removal batch filled in four vectors - custom msgType / state-flip fan-out / reconnect-event fan-out / no-store degradation - which the native packages now solely carry).

| Item | Android | iOS |
|---|---|---|
| Path | `apps/android/` | `apps/ios/` |
| Language | Kotlin (AGP 9 built-in compiler) | Swift (SwiftPM, `swift test` gate on Linux) |
| Transport | OkHttp WebSocket | URLSessionWebSocketTask (Darwin) / scripted fake (Linux gate) |
| Protocol core | Frame/MessageSpec/ChatConnection/WordFilter/ChatPipeline/OfflineSendQueue/DeviceRegistrar/PushRoute | Same (same vector set) |
| Gate | `make test` 110 cases + `gradlew testDebugUnitTest` | `swift test` 232 cases (ChirpProtocolTests 93 + ChirpAppCoreTests 139) |

Already present:
- The five-hook interfaces (MessageInterceptor/AuthProvider/MessageStore/ChatEventListener/CommandHandler)
  + the `ChatPipeline` pipeline, semantics aligned with the C++ core, C# and Web ('/' commands pass through with zero registration,
  interceptor rewrite/intercept, AUTH_FAILED renews at most once, the onReconnecting/onReconnected
  event surface, KICK reason passed through to the listener; SendOptions with the same optional fields as TS)
- The FCM push seam (Android M3.5: build switch + placeholder credentials, empty-token degraded registration) /
  the APNs token seam (iOS M3.5 core: real credentials pending, degraded path tested)

To complete:
- [x] Android receiving-side notification rendering / click deep link (2026-10-11: the `PushRoute.kt` protocol core - `PushRouteParser`/`PushForegroundPolicy` ported with the same semantics as iOS P5, 8 tests cross-checked against the same Swift vector set; the shell `ChirpNotifications` (chirp_messages channel, stacked per conversation, click PendingIntent carrying the conversation key) + `MainActivity` (singleTask+onNewIntent deep link into DMs, POST_NOTIFICATIONS runtime permission on API 33+, onResume/onPause maintaining ActiveChannel) + `ChirpFirebaseService` (compiled only in the -PchirpPush build, data reverse-parsed into a route, banner suppressed when the conversation is being viewed); end-to-end real delivery still needs real Firebase credentials)
- [x] Android token refresh re-registration (2026-10-11: the `PushTokenHub.kt` protocol core - process-level latest token + listener fan-out, empty/same-token idempotent, throwing listeners isolated, 6 tests; the push leg's `onNewToken` records into the hub, and the shell mid-login re-registers over the existing connection (the server's RegisterDevice does create-or-update by device_id, so the same key overwrites the token); when not logged in only latest is recorded, and the next login registers the newest token as always)
- [ ] iOS shell UI, real APNs credentials
- [ ] HarmonyOS (ArkTS, toolchain undecided)

### C++ desktop (P0, foundation exists)

| Item | Requirement |
|---|---|
| C++ standard | C++17 (minimum), C++20 (recommended) |
| Transport | TCP (ASIO) |
| Dependencies | ASIO (standalone), Protobuf |
| Platforms | Linux, Windows, macOS |

Already present:
- The complete `ChatClient` implementation
- The 5 hook interfaces (MessageInterceptor/AuthProvider/MessageStore/ChatEventListener/CommandHandler) wired (2026-09)
- 27 convenience APIs (send extensions/server history/read & unread/blocklist/mute/typing/edit-delete/reaction receipts/bulk delete/@mentions/the full group set, 2026-09)
- Unit tests in `tests/unit/sdk_core_test.cc` (state machine/loopback/hook wiring/convenience APIs/FileMessageStore round trips)
- The `sdk_example` sample

### Go server (P0, foundation exists)

| Item | Requirement |
|---|---|
| Go version | 1.21+ |
| Transport | TCP (outbound to game_server_gateway) |
| Dependencies | google.golang.org/protobuf |

Already present:
- A complete server plane client
- inject / event / identity / subscription / unread RPCs
- 33 race-enabled unit tests (`client_test.go` 14 + `client_edge_test.go` 19)

To complete:
- [x] Align with the new proto package name (game_server_gateway) - verified (2026-09): `sdks/go/client.go` imports `proto/go/game_server_gateway`, there is no orphan `server_gateway` directory under `proto/go/`, and `go build ./...` plus `go test ./sdks/go/` all pass

## Unified hook interface design

All SDKs implement the same hook interfaces (in each language's idiomatic way):

| Hook | C++ | C# | TypeScript | GDScript |
|---|---|---|---|---|
| Message interception | `MessageInterceptor` (virtual base class) | `IMessageInterceptor` (interface) | `MessageInterceptor` (interface) | signal + callback |
| Auth provision | `AuthProvider` (virtual base class) | `IAuthProvider` (interface) | `AuthProvider` (interface) | callback |
| Command handling | `CommandHandler` (virtual base class) | `ICommandHandler` (interface) | `CommandHandler` (interface) | signal |
| Message storage | `MessageStore` (virtual base class) | `IMessageStore` (interface) | `MessageStore` (interface) | Resource |
| Event listening | `ChatEventListener` (virtual base class) | `IChatEventListener` (interface) | `ChatEventListener` (interface) | signal |
| Message rendering | `MessageRenderer` (virtual base class) | `IMessageRenderer` (interface) | `MessageRenderer` (interface) | signal |

The interface semantics are identical; only naming and call style follow each language's conventions. See the C++ header definitions under `sdks/core/include/chirp/` for details.

## Implementation priorities

### First batch (P0, in step with the game plane)

1. **C++ core SDK**: hook interfaces + command system (the reference implementation for the other SDKs)
2. **Unity SDK**: hook interfaces + history storage
3. **Unreal SDK**: hook interfaces + Blueprint events

### Second batch (P1, after the App plane starts)

4. **TypeScript core package**: independent of web_companion, shared by LayaBox/Cocos/Web
5. **LayaBox SDK**: TypeScript core + Laya adapter layer
6. **Mobile native packages**: `apps/android` + `apps/ios` (delivered 2026-09-29; the Flutter app was removed in the same batch)

### Third batch (P2, on demand)

7. **Godot SDK**: GDScript or C# reuse
8. **Cocos Creator SDK**: TypeScript core + Cocos adapter layer
