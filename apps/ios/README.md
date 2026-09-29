# chirp iOS — native protocol core (Swift, batch 1)

SwiftPM package porting the pure protocol core of `apps/mobile_companion`
(Dart/Flutter), same migration path the Android package took (its M1+M2
batch, minus the app shell). Gate: `swift test` on Linux — **25 tests, all
green** (Swift 6.4, x86_64 linux).

## Layout

| Path | Ported from | Role |
|---|---|---|
| `Sources/ChirpProtos/proto/*.pb.swift` | `proto/*.proto` | committed gencode, `protoc-gen-swift` 1.38.1 with `Visibility=Public` (cross-target access; the runtime dep is pinned to the same 1.38.1). Regenerate: `protoc --plugin=<path>/protoc-gen-swift --swift_out=apps/ios/Sources/ChirpProtos --swift_opt=Visibility=Public -I . proto/*.proto` from the repo root |
| `Sources/ChirpProtocol/Frame.swift` | `lib/protocol/frame.dart` | u32-BE length-prefix framing, 16 MiB cap, stream decoder, byte-array in/out |
| `Sources/ChirpProtocol/MessageSpec.swift` | `lib/protocol/msg_map.dart` | all 40 req/resp MsgID↔type pairs with protobuf decoders + the type-erased `all` table |
| `Sources/ChirpProtocol/ChatConnection.swift` | `lib/protocol/chirp_client.dart` | full state machine (`idle/connecting/connected/waitingReconnect/kicked/closed`), sequence correlation, typed request futures (`Promise`), request deadlines, missed-pong heartbeat (`maxMissedPongs=2`), auto reconnect with jittered exponential backoff, KICK terminal semantics, clock offset |
| `Sources/ChirpProtocol/WsTransport.swift` | `lib/protocol/ws_transport.dart` | transport seam: open/onBinary/onClosed/send/close |
| `Sources/ChirpProtocol/Scheduler.swift` | dart event-loop timers | time seam; tests drive a `ManualScheduler` virtual clock |
| `Sources/ChirpProtocol/Promise.swift` | dart `Future`/Kotlin `CompletableFuture` | settle-once future with completion callbacks and a blocking test `get` |
| `Sources/ChirpProtocol/RequestError.swift` | `lib/protocol/errors.dart` | timeout/closed/kicked (+ server/blocked reserved for the api layer) |
| `Tests/ChirpProtocolTests/` | the Kotlin test files | same vector groups: Frame 6, ChatConnection 16, MsgSpecs table 3 — three-platform conformance (dart ↔ Kotlin ↔ Swift) |

Dart's single event loop becomes one recursive lock (transport callbacks and
scheduler ticks arrive on foreign threads; `close()` re-enters through the
synchronous down event, so a plain `NSLock` would self-deadlock). `Scheduler`
and `RandomSource` are injectable — every backoff/heartbeat/deadline test is
deterministic on the virtual clock.

## Gate

```sh
cd apps/ios && swift test    # 25 tests, XCTest, Linux-native
```

No CI leg yet (same as the Android gates — local-only for now). Local
toolchain: Swift 6.4 at `~/swift/usr/bin` (swift.org linux tarball;
`PATH` via `~/.bashrc`). The gencode plugin was built from the same
swift-protobuf tag: `git clone --branch 1.38.1 /tmp/swift-protobuf &&
swift build -c release --product protoc-gen-swift`.

## Assumptions (noted per non-interactive rules)

- **Transport is a seam only** — `URLSessionWebSocketTask` is Darwin-only
  and does not exist in swift-corelibs-foundation, so the real adapter is a
  TODO on the Darwin side (see `WsTransport.swift`); the Linux gate drives
  the state machine through the scripted fake, which is the same vector set
  the Android suite runs against its OkHttp/MockWebServer fakes.
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

## Not in this batch (per TODO)

- Word filter (Android M3 equivalent), chat pipeline / offline queue
  (Android M4 equivalent), real transport, app shell/UI, APNs push
  (TODO L163, needs Apple credentials) — staged later batches on the same
  M1→M4 path Android walked.
