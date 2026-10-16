> 🌐 Language: [中文](./communication_core.md) | **English** (this page)

# Chirp Communication Core model convergence

> Status note: this document is an architecture convergence draft. In this text [current] marks the runtime surface that is implemented today, and [proposal] marks design intent that has not landed. Do not treat [proposal] as a default supported runtime feature. This document exists so that later protocol and service evolution share a vocabulary; it does not change the existing wire protocol.

## 1. Why converge

Chirp has grown from "a chat server" into "real-time communication infrastructure for games": the game plane (client edge + in-game chat), the App plane (companion-side identity, push, hub), the service plane (game-server outbound injection, NPC/system messages), plus experimental surfaces such as social / voice / notification / search / npc.

The risk comes with it: if we keep adding features horizontally as "Chat → Social → Voice → Notification …", we end up with a grab bag that does a little of everything. The core abstractions must first converge into one shared vocabulary, so that new capabilities become applications on top of Communication rather than new business being stuffed into the core.

Choosing "Communication" instead of "Chat" as the first-level abstraction is the only judgment this file makes.

## 2. The core model

```
Communication
├── Identity   who is speaking (player / npc / system / service / bot)
├── Channel    where (private / group / world / system-prefixed channels)
├── Message    what was said (message_id / sender / receiver / channel / payload / metadata)
├── Delivery   how it arrives (offline queue, at-least-once redelivery, receipts)
├── Pipe       who connects to whom (client / service / peer, three kinds of trusted connections)
└── Event      the event channel for the service plane / clients (injection, push, kick, typing…)
```

- Chat = the first consumer of Message + Channel + (part of) Delivery.
- NPC / Social / Party / Notification = upper-layer applications reusing the identity and injection channels, not core entities.
- Voice / Search = independent modules, not part of the Communication core (see §8).

## 3. Current-state mapping

Every row corresponds to a runnable implementation today; no pie in the sky:

| Concept | [current] |
| --- | --- |
| Identity | platform `player_id` (app_auth JWT), in-game `game_user_id` (game_sdk_gateway local token); the service plane injects with `SERVICE` identity. Injected messages already carry a non-player identity (SYSTEM / NPC / SERVICE semantics landed on the server_gateway injection surface, see services/game/server_gateway and npc_dialog) |
| Channel | private chat, group chat (`groups`), world channel (the batch messaging surface), system-channel prefix (`<game_id>:<channel>` cross-plane addressing); `npc:`-prefixed private chat goes over the NPC injection line |
| Message | `message_id` generation, history stored in MySQL (`messages` table), offline queue, message edit/delete/reactions/read receipts (`read_receipts`/`read_cursors`); sender kind distinguishes players from non-players via the sender field |
| Delivery | recipient offline → offline queue (200-per-user in-memory fallback, optional Redis), `SEND_MESSAGE_RESP` returns `TARGET_OFFLINE`, redelivery at next login; service-plane injection is at-least-once (hub ack + Redis Streams PEL replay); "`OK` means only that the service plane accepted it, not that the player side was delivered" is written into the README's current boundaries |
| Pipe | client edge (`game_sdk_gateway` 5000/5001, `app_sdk_gateway` 5200/5201), service-plane outbound persistent connection (`game_server_gateway` 8100), the chat direct entry point and trusted bridge (the trusted pipe verified by `--smoke-edge`), game_chat ↔ app_chat registration + allowlist + version negotiation |
| Event | the service-plane injection channel (`InjectMessageNotify`), the chat offline push bridge → `app_notification` (6xxx), typing/read events |

## 4. The target model ([proposal])

### 4.1 ActorKind (converged 2026-09-04, with a shape adjustment)

The existing "player / NPC / SYSTEM / SERVICE" sender semantics are formally converged into a single enum, serving as the sender-side identity for Message and injection events. **Note on the landed shape**: rather than inventing a new six-valued `ACTOR_*` enum, the two existing enums on both sides are pinned as an aligned contract — `chat.proto SenderKind` (USER=0 / SYSTEM / NPC / SERVICE) and `game_server_gateway.proto SenderKind` (UNKNOWN=0 / SYSTEM / NPC / SERVICE) are numerically aligned, with comments cross-referencing each other; the injection boundary (`inject_consumer`) passes the value through per the contract and rejects UNKNOWN. Landed across the chain:

- **Full-chain persistence**: `MessageData.sender_kind` round-trips with the Redis mirror serialization; a `sender_kind` column on the `messages` table (new databases via `init_db.sql`, existing databases via `scripts/upgrade_db_messages.sql`); the migration worker converts with kind; `GetHistory` cold/hot merging, offline redelivery and history dispatch all rebuild kind.
- **Fan-out fidelity**: cross-plane `deliver_copy` keeps the source kind in each subscriber's private copy (NPC/system announcements do not degrade into player messages, honoring the proto fan-in "SENDER_SERVICE copy" promise).
- **Anti-spoofing**: `SendMessageRequest` has no sender_kind field, so a client is structurally unable to claim a non-player identity.

```
ACTOR_PLAYER     a player (a user verified at the client edge) — chat.SenderKind.SENDER_USER
ACTOR_NPC        a rule/LLM-driven dialog character              — SENDER_NPC (wired end to end)
ACTOR_SYSTEM     system announcements, status broadcasts          — SENDER_SYSTEM (wired end to end)
ACTOR_SERVICE    the game server's service-plane identity (service_id + secret) — SENDER_SERVICE (wired end to end)
ACTOR_BOT        customer-support/moderation/ops bots             — not implemented (no producer, no enum value added)
ACTOR_GM         GM                                               — not implemented (same as above)
```

Constraint: **user credentials and service credentials must not be mixed** — the client edge verifies only user tokens, the service plane verifies only service credentials, and the two planes make the identity declaration at the injection boundary rather than trusting a kind passed in by a client.

### 4.2 Message / Delivery states

```
Message  ── send ──► Accepted ──► Persisted ──► (Queued | Delivered) ──► Acknowledged
                                                 │
                                                 └── offline queue → redelivery at next login
```

Delivery semantics become protocol-level vocabulary instead of scattered across implementation comments:

| Semantic | Meaning | [current] counterpart |
| --- | --- | --- |
| BEST_EFFORT | best effort, no delivery guarantee | the push bridge is fire-and-forget (app_notification's default logging transport only logs; `--push_transport http` is a real HTTP POST and still has no delivery receipt) |
| AT_LEAST_ONCE | at least once, redeliver until acknowledged | service-plane injection: hub ack + Redis Streams PEL replay |
| AT_MOST_ONCE | at most once, duplicates are dropped | consumer-side dedup has landed (2026-10-04): `ChatMessage.delivery_id` (redelivery copies are minted by the server; an ack-timeout redelivery into the queue preserves the original value — retries of the same delivery share one id), the SDK's `MemoryMessageStore` archives idempotently by `message_id`; the service-plane injection idempotency key is `inject_id` |
| DURABLE | persisted to the history surface, survives restart | chat messages archived to MySQL history (the `messages` table) |

The mapping must be stated explicitly: **`OK` answers only Accepted**. Whether the player side reached Delivered/Acknowledged is answered by redelivery and read receipts, which are both a second chain outside "accepted". Today's one-sentence "OK means accepted by the service plane only" is hereby promoted to the formal statement of the state machine above in the protocol documentation.

### 4.3 Separation of request_id and sequence (landed in Packet)

- `sequence`: a monotonically increasing sequence number within a connection, used for local ordering of request-response pairing.
- `request_id` (`Packet` field 4, **landed across the chain, closed out 2026-10-04**): a distributed correlation id for cross-plane / cross-service log tracing. 0 = not provided — default generation is filled in across the chain: the gateway's ServiceBridge falls back to "generated within the connection" (monotonically increasing) for client outbound 2xxx packets and service-plane outbound packets; the chat direct entry point (bare-protocol clients) falls back to a process-level monotonic value; the peer surface (ChatPeerHub/ChatPeerLink) generates monotonically within each of its own connections on outbound (both directions, hub→spoke and spoke→hub); the SDK send side (`MakePacket` in sdks/core, `rawSend` in sdks/ts and apps/shared/protocol) generates it automatically. Explicitly supplied values are never rewritten and are passed through verbatim along the forwarding chain. On the log side: server_gateway injection outbound logs `req=`, and chat injection inbound logs `inject received ... req=`.
- `message_id` and `delivery_id` are separated: one message is sent once and delivered many times (offline redelivery, multiple devices), each delivery having its own id, giving dedup and receipts a subject. **`delivery_id` (`ChatMessage` field 15) has landed**: empty = the first online delivery (the delivery subject is then `message_id`); offline redelivery copies get independently minted values; an ack-timeout redelivery into the queue preserves the original value. Division of labor on the consumer side — the UI dedupes idempotently by `message_id`, the transport layer dedupes by `delivery_id`; read receipts remain message-level (subjected to `message_id`). **An ack explicitly carrying the delivery subject has landed (2026-10-04)**: `MessageAck.delivery_id` / `MessageNack.delivery_id` — the server's `DeliveryAckManager` matches in-flight deliveries exactly by delivery subject (a non-empty `delivery_id`; empty = fall back to `message_id` for the first delivery); old clients with an empty `delivery_id` use compatibility matching, a same-id redelivery refreshes rather than re-suspending, and a late ack clears the offline copy by subject. **Per-device splitting of the offline queue has landed (2026-10-04, closing out the remainder)**: the offline bucket slot = `NormalizePlatformId(platform)` (the same unit as SessionRegistry's (user, platform) slot; empty → the `default` shared bucket, with a key shape identical to the pre-split user-level queue, so zero migration). Three-arm semantics — ① enqueue arm (no device online at send time / offline group members) always lands in the `default` shared bucket, and whichever device logs in first wins (same behavior as before the split, so the delivery guarantee does not degrade); ② redelivery arm (at login) pops this slot's bucket ∪ the `default` bucket (this slot first), with per-device fidelity starting at claim time; ③ the ack-redelivery-into-queue arm lands in the device bucket Track recorded (a live first delivery records `default`, a redelivery records the claiming device's slot, and a redelivery is not hijacked by another device's login), with late-ack cleanup landing precisely in the same (user, slot) bucket. Zero protocol change: a slot is a purely server-side routing concept and proto/SDK do not change. Explicit trade-off — Redis does not maintain a device roster (the Presence adjacency surface, not done); if a claiming device is permanently abandoned, the redelivered copies in its device bucket linger until TTL (the history archive is the same-source fallback, and an ownerless copy never lingers). See [Server plane](../server_plane.en.md). The injection-plane idempotency key `inject_id` and the delivery-tracking `TrackMessageResponse.tracking_id` are subjects of the same family.

### 4.4 Pipe classification ([proposal] — formalizing a concept, no new protocol)

```
Client Pipe   client ──► gateway ──► chat (user credentials)
Service Pipe  game server ──► server_gateway ──► chat (service credentials)
Peer Pipe     game_chat ⇄ app_chat, chat ⇄ chat (trusted peer, allowlist + version negotiation)
```

The current state is already implemented implicitly along these lines; the goal of convergence is to make "which kind of Pipe a given connection is" an enumerable property (logging, rate limiting and credential verification all branch on Pipe type), and to make explicit that **Gateway = Transport Edge**: the gateway handles connections/login/logout/heartbeat/kick/rate limiting/protocol adaptation/forwarding and does not take on Chat/Social/NPC business routing (the README's "the gateway is not yet a business routing layer, chat packets go to chat" is already the current statement of that constraint; this document pins it as the convention that it must never evolve into a God Gateway).

### 4.5 Redis's position

- Redis = Runtime Coordination (sessions, cross-instance kicks, publish/subscribe, the Redis Streams injection broker, temporary offline-queue fallback).
- Redis ≠ Source of Truth. Durable history/profiles land only in MySQL; Redis data can be rebuilt at any time (clearing it does not damage business accounts).

### 4.6 app_chat evolution constraint

app_chat is now the hub of a hub-spoke arrangement, aggregating multiple game channels, identity bindings and unread counts. Convergence constraint: **app_chat is a routing hub, not a single-point logical database**. Future scaling shards by channel/game, the hub layer only does routing and identity mapping, and never becomes the sole source of truth for message storage (messages still live in each plane's chat MySQL). Distributed chat is not being built now: a single-writer chat + horizontal gateways + a reliable service plane, first get the baseline (connections/throughput) established, then talk about sharding.

### 4.7 Identity binding (documented, implementation surface not expanded)

Three layers in the identity chain:

```
platform player_id   the platform account (issued by app_auth, spans multiple games)
  └─ game_user_id    the in-game user (authoritative at the game backend, asserted via the 5013 binding)
       └─ character  a character (proposal, reserved: today game_user_id on the wire is the chat identity,
                     the character layer is not built; introduce it by adding a layer to the binding tuple,
                     without changing the existing keys)
```

Current implementation (for contract details see [Server plane](../server_plane.en.md) "Player identity binding"):

- `player_id ↔ (game_id, game_user_id)` binding: the four RPCs 5013/5015/5017/5019, with a `binding_id` idempotency key (the same id with a different tuple → `INVALID_PARAM`); one `(game_id, game_user_id)` binds to only one player, and the game backend is authoritative (rebinding replaces the old one).
- Cross-plane replies reverse-look-up the sender's game identity via `ResolveGameUser` (when a player has multiple bindings in the same game, the lexicographically smallest is taken, guaranteeing determinism).
- The credential boundary is as in 4.1: a platform `player_id` comes only from App-plane verification (pinned by app_auth / app_gateway), and a `game_user_id` comes only from a game-plane assertion; the injection boundary recognizes only the binding result and does not trust a client's self-reported identity.

## 5. Convergence roadmap

P0 (lands with this document, mostly documentation/protocol statements):

1. This document becomes the shared vocabulary for later design; new services state in their title which upper-layer application they belong to.
2. The delivery-semantics vocabulary enters the delivery-semantics section of `docs/api/overview.md`; the README's "OK means accepted only" statement points at this document.
3. The gateway boundary is pinned into the protocol documentation (Pipe classification), and the social/voice/search docs are annotated in sync as "non-core".

P1 (needs a wire extension, separate batches):

4. `request_id` into Packet (✅ closed out: the protocol field + service-plane outbound generation + gateway bridge fallback + chat inbound logging; ✅ all remaining surfaces landed — the chat direct fallback, peer (hub/link) outbound generation, SDK/client send-side generation; explicit values pass through end to end, see §4.3).
5. `delivery_id` into the delivery/receipt protocol; dedup semantics (AT_MOST_ONCE) landed (✅ first step: the `ChatMessage.delivery_id` field + redelivery minting/redelivery-preserves-original-value + the SDK store idempotent by `message_id`; ✅ first remainder block: `MessageAck`/`MessageNack` explicitly carry `delivery_id`, and `DeliveryAckManager` matches exactly by delivery subject; ✅ second remainder block: per-device splitting of the offline queue — the three-arm semantics in §4.3, zero protocol change).
6. Identity binding documented (platform player_id ↔ game game_user_id ↔ character), implementation surface not expanded (open).

P2 (explicitly not made core):

7. Presence as a standalone service (presence today rides on the chat/Web side and is not split into a service for now).
8. Voice / Search stay plugin surfaces; Voice does not enter the core abstraction.

## 6. Related documents

- [Overall architecture](../architecture.en.md) — two-plane separation, the hub-spoke integration model
- [API overview](../api/overview.en.md) — delivery semantics and the Packet format
- [Server plane](../server_plane.en.md) — the service-plane injection contract
- [NPC dialog system design](./npc_dialog_system.en.md), the [scalability series](./SCALABILITY.en.md) — evolution references for their respective capabilities
- [Capability matrix](../CAPABILITY_MATRIX.en.md) — real completeness (the [proposal] items in this document are never in it)
