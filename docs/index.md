---
layout: home
title: Home
hero:
  name: Chirp
  image:
    src: /logo.svg
    width: 160
  tagline: A lightweight real-time communication backend skeleton for game development
  actions:
    - text: Core
      link: /CORE
      theme: primary
    - text: Getting Started
      link: /guide/getting-started
      theme: secondary
    - text: 中文
      link: /zh/
      theme: alt
features:
  - icon: '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round" width="24" height="24"><path d="M9.5 14.5 14.5 9.5"/><path d="M7.5 16.5 6 18a3.5 3.5 0 1 1-5-5l1.5-1.5"/><path d="M16.5 7.5 18 6a3.5 3.5 0 1 1 5 5l-1.5 1.5"/></svg>'
    title: Core path
    details: Today the recommended path to verify first is gateway + auth + chat.
  - icon: '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round" width="24" height="24"><path d="M3 8.5h18"/><path d="M3 15.5h18"/><circle cx="8" cy="8.5" r="2"/><circle cx="16" cy="15.5" r="2"/><path d="M16 8.5h2"/><path d="M6 15.5h2"/></svg>'
    title: Two-plane architecture
    details: The game plane and the App plane deploy independently, and cross-plane communication is a native chat capability — direct connection over the same protocol, registration allowlists, version negotiation.
  - icon: '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round" width="24" height="24"><path d="M4 12h5"/><path d="M15 6.5h5"/><path d="M15 17.5h5"/><path d="M9 12c3.5 0 3.5-5.5 6-5.5"/><path d="M9 12c3.5 0 3.5 5.5 6 5.5"/></svg>'
    title: Two protocols
    details: TCP and WebSocket use the same length-prefixed Protobuf Packet protocol.
  - icon: '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round" width="24" height="24"><circle cx="12" cy="12" r="8.5" stroke-dasharray="3.2 3.2"/><path d="M12 7.5v4.5l3 1.8"/></svg>'
    title: Explicit boundaries
    details: Redis enhancements are optional; social, voice, notification, search and the SDKs vary in completeness — see the capability matrix.
footer: Apache-2.0 License | Copyright © 2024-Present Chirp Project
---

## Current positioning

Chirp today is "a runnable core communication skeleton + a batch of experimental extensions", not a product suite in which every directory is equally mature.

Read first:

- [Core](/CORE): the currently usable paths, protocols, service boundaries and local verification commands
- [Capability Matrix](/CAPABILITY_MATRIX): the real completeness of each service, SDK and application
- [Introduction](/guide/introduction): what the documentation covers, the supported paths and the boundary notes
- [Installation](/guide/installation): development environment and dependency preparation
- [Getting Started](/guide/getting-started): local build, Docker Compose and smoke tests
- [API Overview](/api/overview): Packet envelope, message IDs and core flows
- [Overall Architecture](/architecture): service topology, data flow and architecture judgments

## Documentation layers

| Layer | Representative pages | Purpose |
| --- | --- | --- |
| Current runtime | `CORE`, `CAPABILITY_MATRIX`, `guide/getting-started`, `api/overview`, `architecture`, `server_plane` | Currently usable paths, protocols and boundary constraints |
| Getting started | `guide/introduction`, `guide/installation`, `guide/getting-started`, `guide/deployment` | Environment preparation, local build and verification |
| Topics / historical | `design-notes/DEPLOYMENT`, `design-notes/DISTRIBUTED_DEPLOYMENT`, `design-notes/SCALABILITY*`, `design-notes/game_chat_*`, `design-notes/npc_dialog_system`, `design-notes/INTEGRATION_TEST_FIXES` | Design records, evolution plans and legacy notes |
| Compatibility pages | `API`, `QUICKSTART` | Compatibility entry points that prefer redirecting to maintained pages |

## Core services

| Service | Default port | Current status | Notes |
| --- | --- | --- | --- |
| Gateway | TCP 5000 / WS 5001 | Supported | Login, logout, heartbeat, session binding, optional Redis cross-instance kick |
| Auth | TCP 6000 | Supported | Basic token flow; the enhanced auth implementation is built when its dependencies are met |
| Chat | TCP 7000 / WS 7001 | Supported | Private chat, history, offline queue; the enhanced storage path is built when its dependencies are met |
| Server Gateway | TCP 8100 | Experimental | Service-plane hub: a game backend outbound persistent connection + `service_id`+secret credentials, message injection, and offline event queueing with redelivery — see [Server Plane](/server_plane) |
| NPC Dialog | no listening port | Experimental | A pure server-plane client: consumes `npc.player_message` events, replies from keyword rules and injects into chat — see [Server Plane](/server_plane) |
| Search | TCP 5007 | Supported | Message search: SQLite FTS5 index, 2248/2249 composite-cursor pagination; both SDK gateways forward via `--search_host` (degrade, no kick) |

## Key boundaries

The current architecture is reasonable for "a game chat backend skeleton, local verification, protocol integration and secondary development". It is not yet a production-grade unified communication platform: with `--chat_host` configured, the SDK gateway forwards chat business packets (2xxx) to chat over a per-client pipe (with chat replaying login authentication), 2248 search packets can be forwarded to the search service via `--search_host` (target port `--search_port`, default 5007), and planes such as social/voice still integrate independently; the server plane's injection chain is wired at loopback level (chat consumes injected messages, and the NPC dialog loop has a process-level smoke test, `--smoke-npc`), while some advanced capabilities lack a unified session and verification chain.
