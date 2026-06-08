# Chirp Capability Matrix

Last reviewed: 2026-06-08

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
| Chat basic messaging | `chirp_chat` | Supported | Core private messaging path exists |
| Chat distributed routing | `chirp_chat_distributed` | Experimental | Separate target; not the default documented service binary |
| Chat hybrid Redis + MySQL storage | `chirp_chat` / `chirp_chat_enhanced` | Experimental | With MySQL available, the default `chirp_chat` target builds the enhanced implementation; `chirp_chat_enhanced` is now a compatibility alias |
| Auth registration / refresh / brute-force / rate-limit stack | `chirp_auth` / `chirp_auth_enhanced` | Experimental | With MySQL and libsodium available, the default `chirp_auth` target builds the enhanced implementation; `chirp_auth_enhanced` is now a compatibility alias |
| Social / presence | `services/social` | Experimental | Present as service code, but not validated as a core path |
| Voice signaling / WebRTC integration | `services/voice`, `sdks/core/modules/voice` | Experimental | Broad surface area, environment-heavy, not part of the minimal verified path |
| Notification delivery | `services/notification` | Experimental | Contains placeholder behavior |
| Search service | `services/search` | Experimental | Present in tree, not established as a verified path |

## SDKs and Apps

| Area | Target | Status | Notes |
| --- | --- | --- | --- |
| C++ Core SDK | `sdks/core` | Experimental | Useful integration base, but repo docs should avoid calling it fully stable |
| Unity SDK | `sdks/unity` | Experimental | Contains TODO-backed social bindings |
| Unreal SDK | `sdks/unreal` | Experimental | Contains unimplemented return paths |
| Mobile companion app | `apps/mobile_companion` | Demo | UI and integration exist, but should not be presented as production-ready |
| Admin dashboard | `apps/admin_dashboard` | Stub | Uses mock data and demo pages rather than real backend integration |
| CLI client / benchmark tools | `apps/cli_client`, `tools/benchmark` | Demo | Good for smoke testing and manual validation |

## Test and Delivery Confidence

| Concern | Current State | Status |
| --- | --- | --- |
| Unit test sources exist | Yes | Supported |
| Standard local build directory always runs tests successfully via `ctest` | Not guaranteed | Experimental |
| CI treats test absence/failure as hard failure | Not consistently | Experimental |
| Docker Compose path for core services | Present | Supported |
| Roadmap matches default build outputs | No | Needs correction |

## Recommended Public Positioning

For external readers, the repository should currently present itself as:

- a supported core backend skeleton for `gateway + auth + chat`
- an experimental playground for distributed chat, richer auth, voice, social, and multi-engine SDK work
- a demo repository for mobile/admin surfaces rather than a finished product suite
