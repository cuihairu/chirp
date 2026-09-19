# Chirp Capability Matrix

Last reviewed: 2026-09-19

This document describes the repository's current implementation status by runtime target, not by roadmap intent.

## Status Legend

- `Supported`: included in the default documented backend path and should be kept buildable
- `Experimental`: implemented partially or behind alternate targets / environment-specific dependencies
- `Demo`: intended mainly for showcase or local exploration; not a reliable backend contract
- `Stub`: placeholder, mock-driven, or visibly incomplete

## Backend Services

| Area | Runtime Target | Status | Notes |
| --- | --- | --- | --- |
| Gateway core login/session routing | `chirp_gateway` | Supported | Main edge entry for TCP/WS login, heartbeat, kick flow; cross-instance kick is device-scoped (same user+device displaces, other devices coexist). With `--chat_host` set, forwards chat business packets (2xxx) to chat over one per-client internal connection (SERVER_AUTH_REQ trust gate + login replay, verbatim relay both ways; a dropped pipe kicks the client so it reattaches) |
| Auth basic token flow | `chirp_auth` | Supported | Default auth binary exists; without MySQL/libsodium it falls back to the simpler token validation path. Enhanced mode validates `LOGIN_REQ` tokens as HS256 JWTs (`--jwt_secret`, `exp` enforced); the scaffold fallback is behind `--allow_scaffold_login` (default off), and the deployment convention aligns that secret with the edge services' `--token_secret` |
| Chat basic messaging | `chirp_chat` | Supported | Private messaging, history, offline queue, group management (create/join/leave/kick/invite/members), read receipts, typing indicators, message reactions, message edit/delete (with moderator support and bulk delete), @mention parsing/autocomplete, and direct-entry rate limiting (per-IP login / per-user send fixed windows, Redis-backed, fail-open) wired into the default binary. The direct client entry (7000/7001) stays open during the edge transition; with `--gateway_service_secret` set, connections that pass the SERVER_AUTH_REQ trust gate (gateway pipes) skip the per-IP login limiter. With `--token_secret` set, login requires an HS256 JWT under the secret aligned with auth's `--jwt_secret` |
| Chat distributed routing | `chirp_chat_distributed` | Experimental | Separate target; not the default documented service binary |
| Chat hybrid Redis + MySQL storage | `chirp_chat` / `chirp_chat_enhanced` | Experimental | With MySQL available, the default `chirp_chat` target builds the enhanced implementation; `chirp_chat_enhanced` is now a compatibility alias |
| Auth registration / refresh / brute-force / rate-limit stack | `chirp_auth` / `chirp_auth_enhanced` | Experimental | With MySQL and libsodium available, the default `chirp_auth` target builds the enhanced implementation; `chirp_auth_enhanced` is now a compatibility alias. `LOGIN_REQ` accepts a valid access token (HS256 JWT) or an active session; scaffold login requires `--allow_scaffold_login 1` |
| Server plane hub (game backend <-> chirp) | `chirp_server_gateway` | Experimental | Service auth (`service_id` + secret), message injection routing toward chat, reliable event delivery (per-service queues, acks, reconnect redelivery); chat consumes injections as an internal peer; upstream injection also available over a Redis Stream (`--broker_redis_host`, consumer group + `XAUTOCLAIM` replay, needs Redis >= 6.2; downlink events still long-connection only) |
| NPC dialog (keyword rule engine) | `chirp_npc_dialog` | Experimental | Pure server-plane client (no player-facing listener): chat turns `npc:`-prefixed private messages into `npc.player_message` events, the service answers with keyword-table replies injected back as `SENDER_NPC` (at-least-once; a hub redelivery can duplicate a reply). Process-level smoke: `./test_services.sh --smoke-npc` |
| Offline-message push trigger | `chirp_chat` (default + distributed builds) | Experimental | Offline private/group messages and server-plane injections enqueue a push through `PushBridge` -> notification (fire-and-forget, logged failures). Wired into the default build and `chirp_chat_distributed` (which also stores offline only when no instance delivered the message, using the router's PUBLISH receiver count); `main_enhanced` (MySQL build) still lacks the wiring pending a build environment that can compile the enhanced branch |
| Social / presence | `services/social` | Experimental | Present as service code, but not validated as a core path |
| Voice signaling / WebRTC integration | `services/voice`, `sdks/core/modules/voice` | Experimental | Signal plane complete (61 unit tests, TSan-clean): targeted SDP offer/answer/candidate relay, LOGIN auth gate (`--token_secret`; scaffold self-report default), coturn REST short-term credentials in join responses, mute/deafen with derived participant state, idle-connection sweep. Environment-heavy and not part of the minimal verified path — no real browser/media E2E yet, so the WebRTC media plane remains unvalidated end to end |
| Party signaling (cross-game team-up) | `services/party` | Experimental | Signal plane complete (46 unit tests, TSan-clean): invite-accept joining (no join codes, idempotent re-invites), ready checks, leader succession on leave/offline, silent last-member disband, kick/transfer-leader with snapshot fan-out (`PARTY_STATE_CHANGED` reaches every member including the actor). Party snapshots write through to Redis (invites stay memory-only, 10-min lazy expiry); disconnecting the last device leaves the party. Player-plane identity decoupled from game-scoped voice rooms; single instance, no cross-instance fan-out; the web companion ships a party UI (snapshot-driven third websocket), Flutter/engine clients land with phase 3 |
| Notification delivery | `services/notification` | Experimental | Protocol face live on TCP 5006 / WS 5016 (6xxx device + push messages, 100% unit coverage); in-process device registry, per-user cooldown and payload builders are real. `--push_transport http` switches on a real HTTP/1.1 provider POST client (`HttpPushTransport`, TCP connection factory behind an injectable seam, deadline/size caps, loopback-tested); the TLS handshake and APNs HTTP/2 remain out until the build carries OpenSSL/nghttp2, and the default stays the logging transport |
| App gateway edge (companion apps) | `chirp_app_gateway` | Experimental | TCP 5200 / WS 5201, plus optional TLS/wss listeners (`--tls_port`/`--ws_tls_port`, default off; `--tls_cert`/`--tls_key` required when either is on, TLS 1.2+): login/heartbeat/session binding like the game gateway (device-scoped cross-instance kick) plus 6xxx device-message forwarding to notification (requires an authenticated session, `user_id` pinned server-side); TLS sessions flow through the same registry/auth paths as plaintext; chat business packets are not accepted. The player-aggregation target model (player identity linked to N games, cross-game subscriptions / voice / chat fan-in) is documented in architecture.md but not implemented |
| Search service | `services/search` | Experimental | Present in tree, not established as a verified path |

## SDKs and Apps

| Area | Target | Status | Notes |
| --- | --- | --- | --- |
| C++ Core SDK | `sdks/core` | Experimental | Game-client integration base (`chirp::sdk::ChatClient`); process-level smoke covers login / bidirectional online delivery / offline-queue refill against a real `chirp_chat` (`./test_services.sh --smoke-sdk`). Still avoid calling it fully stable |
| Unity SDK | `sdks/unity` | Experimental | Contains TODO-backed social bindings |
| Unreal SDK | `sdks/unreal` | Experimental | Contains unimplemented return paths |
| Mobile companion app | `apps/mobile_companion` | Experimental | Flutter app on a pure-Dart protocol stack (the old FFI bridge is deleted): generated protos in `proto/dart` (package `chirp_proto`), a ported `ChirpClient` (length-prefixed Packet framing, sequence-correlated request/response, 25s heartbeats, exponential-backoff auto reconnect, terminal KICK handling), store + api layers mirroring the web companion. Login with cross-device-kick banner, four-tab home (chats/friends/party/profile), private + group chat with history paging, read receipts, typing, reactions, edit/delete, optimistic sends with offline queuing, local notifications on background messages. Four degradeable websockets exactly like the web app (chat 7001 / social 8001 / party 7501 / app_gateway 5201). 40-test suite (protocol/state/api/widget); android build, analyze and tests in CI (`mobile-build.yml`). Still the transitional direct-connect topology, moving to the app_gateway aggregation edge together with web |
| Web companion app | `apps/web_companion` | Experimental | Vite + React + TS strict + MUI; real backend over four degradeable websockets (chat 7001 / social 8001 / party 7501 / app_gateway 5201): login with cross-device kick, private chat (history/read receipts/typing/reactions/edit-delete), full group management, server-authoritative friends + presence, a snapshot-driven party UI (create/invite/ready/kick/transfer/leave/disband), device management (auto-registers the browser as a push target through the auth-gated app_gateway forward path, list/unregister UI), and desktop notifications (Notification API over the live chat stream; real Web-Push awaits the backend transport). Direct-connect transitional topology until the app_gateway aggregation edge; unit + component suite (226 tests) with a real-backend smoke script |
| Admin dashboard | `apps/admin_dashboard` | Stub | Uses mock data and demo pages rather than real backend integration |
| CLI client / benchmark tools | `apps/cli_client`, `tools/benchmark` | Demo | Good for smoke testing and manual validation |

## Test and Delivery Confidence

| Concern | Current State | Status |
| --- | --- | --- |
| Unit tests | 32 suites in `tests/unit`; every backend package that is linked into a test binary is at 100% line coverage per `scripts/run_coverage.sh` (documented `KNOWN_UNCOVERABLE` exclusions only). `app_gateway`, `voice` and `party` have suites (`app_gateway_tests`, `voice_tests`, `party_tests`) but their `main.cc` files sit outside the coverage measurement | Supported |
| Standard local build runs tests via `ctest` | `ctest --preset dev` (and `--preset coverage` with the gcov build); a fresh tree builds and passes | Supported |
| CI treats test failure as hard failure | `ci.yml` runs Debug + Release builds with `ctest`, plus a coverage job that fails when any package drops below 98% line coverage | Supported |
| Process-level smoke coverage | `test_services.sh --smoke / --smoke-chat / --smoke-sdk / --smoke-npc / --smoke-edge / --smoke-jwt / --smoke-redis` all run in the CI smoke job (`--smoke-redis` uses docker redis for the cross-instance kick) | Supported |
| Docker Compose path for core services | Present | Supported |
| Roadmap matches default build outputs | Yes — TODO.md is the live roadmap (rewritten 2026-09); completed items are struck through in README.md | Supported |

## Recommended Public Positioning

For external readers, the repository should currently present itself as:

- a supported core backend skeleton for `gateway + auth + chat`
- an experimental playground for distributed chat, richer auth, voice, social, and multi-engine SDK work
- a demo repository for mobile/admin surfaces rather than a finished product suite
