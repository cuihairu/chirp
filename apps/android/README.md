# chirp Android — native protocol core (M1) + connection layer (M2) + app shell (M3) + chat pipeline (M4) + push seam (M3.5)

Kotlin/JVM port of the wire protocol shared with `apps/mobile_companion`
(Dart/Flutter), plus a minimal Android app shell. Four batches so far, all
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
| `protocol/WordFilter.kt` | `lib/protocol/word_filter.dart` | send-side sensitive-word pre-check: server-format lexicon parsing, ASCII-only case folding (UTF-8 safe), mask-interval replace with run collapsing, REPLACE/REJECT policy. Algorithm spec shared by all four implementations (C++ server / dart / C# / Kotlin): `docs/design-notes/word_filter.md` |
| `protocol/Hooks.kt` | `lib/protocol/hooks.dart` + `lib/state/message_store.dart` | the five seam types (`MessageInterceptor` rewrite/drop, `AuthProvider` token sourcing + one renewal, `MessageStore` archive + in-memory impl, `ChatEventListener` fan-out, `CommandHandler`), plus `SendOptions` |
| `protocol/ChatPipeline.kt` | `lib/protocol/chat_pipeline.dart` (388 lines) | the api layer over `ChatConnection`: login (token sourcing explicit > provider > userId, one AUTH_FAILED renewal round, terminal `onLoginResult`/`onAuthResult` fan-out), send (CLOSED-first validation, `/command` routing with throwing-handler fallback, interceptor rewrite→wire / drop→BLOCKED, archive both directions), incoming path (notify parse → interceptor → archive → listeners, malformed bodies dropped without touching the link), lifecycle (start/stop with throwing-listener isolation) |
| `protocol/OfflineSendQueue.kt` | new (dart side has UI-level pending flags only) | client outbox replayed on reconnect: at-least-once (TIMEOUT keeps the entry), CLOSED stops the flush with the tail kept in order, BLOCKED/argument errors drop, any server response (incl. TARGET_OFFLINE) confirms; clientId dedupe, drop-oldest eviction at cap |
| `protocol/WordFilterLoader.kt` | — | reads a server-format lexicon file into a `WordFilter` (no protocol channel for lexicon delivery exists — see below) |

Dart's single event loop becomes a lock: all state transitions hold one
monitor (transport callbacks arrive on OkHttp threads, timers on the
scheduler thread). `Scheduler` and `Random` are injectable, so every
backoff/heartbeat/deadline test is deterministic.

### App shell (`src/app/kotlin/` + manifest + res — Gradle-only, needs android.jar)

| File | Ported from | Role |
|---|---|---|
| `MainActivity.kt` | `lib/api/chat_api.dart` login/send + notify ack | dev shell, M4-rewired onto `ChatPipeline`: login via `pipeline.login` (provider-less dev token = user id), DM a peer through `pipeline.send` (sorted-pair channelId), interceptor-installed send-side word filter, `MemoryMessageStore` archive, mandatory MESSAGE_ACK stays at the shell layer (pipeline doesn't ack, matching dart layering), `OfflineSendQueue` flushed from `onReconnected`, device registration (`DeviceRegistrar` + `PlatformPushTokenSource`) on login success |
| `src/push` / `src/nopush` (`chirp.mobile.push.PlatformPushTokenSource`) | — | same-FQCN token-source pair picked by the `-PchirpPush` switch: the Firebase one (25.1.3, google-services plugin 4.5.0) fetches the FCM token with a 10s bound, the no-op one always reports `null` |
| `AndroidManifest.xml` + `res/` | — | framework-Views UI only (no androidx/Compose — minimal version-coupling surface); cleartext `ws://` enabled for dev gateways |

Host is fixed to `ws://10.0.2.2:7001` (emulator host-loopback alias, same as
the dart dev default); configurable host comes with real-device work.
Lexicon loading: **no protocol channel for lexicon delivery exists** (the
server reads its lexicon from `--word_filter_file` argv; see
`docs/design-notes/word_filter.md`), so the shell reads
`filesDir/word_filter.txt` in server format (one term per line, `#`
comments) when present — an empty lexicon passes everything through and the
server still enforces its own filter.

## Push (M3.5 — built switch, no real credentials)

`DeviceRegistrar` (protocol core, JVM-tested) ports dart
`device_api.dart#registerSelf`: after login it registers the install on the
device plane (app_gateway WS 5201) — the M2 spec set already carried
`registerDevice`; only the caller was missing. The FCM token flows through
the `PushTokenSource` seam:

- **Default build** (no flag): `src/nopush` no-op source → empty
  `fcm_token`, registration still happens (dart's degrade path — the
  device lists, pushes degrade to the server's logging transport). No
  Firebase class on any classpath.
- **`./gradlew assembleDebug -PchirpPush=true`**: applies the
  google-services plugin, adds firebase-messaging 25.1.3, swaps in the
  Firebase source (`src/push`). FCM token fetch is bounded at 10s; any
  failure (Play services missing, bad credentials) reports `null` and
  degrades to the same empty-token path.

**Assumption, explicit**: there are no Firebase credentials for this
project. `google-services.json` in this directory is a committed
PLACEHOLDER (fabricated ids, documented in its `_note` field) that the
plugin accepts at build time while runtime token fetches fail — which is
exactly the degrade path above. Real enablement = replace that one file
(package_name must stay `chirp.mobile`); zero code changes. The switch
stays off in the default gate so the build stays deterministic.

## Gate

Two legs, both must stay green:

```sh
make test    # leg 1 (SDK-free): JDK 21 + kotlinc; compiles src/main/kotlin +
             #   src/test/kotlin against pinned jars, runs the 80 tests
make clean

./gradlew assembleDebug        # leg 2: AGP 9.4.1 app shell → debug APK
./gradlew testDebugUnitTest    #   the same 80 tests through Gradle
#   (+ the push variant once: ./gradlew clean assembleDebug testDebugUnitTest
#    -PchirpPush=true — fetches Firebase, exercises the placeholder
#    google-services.json through the plugin; verified green 2026-09-28)
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

- ~~M4 — chat pipeline~~ **done**: `ChatPipeline.kt` (command routing,
  interceptor rewrite/drop, local archive, lifecycle), `OfflineSendQueue.kt`,
  local-file lexicon into the shell's filter seam. Still open: real
  navigation beyond the single-activity dev shell (M4 shipped the pipeline
  without new screens).
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
