> 🌐 Language: [中文](./server_plane.md) | **English** (this page)

# Server Plane: game backend integration

Status: **Experimental**——the hub (`chirp_game_server_gateway`), the chat-side consumer, and the Redis Streams broker fallback are all implemented and verified by unit tests (100% line coverage): chat dials in as an internal peer, and injected messages go through the same storage/delivery tail as player messages. The two-plane topology is in [overall architecture](./architecture.en.md).

> **Architecture note (2026-09-21; landed 2026-09-22).** Target topology: `game_chat` registers into `app_chat` using the native peer registration protocol (with whitelist and version negotiation), with no external bridging process. Identity binding, channel subscriptions, and the unread badge ledger are all handled internally by `app_chat` (`PlayerDirectory`); fan-in delivery moved up accordingly——`game_chat` hands channel messages over the peer to `app_chat`, which fans them out by subscription. `chirp_game_server_gateway` keeps its original duties: game backend injection + reliable event delivery downward; it no longer carries any data face of WP-8.

## What this is

The server plane is how the game backend talks to chirp. It is deliberately separate from the player edge:

- **Dial-out**: the game server opens a connection to `chirp_game_server_gateway`. chirp never needs to reach into the game network, and game servers on private subnets need no public callback endpoints.
- **Service identity, not user identity**: peers authenticate with `service_id` + shared secret. They are never user accounts, never appear in sessions/kick/online state, and injected messages carry a non-user sender type (`SYSTEM` / `NPC` / `SERVICE`).
- **Same framing**: TCP + `[uint32_be size][chirp.gateway.Packet]`, using the `5xxx` msg-id range.

The game backend does not have to re-implement this wire contract from scratch: `sdks/go` (package `chirp`, see its README) is the reference Go client——the auth handshake, server-assigned heartbeat, sequence-correlated inject/event RPC, at-least-once event ack, and hang-up-and-reconnect on failure——all consistent with the semantics of the in-repo C++ peer (`libs/network/server_gateway_peer.cc`).

### Credential boundaries

The `service_id` + secret pair is an appkey/appSecret-style credential: it identifies the connecting backend and is long-lived. This gives two iron rules:

- **Never take it into a client.** Anything baked into a game client or app binary is effectively public. Clients hold short-lived user tokens; the game backend exchanges/derives these tokens after the player's own login. See [credential model](./architecture.en.md#credential-model).
- **Never use it to impersonate a user.** Injections carry `sender_kind` (`SYSTEM` / `NPC` / `SERVICE`) precisely so the server plane can act without pretending to be some player account.

## Running it

```bash
cmake --preset dev && cmake --build --preset dev
./build/services/game/server_gateway/chirp_game_server_gateway \
  --port 8100 \
  --service game=game-secret \
  --service chat=chat-secret \
  --chat_service_id chat \
  --heartbeat_interval 30 \
  --auth_timeout 10 \
  --max_pending 1000
```

| Flag | Default | Meaning |
| --- | --- | --- |
| `--port` | 8100 | TCP listen for service connections |
| `--service` | (none) | repeatable `service_id=secret` credential entries |
| `--chat_service_id` | `chat` | service that receives message injections |
| `--heartbeat_interval` | 30 | delivered keepalive cadence (seconds) |
| `--auth_timeout` | 10 | authentication deadline; the connection is closed on timeout |
| `--max_pending` | 1000 | per-service queued (unacked) event ceiling |

Without `--service` entries, the hub starts normally but refuses all logins (fail closed).

## Connection lifecycle

1. The game server dials TCP and must send `SERVER_AUTH_REQ` within `--auth_timeout` seconds, otherwise the connection is closed.
2. `SERVER_AUTH_RESP` returns the result, server time, and the assigned `heartbeat_interval_seconds`. The connection must see at least one packet of traffic every `2 × heartbeat_interval` seconds, otherwise it is closed.
3. A second login from the same `service_id` on another connection evicts the old connection; the evicted connection is closed.

## Upstream: message injection

A trusted service asks chirp to deliver a message whose sender is not a user (announcements, NPC lines, trade status). `INJECT_MESSAGE_REQ` carries `chirp.server_gateway.MessageInjectRequest`:

- `inject_id`: an idempotency key supplied by the caller (echoed back in the response)
- `sender_kind`: sender type, one of `SENDER_SYSTEM` / `SENDER_NPC` / `SENDER_SERVICE` (values aligned with the chat-side `SenderKind`; UNKNOWN is rejected. The value is then passed through end to end: stored in `messages.sender_kind`, offline re-delivery, history delivery, and the cross-plane fan-out copies all keep the original value)
- `sender_id`: e.g. `npc:blacksmith_01`, `trade`
- `channel_type` + `channel_id`, or `receiver_id` for the one-to-one case
- `content`: the message body
- `game_id`: **must be empty**. Fan-in delivery carrying `game_id` has moved to the app plane (see next section); the hub answers this shape with `INVALID_PARAM`

Response codes:

| Code | Meaning |
| --- | --- |
| `OK` | validation passed and handed to the chat service (`InjectMessageNotify`) |
| `INVALID_PARAM` | empty content / `SENDER_UNKNOWN` / empty `sender_id` / neither channel nor receiver / carrying `game_id` (the fan-in shape is retired) |
| `SERVER_UNAVAILABLE` | chat service not connected, or write failed |

The meaning of `OK` is still "accepted by this plane": chat consumes the injection asynchronously, and the response does not confirm the player received it.

### Fan-in delivery (WP-8 slice 3; moved to the app plane)

Injections carrying `game_id` used to fan out at this hub; after the WP-8 move that path no longer exists. Fan-in now lives in `app_chat`'s `PlayerDirectory::FanoutChannelMessage` (`services/shared/chat/src/player_directory.cc`): `game_chat` hands non-PRIVATE channel messages over the peer (`CHANNEL_MESSAGE_NOTIFY`) to `app_chat`, which, via its subscription registry keyed by `(game_id, channel_id)`, hands **each subscriber a private-chat copy** (`channel_type` becomes `PRIVATE`, the receiver is pinned to the subscriber's `player_id`, and the original `sender_id`/`content` is preserved), and every accepted copy also increments the recipient's unread ledger (see "Unified unread" below).

The hub's only remaining relationship to fan-in is: **refusal**. An `INJECT_MESSAGE_REQ` carrying `game_id` (and broker entries carrying a `game_id` field) is answered `INVALID_PARAM` and acked immediately——the old "stuff game_id into the injection" integration style must change to: the game backend connects directly to `app_chat`'s client port and uses the WP-8 RPC (see below), or broadcasts channel messages over the peer plane.

Empty subscription is a semantic no-op; if the subscriber count exceeds `--max_fanout_per_message` (an `app_chat` parameter, default 10000) the message is dropped whole and logged——neither outcome produces a copy or records unread.

### Chat-side consumption

The chat service connects to the hub as an internal peer (`--server_gateway_host`, empty by default, i.e. off) and answers auth and heartbeat. Forwarded `InjectMessageNotify` goes through the same tail as `SEND_MESSAGE`:

- store history first; there is no member validation and no mention cooldown (the sender is not a user).
- `PRIVATE` with the recipient online is delivered immediately; otherwise it is queued for the offline user.
- non-private channels (`TEAM` / `GUILD` / `WORLD`) broadcast to members and queue for offline members.
- a malformed `InjectMessageNotify` is logged and skipped; the connection stays up.

### Broker fallback (upstream via Redis Streams)

For game backends that cannot host long-lived connection clients, the same injection path can also run over a Redis Stream. Start the hub with `--broker_redis_host` (empty, i.e. by default, disables the consumer):

| Flag | Default | Meaning |
| --- | --- | --- |
| `--broker_redis_host` | (empty) | Redis host; empty disables the broker |
| `--broker_redis_port` | 6379 | Redis port |
| `--broker_stream` | `chirp:server_plane:inject` | the stream to consume |
| `--broker_group` | `chirp-plane` | consumer group (created idempotently) |
| `--broker_consumer` | `<hostname>:<pid>` | consumer name within the group |
| `--broker_claim_min_idle_ms` | 30000 | `XAUTOCLAIM` min-idle-time (for redelivery) |

Requires Redis >= 6.2 (`XAUTOCLAIM`). The producer writes one entry per message as flat string fields——any language that can `XADD` can integrate:

| Field | Required | Meaning |
| --- | --- | --- |
| `service_id` | yes | service identity (must exist in `--service`) |
| `secret` | yes | shared secret, same credential set as the connection face |
| `sender_kind` | yes | `SYSTEM` / `NPC` / `SERVICE` (`SENDER_` prefix tolerated) |
| `channel_type` | yes | `PRIVATE` / `TEAM` / `GUILD` / `WORLD`, or `0`–`3` |
| `sender_id` | yes* | validated downstream, same as the proto path |
| `channel_id` / `receiver_id` | — | channel target, or the one-to-one receiver |
| `game_id` | no | no longer supported: if set, the entry is acked as `INVALID_PARAM` immediately (fan-in delivery moved to the app plane) |
| `content` | yes* | message body |
| `inject_id` | no | idempotency key; generated as `<consumer>-<seq>` when absent |
| `reply_to` | no | name of the Stream that receives the `{inject_id, code}` result entry |

Semantics:

- the hub runs one consumer group (`XREADGROUP ... BLOCK`) and feeds every entry into the same `HandleInject` path as the long-connection face; validation and response codes are identical.
- `OK` / `INVALID_PARAM` / `AUTH_FAILED` are **acked immediately** (`XACK`): malformed or rejected entries are poison pills and must not be replayed.
- `SERVER_UNAVAILABLE` (chat service offline) is **not acked**: the entry stays in the pending entries list and is swept up for redelivery by the periodic `XAUTOCLAIM` until chat recovers. Redelivery is unbounded by design——the poison pills are already blocked by the immediate-ack rule above.
- With `reply_to` set, the result (`inject_id` + the `ErrorCode` name, e.g. `OK`) is written back only for a **terminal state that has already been acked**, so a replayed entry is answered exactly once.
- A transport failure between commands disconnects and reconnects; unacked entries are replayed. Overall at-least-once.

## Downstream: events (at-least-once)

`EVENT_PUBLISH_REQ` (`chirp.server_gateway.EventPublishRequest`) publishes an event that must reach a target service——for example a quest trigger produced by chat-side logic:

- `event_id`: an optional caller idempotency key; generated when absent (`evt-<ts>-<n>`) and echoed in the response
- remaining request fields: `target_service_id` (target service), `event_type` (event type), `payload` (payload)

Delivery semantics:

- target **online** → delivered immediately as `EVENT_DELIVER_NOTIFY` (`queued=false`).
- target **offline** → queued per service (`queued=true`), delivered on its next login/reconnect.
- events stay queued until `EVENT_ACK_REQ` confirms them. Reconnect redelivers all unconfirmed events; each delivery increments `attempt`.
- when the queue is full (over `--max_pending`), publishing is refused with `SERVER_UNAVAILABLE` instead of quietly dropping old events. The publisher retries with backoff.
- ack is idempotent; an unknown id returns `OK`.
- a closed old connection that was evicted does not reset the in-flight tracking of a live connection (no duplicate redelivery storm).

## NPC dialogue service

`services/game/npc_dialog` is the first event consumer on this plane: a pure server plane client (no player-side listener) that closes the NPC dialogue loop end to end.

- **Upstream (chat → hub → npc_dialog).** chat rewrites a private chat a player sent to a `npc:`-prefixed recipient into an `EventPublishRequest` of type `npc.player_message` (payload: `chirp.chat.NpcPlayerUtterance`; `event_id` = the chat message id) and sends it fire-and-forget——the player's `OK` means accepted, not that a reply will come. The player's original words stay in the chat history.
- **Replies (npc_dialog → hub → chat).** the responder generates a reply from a keyword rule table (`npc_id<TAB>keyword<TAB>reply` TSV, `*` = that NPC's fallback line, ASCII case-insensitive substring match, first hit wins; built-in demo rules when `--rules_file` is not given), injects it back to the player on a private channel as `SENDER_NPC`, with `event_id` as the `inject_id` idempotency key.
- **ack policy.** unknown event types, unparseable payloads, and utterances missing sender/NPC identity are acked immediately (poison pills: retrying can never make them valid). Valid events are acked only **after** the hub accepts the reply injection (`OK`); any other outcome stays unacked and the hub redelivers——at-least-once. **Redelivery dedup (2026-09):** once a reply has been accepted, the event id is recorded in a bounded recent window (1024 entries); a redelivered, already-answered event is acked and not re-injected; an event whose injection never succeeded never enters the window, so its redelivery is a real retry. Duplicate deliveries that arrive before the first attempt resolves are not suppressed (the hub serializes delivery; this is only a theoretical edge).

Process-level verification: `./test_services.sh --smoke-npc` runs the hub + chat + npc_dialog as real processes and checks keyword replies, fallback replies, history (player words + reply) and the offline queue re-delivery path.

## Player identity binding (WP-8 slice 1)

The aggregation plane needs a platform-level `player_id` spanning multiple games; the game backend asserts those bindings here. After the WP-8 move (2026-09-22), the four WP-8 RPC blocks (5013-5030) live entirely in **`app_chat`** (`chirp::chat::PlayerDirectory`, `services/shared/chat/src/player_directory.cc`, with the registry in `identity_registry.{h,cc}` in the same directory), no longer passing through `chirp_game_server_gateway`. To call them: dial `app_chat`'s client port as a trusted service, pass the `SERVER_AUTH_REQ` trust gate first (same credential system as the server plane, entries like `chat=chat-secret`), then send the WP-8 requests; any connection that has not passed the trust gate is answered `AUTH_FAILED`.

- `BIND_PLAYER_IDENTITY_REQ` (5013)——`binding_id` (an idempotency key chosen by the caller), `player_id`, `game_id`, `game_user_id`. Same id + same tuple again → `OK` with `existed=true`; same id + different tuple → `INVALID_PARAM` (reusing a key would quietly defeat duplicate detection). A `(game_id, game_user_id)` pair can bind to only one player: asserting again with a new `binding_id` evicts the old binding (number change / unbind-then-rebind——the game backend is authoritative).
- `UNBIND_PLAYER_IDENTITY_REQ` (5015)——by `binding_id` **or** by the full `(game_id, game_user_id)` pair; neither can be given both at once or neither (otherwise `INVALID_PARAM`). Target absent → `OK` (idempotent).
- `GET_PLAYER_IDENTITIES_REQ` (5017)——all bindings for a `player_id`.
- `RESOLVE_GAME_USER_REQ` (5019)——`(game_id, game_user_id)` → `player_id` (`OK` with an empty `player_id` when unbound).

Storage is in-memory authoritative + write-through Redis mirror (`chirp:binding:entry:<binding_id>` = the serialized `StoredIdentityBinding`, enabled on `chirp_chat` via `--binding_redis_host`/`--binding_redis_port`, off by default). On startup all existing entries are replayed; corrupt records are skipped and logged. Redis write failures are best-effort——memory stays authoritative, and the next change to the same record retries the write——so Redis failure degrades to pure in-memory semantics rather than an error.

This registry is the shared foundation that both aggregation-plane designs need (a shared multi-tenant core with game namespaces, or a federation bridge); fan-in delivery and unified unread (both shipped, see context) are built on top of it.

## Player channel subscriptions (WP-8 slice 2)

Binding answers "which platform player is this game user"; subscriptions answers "which game channels does this player want". The subscription registry (`SubscriptionRegistry`) shares `app_chat`'s `PlayerDirectory` with fan-in delivery (`services/shared/chat/src/subscription_registry.{h,cc}`): the fan-out path reads its (game_id, channel_id) reverse index (see "Fan-in delivery" above)——the registry itself still only stores intent, and the delivery decision lives in the fan-out path.

- `SUBSCRIBE_PLAYER_CHANNEL_REQ` (5021)——`player_id`, `game_id`, `channel_id`, plus an optional `subscription_id`. When an id is carried, it is the caller's idempotency key: same id + same tuple again → `OK` with `existed=true`; same id + different tuple → `INVALID_PARAM` (reusing a key would quietly defeat duplicate detection). The `(player_id, game_id, channel_id)` triple is globally unique: subscribing to the same tuple with a new id evicts the old record——the asserting party is authoritative (e.g. a game revamp reorders channels). When `subscription_id` is **empty**, the server mints one (`sub-...`): this is the player self-service path, and app_gateway pins `player_id` to the authenticated user before forwarding, so repeated subscriptions of the same tuple converge on the existing record (stable id, `existed=true`) instead of piling up rows.
- `UNSUBSCRIBE_PLAYER_CHANNEL_REQ` (5023)——by `subscription_id` **or** by the full `(player_id, game_id, channel_id)` triple; cannot be given both at once or neither (otherwise `INVALID_PARAM`). Target absent → `OK` (idempotent).
- `GET_PLAYER_SUBSCRIPTIONS_REQ` (5025)——all subscriptions for a `player_id`, with an optional `game_id` filter ("my subscriptions in game X").

The same set of six message ids serves two classes of caller: the game backend connects directly to `app_chat`'s client port through the trust gate, and players reach the same handlers via app_gateway forwarding——the app edge pins `player_id`, so a client can only ever create, list, or delete its own subscriptions.

Storage mirrors the bindings: in-memory authoritative + write-through Redis mirror (`chirp:subscription:entry:<subscription_id>` = the serialized `StoredChannelSubscription`, enabled on `chirp_chat` via `--subscription_redis_host`/`--subscription_redis_port`, off by default), startup replay skips corrupt records, best-effort writes degrade Redis failure to pure in-memory.

Open question (revisited when fan-in went live, still retained): subscriptions do not check for existing identity bindings——over self-service, a player can subscribe to channels of a game they have never played. Fan-in delivery shipped with a permissive selection (binding is required nowhere; backend assertions stay trusted), so `app_chat`'s fan-out has no cross-registry dependency. Whether the self-service path should eventually require binding is still an open product decision.

## Unified unread (WP-8 slice 4)

`app_chat` maintains an `UnreadLedger` (`services/shared/chat/src/unread_ledger.{h,cc}`, part of `PlayerDirectory`): per-player, per-`(game_id, channel_id)` badge counts counting **unhandled fan-in notifications**——each accepted fan-out copy increments one (see "Fan-in delivery" above). This is a badge, not a read cursor: it cannot see the chat service's read state (gateway 2201-2207), and the two do not feed each other. Unsubscribing does not clear the badge——marking read is the only decrementing path, and a failed delivery is never rolled back. Counters are `int32`.

Two RPCs (the game backend connects to `app_chat` directly through the `SERVER_AUTH_REQ` trust gate; players reach it via app_gateway forwarding with `player_id` pinned to the authenticated user):

- `MARK_CHANNELS_READ_REQ` (5027)——layered selector: with `channel_id` (requires `game_id`) clears that one channel; with only `game_id` clears all channels of that game; both empty clears everything for the player. Idempotent: an absent target returns `OK` with `cleared = 0`.
- `GET_UNREAD_SUMMARY_REQ` (5029)——one `UnreadSummaryEntry` per non-zero count (`game_id`, `channel_id`, `unread_count`), sorted by `(game_id, channel_id)`, plus `total_unread`——the sum after an optional `game_id` filter ("my unread in game X").

Storage mirrors each registry: in-memory authoritative + write-through Redis mirror (`chirp:unread:entry:<player_id>:<game_id>:<channel_id>` = the serialized `StoredUnreadEntry`, enabled on `chirp_chat` via `--unread_redis_host`/`--unread_redis_port`, off by default), startup replay skips corrupt records and zero counts, best-effort writes degrade Redis failure to pure in-memory. Cleared entries are deleted from the mirror rather than stored as zero, so counters never revive or accumulate. The key components are concatenated as-is: ids containing `:` can alias to another entry's key on disk——the in-memory map holds the exact tuples, so this only affects restart fidelity for odd ids.

## Cross-plane replies (App player [game channel, 2026-09-22]

The reverse direction of fan-in delivery: an App player talking in a game channel. The App player sends a regular `SEND_MESSAGE_REQ` on `app_chat`'s main port, with a non-`PRIVATE` `channel_type` and a `channel_id` shaped `<game_id>:<bare>` (splitting at the first `:`; `game_id` itself contains no `:`, the registry rejects that at bind time). Hub-mode `app_chat` intercepts it after the send rate limiter (cross-plane sends still consume the send budget) and orchestrates three steps (`PlayerDirectory::RelayGameReply`):

1. `ChatPeerHub::service_id_for_game(game_id)` reverse-looks-up the online spoke registered for that `game_id`;
2. `IdentityRegistry::ResolveGameUser(game_id, player_id)` reverse-looks-up the sender's game identity (when a player has multiple bindings in the same game, the lexicographically smallest `game_user_id` is taken, for determinism);
3. assembles `PEER_INJECT_MESSAGE_NOTIFY` (with a **bare** channel ID, `sender_id` = `game_user_id`, and `client_msg_id` passed through) and sends it down to that spoke over the hub. The spoke consumes it as a normal injection: the message id is minted by the server, going through the same storage/delivery tail as injected private chats (online push, offline queue).

Return codes: `OK` means handed over to the game plane (the game side mints its own `message_id`, not present in the response); `SERVER_UNAVAILABLE` means that game has no online spoke or the downstream failed; `INVALID_PARAM` means the sender has no identity binding in that game. Refusals always use an explicit code; it never degrades into a silently-delivered local `<game_id>:<bare>` channel——the client must not mistake an undelivered reply for success.

Conventions and boundaries: an App-side channel ID containing `:` is reserved for the game plane, so deployments must not name their own App channels with colons; unprefixed sends still go to local channels with unchanged behavior. No loopback: when an injected message fans back to the App player over the spoke it is an **unprefixed private-chat copy** (see "Fan-in delivery") and does not re-enter this path. Mention handling is skipped (content passes through unchanged to the game side). Non-hub mode (hub not enabled) has no spoke to resolve, so the whole interception does not apply.

## Game online state and friend DM reaching the game (2026-09-27)

Two things of the same origin: whether friends can see "this person is playing《X》", and whether friend DMs can land inside the game. Both are governed by the same switch and both build on WP-8's **identity binding**——the binding is an "online assertion": the game backend calls `BIND_PLAYER_IDENTITY` when entering the game and `UNBIND` on exit, so the state comes online and goes offline with the lifecycle of the assertion; no separate heartbeat or TTL needs maintaining.

Lives in `app_chat`'s `PlayerDirectory` (`game_presence.{h,cc}` stores the switch; `player_directory.cc` derives the roster/events), with four new message ids in the same block as WP-8 (5031-5034) and the same `SERVER_AUTH_REQ` trust gate; the player side reaches it via app_gateway forwarding with `player_id` pinned to the login identity:

- `SET_GAME_PRESENCE_ENABLED_REQ` (5031)——writes the player's own switch. Empty `player_id` → `INVALID_PARAM` (the forwarding path always fills it; only a direct connection can leak one). The roster is recomputed only when the value changes; rewriting the same value is a silent no-op.
- `GET_GAME_PRESENCE_REQ` (5033)——returns `enabled` + `entries` (each with `game_id`/`game_user_id`), sorted by `game_id`. **In the disabled state `entries` is always empty**——not a single binding is missing, it is just no longer fanned out.

### Default value semantics (binding implies default enabled)

`game_presence_enabled` is **true when there is no row**: `GamePresence` stores only explicit choices (`chirp:game_presence:setting:<player_id>` = `"1"`/`"0"`, enabled via `--game_presence_redis_host`/`--game_presence_redis_port`, off by default = pure in-memory), and a player who never set anything reads as enabled. The product consequence is: if the game backend does nothing, the moment binding completes friends can see the online state and private chat can reach the game; to turn it off the player must explicitly turn it off once. Writing `true` to a player who never wrote anything still counts as "an explicit choice recorded" (the effective value did not change, but a row was written), so the return code is `OK` and no extra event is produced——recomputation is idempotent.

### Derived roster and events (state is not pushed)

`PlayerDirectory::RefreshPresence(player_id)` recomputes that player's desired set at three points: on binding, on unbinding, and on a switch flip: `enabled ? the set of game_ids in the bindings (sorted, deduplicated) : the empty set`. It diffs against the previously published set and, only for **flipped** games, emits one `GamePresenceEvent{player_id, game_id, online}` to the Redis pub/sub channel `chirp:game_presence:events`; the derived key `chirp:game_presence:online:<player_id>` = the newline-joined set of game_ids, and is `DEL`ed when the set is empty (not written as an empty string).

So **the disabled state is completely silent**: neither the roster key is written nor any event emitted——`chirp_social` receives no flip for that player, and the friend view naturally falls back to the basic online state. Likewise, a new binding during a game-disabled period produces no event (bound but not fanned out). Events are best-effort: a publish failure only logs a warning; the binding/switch writes still succeed——Redis is a transport here, not the authority.

On the `chirp_social` side (`services/social/src/main.cc`), a `RedisSubscriber` subscribes to that channel (note that `SUBSCRIBE` must be attached inside the connect callback; the socket is not open yet before `Start()`), merging events into a `game_presence` override table: `EffectivePresence` = a merged view of the base state (connection lifecycle + `SET_PRESENCE`) and the game assertions, and **a non-empty override table is always `IN_GAME`** (the more specific fact), with game ids going into `status_message` (comma-joined, set order) and `metadata[<game_id>]="1"` (machine-readable). The override table and the base state have **deliberately different** lifecycles: only the base state flips when the social client disconnects, so a player still in-game remains `IN_GAME`——exactly what this feature needs to expose to friends; `SET_PRESENCE AWAY` also cannot override a game assertion. Without Redis configured, the override table is always empty and `GET_PRESENCE` degrades to the base state.

### Friend DM delivery reaching the game

`PlayerDirectory::RelayFriendMessage(sender_player_id, recipient_player_id, content, client_msg_id, resolver, inject)`: hub-mode `app_chat` calls it once for the **recipient** in the private chat delivery tail (after online push/offline queue), mirroring the same message into the game plane. Rules:

1. the recipient's switch is off, or the recipient has no bindings at all → return 0 directly, producing no inject;
2. each of the recipient's bindings corresponds to one copy, ordered by `game_id` (determinism);
3. if `service_id_for_game(game_id)` cannot resolve an online spoke → **skip and continue**, not an error: the normal end delivery already succeeded, and the game side's absence is not a failure;
4. if the inject returns false (spoke downstream failure), likewise skip only, do not count;
5. blocked pairs and NPC recipients do not enter this path.

The field semantics of `PEER_INJECT_MESSAGE_NOTIFY` differ from cross-plane replies, which must be remembered: **the spoke treats `channel_id` as the target user** (the game side consumes it as a player private chat), so `channel_id` = the recipient's `game_user_id`, while `sender_id` is the **sender's chirp `user_id`**——what the game client receives is "platform friend xxx brought you a message", not an in-game identity. This is deliberate: the recipient recognizes themselves in the game, and the sender's platform identity is exactly the edge the App side needs to cross.

Boundaries: relay is effective only in hub mode (without a spoke registry there are no games to resolve); delivery is a best-effort mirror that does not use the explicit return-code semantics of `RelayGameReply`, and the App client still sees a normal private chat return code. The switch and bindings are both **registry-local in-memory authoritative** + their own Redis mirrors: in a multi-node `app_chat` deployment, only the instance that handled that binding/switch write recomputes and broadcasts the event, and other instances' `published_games_` views do not sync automatically (same boundary as the existing local notify).

Test anchors: `chat_player_directory_tests` (switch default semantics, write-through/replay, roster and event flow, disabled-state silence, relay fan-out/skip/count-no-failure/sender identity), `social_tests` (override table merge, assertion release fallback, still in-game after logout, `GET_PRESENCE` merged view, malformed/wrong-channel events), `app_sdk_gateway_tests` (5031/5033 forwarding + `player_id` pinning).

## Roadmap

1. ~~chat connects in as an internal peer and consumes `InjectMessageNotify`~~——done (loop verified end to end); the process-level E2E smoke is left as a later option.
2. ~~provide a Redis Streams fallback broker for integrators that cannot host long-lived connection clients (ack + replay, no raw pub/sub)~~——done, upstream injections only (see "Broker fallback" above); downstream events still use the long-connection face.
3. ~~chat-side event production~~——NPC dialogue partially done (`npc.player_message`, consumed by `services/game/npc_dialog`, see above); word-filter punishment and trade status transitions remain open.
