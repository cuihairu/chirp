# chirp Android — native protocol core (M1) + connection layer (M2) + app shell (M3)

Kotlin/JVM port of the wire protocol shared with `apps/mobile_companion`
(Dart/Flutter), plus a minimal Android app shell. Three batches so far, all
unit-tested on the JVM.

## What ships here

### Protocol core (`src/main/kotlin/` — SDK-free, compiled by both gates)

| File | Ported from | Role |
|---|---|---|
| `protocol/Frame.kt` | `lib/protocol/frame.dart` | u32-BE length-prefix framing, 16 MiB cap, stream decoder |
| `protocol/MessageSpec.kt` | `lib/protocol/msg_map.dart` | 40 req/resp MsgID↔type specs with protobuf decoders |
| `protocol/ChatConnection.kt` | `lib/protocol/chirp_client.dart` | full state machine (`waitingReconnect` included), sequence correlation, typed request futures with deadlines, missed-pong heartbeat detection, auto reconnect with jittered exponential backoff, KICK terminal semantics |
| `protocol/RequestError.kt` | `lib/protocol/errors.dart` | request failure kinds (timeout/closed/kicked; server/blocked are api-layer) |
| `protocol/WsTransport.kt` + `protocol/OkHttpTransport.kt` | `lib/protocol/ws_transport.dart` | transport seam + the OkHttp WebSocket adapter (OkHttp instead of `java.net.http`: it exists on every Android API level) |
| `protocol/Scheduler.kt` | dart event-loop timers | time seam; tests drive a manual virtual clock |
| `protocol/WordFilter.kt` | `lib/protocol/word_filter.dart` | send-side sensitive-word pre-check: server-format lexicon parsing, ASCII-only case folding (UTF-8 safe), mask-interval replace with run collapsing, REPLACE/REJECT policy |

Dart's single event loop becomes a lock: all state transitions hold one
monitor (transport callbacks arrive on OkHttp threads, timers on the
scheduler thread). `Scheduler` and `Random` are injectable, so every
backoff/heartbeat/deadline test is deterministic.

### App shell (`src/app/kotlin/` + manifest + res — Gradle-only, needs android.jar)

| File | Ported from | Role |
|---|---|---|
| `MainActivity.kt` | `lib/api/chat_api.dart` login/send + notify ack | dev shell: login (dev token = user id, persisted UUID deviceId, `supports_message_ack=true`), DM a peer (sorted-pair channelId like `home_screen.dart`), live CHAT_MESSAGE_NOTIFY render + mandatory MESSAGE_ACK, word-filter on send |
| `AndroidManifest.xml` + `res/` | — | framework-Views UI only (no androidx/Compose — minimal version-coupling surface); cleartext `ws://` enabled for dev gateways |

Host is fixed to `ws://10.0.2.2:7001` (emulator host-loopback alias, same as
the dart dev default); configurable host comes with real-device work.
The client-side lexicon is empty for now (the server enforces its own
filter; this is the REPLACE-semantics send-side seam, lexicon delivery in M4).

## Gate

Two legs, both must stay green:

```sh
make test    # leg 1 (SDK-free): JDK 21 + kotlinc; compiles src/main/kotlin +
             #   src/test/kotlin against pinned jars, runs the 35 tests
make clean

./gradlew assembleDebug        # leg 2: AGP 9.4.1 app shell → debug APK
./gradlew testDebugUnitTest    #   the same 35 tests through Gradle
```

Leg 1 needs no Android SDK. Leg 2 needs `ANDROID_HOME`/`local.properties`
(pointing at an SDK with platform 36 + build-tools 36.0.0) and fetches AGP
from Google Maven. CI currently runs **neither** leg (no android job in
ci.yml) — the server build-and-test leg only proves `gen_proto.sh`'s java
block. Wiring an android gate job is future work.

The suite includes real-socket OkHttp transport tests over MockWebServer
(WS upgrade, binary echo, peer close, failed upgrade). MockWebServer quirk
recorded in the tests: its side never advances the close handshake past
`onClosing`, so the transport announces "down" there — exactly-once guarded.

### Make-leg dependencies

Not vendored (same convention as the Unity csharp runtime): protobuf-java,
okhttp/okio, mockwebserver (+ its junit4 supertype), and the JUnit console
launcher are fetched from Maven Central into the gitignored `.cache/` with
pinned sha256 checksums. The protobuf-java version (4.33.4) must match the
protoc generation that produced `proto/java` (libprotoc 33.4) — the
generated code carries that guard.

### Gradle-leg notes (AGP 9 realities, discovered the hard way)

- **AGP 9 has built-in Kotlin support** — applying
  `org.jetbrains.kotlin.android` is rejected outright ("no longer required
  since AGP 9.0"). `.kt` sources in android sourceSets just compile; the
  bundled compiler is Kotlin 2.2.0 (reads metadata ≤ 2.3.0), so the test
  stack pins kotlin-test/kotlin-test-junit5 **2.2.20** — 2.4.x stdlib
  breaks the test compile with a metadata version error.
- `proto/java/` rides in the main sourceSet (`java.srcDirs`), compiled by
  javac ahead of Kotlin in the same task graph; protobuf-java 4.33.4 is the
  implementation dependency (same pin as the make leg).
- `gradle-wrapper.properties` pins the canonical bin dist. On a loaded box
  where `services.gradle.org` crawls, the wrapper cache
  (`~/.gradle/wrapper/dists/gradle-9.8.0-bin/<hash>/`) can be pre-seeded
  from the sdkman install — committed wrapper stays canonical.

Generated protobuf Java lives in the repo-root `proto/java/` (committed
gencode; regenerated by `./gen_proto.sh` like ts/dart/csharp — no
`java_package` options, so classes sit in the default proto packages as
nested classes of the outer files).

## Roadmap (staged native migration)

- **M4 — chat pipeline**: port `chat_pipeline.dart` (388 lines: command
  routing, interceptor rewrite/drop, local archive, lifecycle) + the offline
  queue, word-lexicon delivery into the shell's filter seam, grow real
  navigation beyond the single-activity dev shell.
- **Push (M3.5)**: the server plane already exists —
  `services/app/notification` registers devices with
  `fcm_token`/`apns_token` (plus Push Kit order) and fans out
  `PushNotificationRequest` (badge/click_action fields for iOS/Android
  deep links). Android side = firebase-messaging SDK + google-services.json
  (needs Firebase project credentials). Registering the WS device plane
  (`registerDevice`, app_gateway 5201) is already possible from the shell.
- **CI gate job**: none of the two legs runs in CI yet (see Gate).
- **iOS**: Swift port of the same files (URLSessionWebSocketTask
  transport). Blocked on a toolchain decision — no Xcode/swift locally and
  CI runners are ubuntu-only.
- **HarmonyOS**: ArkTS port under DevEco/hvigor. No local hvigor toolchain;
  will ship with an honest no-local-gate note unless a CI slot appears.
- The Flutter app (`apps/mobile_companion`) is frozen until the native
  clients reach protocol parity, then removed.
