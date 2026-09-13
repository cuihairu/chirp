# Server Plane: Game Backend Integration

Status: **Experimental** — the hub (`chirp_server_gateway`), the chat-side consumer, and the Redis Streams broker fallback are implemented and unit-verified at 100% line coverage: chat dials in as an internal peer and injection messages flow through the same storage/delivery tail as player-sent messages. See [Architecture](./architecture.md) for the three-edge topology decision.

## What it is

The server plane is how a game backend talks to chirp. It is deliberately separate from the player edges:

- **Dial-out**: the game server opens the connection to `chirp_server_gateway`. Chirp never needs to reach into game networks, and game servers in private subnets need no public callback endpoint.
- **Service identity, not user identity**: peers authenticate with `service_id` + shared secret. They are never user accounts, never appear in session/kick/presence, and injected messages carry non-user sender kinds (`SYSTEM` / `NPC` / `SERVICE`).
- **Same framing**: TCP + `[uint32_be size][chirp.gateway.Packet]`, with the `5xxx` msg-id block.

### Credential boundary

The `service_id` + secret pair is an appkey/appSecret-style credential: it identifies the integrating backend and is long-lived. Two rules follow:

- **Never ship it in a client.** Anything bundled into a game client or app binary is effectively public. Clients hold short-lived user tokens instead; the game backend exchanges/derives those after the player's own login. See [Credential model](./architecture.md#credential-model-service-credentials-vs-user-tokens).
- **Never use it to impersonate a user.** Injections carry `sender_kind` (`SYSTEM` / `NPC` / `SERVICE`) precisely so the server plane can act without pretending to be a player account.

## Running

```bash
cmake --preset dev && cmake --build --preset dev
./build/services/server_gateway/chirp_server_gateway \
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
| `--port` | 8100 | TCP listener for service connections |
| `--service` | (none) | Repeatable `service_id=secret` credential entry |
| `--chat_service_id` | `chat` | The service that receives message injections |
| `--heartbeat_interval` | 30 | Assigned keepalive cadence (seconds) |
| `--auth_timeout` | 10 | Seconds allowed to authenticate before disconnect |
| `--max_pending` | 1000 | Per-service bound on queued (unacked) events |

With no `--service` entries the hub starts but rejects every login (fail closed).

## Connection lifecycle

1. Game server dials TCP and must send `SERVER_AUTH_REQ` within `--auth_timeout` seconds, or the connection is closed.
2. `SERVER_AUTH_RESP` returns the result, the server time, and the assigned `heartbeat_interval_seconds`. The connection must show traffic at least every `2 × heartbeat_interval` seconds or it is closed.
3. Logging in again with the same `service_id` from another connection displaces the older one; the displaced connection is closed.

## Uplink: message injection

A trusted service asks chirp to deliver a message whose sender is not a user (announcement, NPC line, trade state). `INJECT_MESSAGE_REQ` carries `chirp.server_gateway.MessageInjectRequest`:

- `inject_id`: caller-supplied idempotency key (echoed in the response)
- `sender_kind`: `SENDER_SYSTEM` / `SENDER_NPC` / `SENDER_SERVICE`
- `sender_id`: e.g. `npc:blacksmith_01`, `trade`
- `channel_type` + `channel_id`, or `receiver_id` for 1:1
- `content`

Response codes:

| Code | Meaning |
| --- | --- |
| `OK` | Validated and forwarded to the chat service (`InjectMessageNotify`) |
| `INVALID_PARAM` | Empty content / `SENDER_UNKNOWN` / empty `sender_id` / no channel or receiver |
| `SERVER_UNAVAILABLE` | Chat service not connected, or the write failed |

`OK` still means "accepted by the plane": the chat side consumes the injection asynchronously, so the response does not confirm delivery to players.

### Chat-side consumption

The chat service connects to the hub as an internal peer (`--server_gateway_host`, default disabled when empty) and answers auth + heartbeats. A forwarded `InjectMessageNotify` follows the same tail as `SEND_MESSAGE`:

- Stored to history first; no membership checks and no mention cooldowns (the sender is not a user).
- `PRIVATE` with an online receiver is delivered immediately, otherwise queued for the offline user.
- Non-private channels (`TEAM` / `GUILD` / `WORLD`) broadcast to members and queue the message for offline members.
- A malformed `InjectMessageNotify` is logged and skipped; the connection stays up.

### Broker fallback (upstream over Redis Streams)

For game backends that cannot host a long-connection client, the same injection path is also available over a Redis Stream. Start the hub with `--broker_redis_host` (empty, the default, disables the consumer):

| Flag | Default | Meaning |
| --- | --- | --- |
| `--broker_redis_host` | (empty) | Redis host; empty disables the broker |
| `--broker_redis_port` | 6379 | Redis port |
| `--broker_stream` | `chirp:server_plane:inject` | Stream to consume |
| `--broker_group` | `chirp-plane` | Consumer group (created idempotently) |
| `--broker_consumer` | `<hostname>:<pid>` | Consumer name inside the group |
| `--broker_claim_min_idle_ms` | 30000 | `XAUTOCLAIM` min-idle-time for redelivery |

Requires Redis >= 6.2 (`XAUTOCLAIM`). A producer writes one entry per message with flat string fields — any language that can `XADD` can integrate:

| Field | Required | Meaning |
| --- | --- | --- |
| `service_id` | yes | Service identity (must exist in `--service`) |
| `secret` | yes | Shared secret, same credentials as the connection plane |
| `sender_kind` | yes | `SYSTEM` / `NPC` / `SERVICE` (`SENDER_` prefix tolerated) |
| `channel_type` | yes | `PRIVATE` / `TEAM` / `GUILD` / `WORLD`, or `0`–`3` |
| `sender_id` | yes* | Validated downstream like the proto path |
| `channel_id` / `receiver_id` | — | Channel target, or receiver for 1:1 |
| `content` | yes* | Message body |
| `inject_id` | no | Idempotency key; `<consumer>-<seq>` is generated when absent |
| `reply_to` | no | Stream name to receive the `{inject_id, code}` result entry |

Semantics:

- The hub runs a consumer group (`XREADGROUP ... BLOCK`) and turns each entry into the same `HandleInject` path as the long-connection plane, with identical validation and response codes.
- `OK` / `INVALID_PARAM` / `AUTH_FAILED` are **acked immediately** (`XACK`): malformed or rejected entries are poison and must not replay.
- `SERVER_UNAVAILABLE` (chat service offline) is **not acked**: the entry stays in the pending entries list and is redelivered by a periodic `XAUTOCLAIM` sweep until chat is back. Redelivery is unbounded by design — poison is bounded out by the immediate-ack rule above.
- With `reply_to`, the result (`inject_id` + `ErrorCode` name, e.g. `OK`) is written back only for **acked terminal outcomes**, so a replayed entry answers exactly once.
- Transport failures between commands drop the connection and reconnect; unacked entries replay. At-least-once overall.

## Downlink: events (at-least-once)

`EVENT_PUBLISH_REQ` (`chirp.server_gateway.EventPublishRequest`) publishes an event that must reach a target service — e.g. a quest trigger produced by chat-side logic:

- `event_id`: optional caller-supplied idempotency key; generated (`evt-<ts>-<n>`) when omitted and echoed in the response
- `target_service_id`, `event_type`, `payload`

Delivery semantics:

- Target **online** → delivered immediately as `EVENT_DELIVER_NOTIFY` (`queued=false`).
- Target **offline** → queued per service (`queued=true`) and delivered on its next login/reconnect.
- Events stay queued until `EVENT_ACK_REQ` acknowledges them. Reconnects redeliver everything unacknowledged; `attempt` increments per delivery.
- A full queue (over `--max_pending`) rejects the publish with `SERVER_UNAVAILABLE` instead of silently dropping older events. Publishers retry with backoff.
- Acks are idempotent; unknown ids answer `OK`.
- A displaced connection closing late does not reset the live connection's in-flight tracking (no duplicate redelivery storm).

## NPC dialog service

`services/npc_dialog` is the first event consumer on the plane: a pure server-plane client (no player-facing listener) that closes the NPC conversation loop end to end.

- **Uplink (chat → hub → npc_dialog).** Chat rewrites a player's private message to an `npc:`-prefixed receiver into an `EventPublishRequest` of type `npc.player_message` (payload: `chirp.chat.NpcPlayerUtterance`; `event_id` = chat message id) and fire-and-forgets it — the player's `OK` means accepted, not that a reply will come. The chat history keeps the player's original line.
- **Reply (npc_dialog → hub → chat).** The responder renders a reply with a keyword rule table (`npc_id<TAB>keyword<TAB>reply` TSV, `*` = the NPC's fallback line, ASCII case-insensitive substring, first match wins; built-in demo rules when no `--rules_file`) and injects it as `SENDER_NPC` on a private channel to the player, with the event id as the `inject_id` idempotency key.
- **Ack policy.** Foreign event types, unparseable payloads, and utterances missing sender/NPC identity are acked immediately (poison-pill: they can never become valid by retrying). A valid event is acked **only after** the hub accepted its reply injection (`OK`); any other outcome stays unacked, so the hub redelivers — at-least-once, and a redelivery inside the retry window can produce a duplicate reply. The MVP accepts this; dedupe by `inject_id`/event id is the later fix.

Process-level verification: `./test_services.sh --smoke-npc` runs hub + chat + npc_dialog as real processes and checks the keyword reply, the fallback reply, history (player line + reply), and the offline-queue refill path.

## Roadmap

1. ~~Chat service connects as an internal peer and consumes `InjectMessageNotify`~~ — done (loopback-verified end to end); a process-level E2E smoke is still an option for later.
2. ~~Redis Streams fallback broker for integrations that cannot host a long-connection client (ack + replay, no raw pub/sub)~~ — done, upstream injection only (see "Broker fallback" above); downlink events still use the long-connection plane.
3. ~~Event production on the chat side~~ — done for NPC dialogue (`npc.player_message`, consumed by `services/npc_dialog`, see above); sensitive-word penalties and trade state transitions remain open.
