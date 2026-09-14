# Chirp Capability Matrix

Last reviewed: 2026-09-14

This document describes the repository's current implementation status by runtime target, not by roadmap intent.

## Status Legend

- `Supported`: included in the default documented backend path and should be kept buildable
- `Experimental`: implemented partially or behind alternate targets / environment-specific dependencies
- `Demo`: intended mainly for showcase or local exploration; not a reliable backend contract
- `Stub`: placeholder, mock-driven, or visibly incomplete

## Backend Services

| Area | Runtime Target | Status | Notes |
| --- | --- | --- | --- |
| Gateway core login/session routing | `chirp_gateway` | Supported | Main edge entry for TCP/WS login, heartbeat, kick flow |
| Auth basic token flow | `chirp_auth` | Supported | Default auth binary exists; without MySQL/libsodium it falls back to the simpler token validation path |
| Chat basic messaging | `chirp_chat` | Supported | Private messaging, history, offline queue, group management (create/join/leave/kick/invite/members), read receipts, typing indicators, message reactions, message edit/delete (with moderator support and bulk delete), @mention parsing/autocomplete, and direct-entry rate limiting (per-IP login / per-user send fixed windows, Redis-backed, fail-open) wired into the default binary |
| Chat distributed routing | `chirp_chat_distributed` | Experimental | Separate target; not the default documented service binary |
| Chat hybrid Redis + MySQL storage | `chirp_chat` / `chirp_chat_enhanced` | Experimental | With MySQL available, the default `chirp_chat` target builds the enhanced implementation; `chirp_chat_enhanced` is now a compatibility alias |
| Auth registration / refresh / brute-force / rate-limit stack | `chirp_auth` / `chirp_auth_enhanced` | Experimental | With MySQL and libsodium available, the default `chirp_auth` target builds the enhanced implementation; `chirp_auth_enhanced` is now a compatibility alias |
| Server plane hub (game backend <-> chirp) | `chirp_server_gateway` | Experimental | Service auth (`service_id` + secret), message injection routing toward chat, reliable event delivery (per-service queues, acks, reconnect redelivery); chat consumes injections as an internal peer; upstream injection also available over a Redis Stream (`--broker_redis_host`, consumer group + `XAUTOCLAIM` replay, needs Redis >= 6.2; downlink events still long-connection only) |
| NPC dialog (keyword rule engine) | `chirp_npc_dialog` | Experimental | Pure server-plane client (no player-facing listener): chat turns `npc:`-prefixed private messages into `npc.player_message` events, the service answers with keyword-table replies injected back as `SENDER_NPC` (at-least-once; a hub redelivery can duplicate a reply). Process-level smoke: `./test_services.sh --smoke-npc` |
| Offline-message push trigger | `chirp_chat` (default build) | Experimental | Offline private/group messages and server-plane injections enqueue a push through `PushBridge` -> notification (fire-and-forget, logged failures). Only wired into the default `chirp_chat` build (`main_enhanced` / `main_distributed` are not); actual delivery still depends on the notification transport stub |
| Social / presence | `services/social` | Experimental | Present as service code, but not validated as a core path |
| Voice signaling / WebRTC integration | `services/voice`, `sdks/core/modules/voice` | Experimental | Broad surface area, environment-heavy, not part of the minimal verified path |
| Notification delivery | `services/notification` | Experimental | Protocol face live on TCP 5006 / WS 5016 (6xxx device + push messages, 100% unit coverage); in-process device registry, per-user cooldown and payload builders are real, but provider HTTP delivery is a logging `PushTransport` stub (no TLS; inject a real transport for APNs HTTP/2 / FCM HTTP) |
| App gateway edge (companion apps) | `chirp_app_gateway` | Experimental | TCP 5200 / WS 5201: login/heartbeat/session binding like the game gateway plus 6xxx device-message forwarding to notification (requires an authenticated session, `user_id` pinned server-side); chat business packets are not accepted |
| Search service | `services/search` | Experimental | Present in tree, not established as a verified path |

## SDKs and Apps

| Area | Target | Status | Notes |
| --- | --- | --- | --- |
| C++ Core SDK | `sdks/core` | Experimental | Game-client integration base (`chirp::sdk::ChatClient`); process-level smoke covers login / bidirectional online delivery / offline-queue refill against a real `chirp_chat` (`./test_services.sh --smoke-sdk`). Still avoid calling it fully stable |
| Unity SDK | `sdks/unity` | Experimental | Contains TODO-backed social bindings |
| Unreal SDK | `sdks/unreal` | Experimental | Contains unimplemented return paths |
| Mobile companion app | `apps/mobile_companion` | Demo | UI and integration exist, but should not be presented as production-ready |
| Admin dashboard | `apps/admin_dashboard` | Stub | Uses mock data and demo pages rather than real backend integration |
| CLI client / benchmark tools | `apps/cli_client`, `tools/benchmark` | Demo | Good for smoke testing and manual validation |

## Test and Delivery Confidence

| Concern | Current State | Status |
| --- | --- | --- |
| Unit tests | 25 suites in `tests/unit`; every backend package that is linked into a test binary is at 100% line coverage per `scripts/run_coverage.sh` (documented `KNOWN_UNCOVERABLE` exclusions only). `app_gateway` and `voice` are not yet linked into any suite | Supported |
| Standard local build runs tests via `ctest` | `ctest --preset dev` (and `--preset coverage` with the gcov build); a fresh tree builds and passes | Supported |
| CI treats test failure as hard failure | `ci.yml` runs Debug + Release builds with `ctest`, plus a coverage job that fails when any package drops below 98% line coverage | Supported |
| Process-level smoke coverage | `test_services.sh --smoke / --smoke-chat / --smoke-sdk / --smoke-npc / --smoke-redis` exist and pass locally; none are wired into CI yet (tracked in TODO.md Current Focus) | Experimental |
| Docker Compose path for core services | Present | Supported |
| Roadmap matches default build outputs | Yes — TODO.md is the live roadmap (rewritten 2026-09); completed items are struck through in README.md | Supported |

## Recommended Public Positioning

For external readers, the repository should currently present itself as:

- a supported core backend skeleton for `gateway + auth + chat`
- an experimental playground for distributed chat, richer auth, voice, social, and multi-engine SDK work
- a demo repository for mobile/admin surfaces rather than a finished product suite
