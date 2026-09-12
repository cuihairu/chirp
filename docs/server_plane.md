# Server Plane: Game Backend Integration

Status: **Experimental** — the hub (`chirp_server_gateway`) and the chat-side consumer are implemented and unit-verified at 100% line coverage: chat dials in as an internal peer and injection messages flow through the same storage/delivery tail as player-sent messages. See [Architecture](./architecture.md) for the three-edge topology decision.

## What it is

The server plane is how a game backend talks to chirp. It is deliberately separate from the player edges:

- **Dial-out**: the game server opens the connection to `chirp_server_gateway`. Chirp never needs to reach into game networks, and game servers in private subnets need no public callback endpoint.
- **Service identity, not user identity**: peers authenticate with `service_id` + shared secret. They are never user accounts, never appear in session/kick/presence, and injected messages carry non-user sender kinds (`SYSTEM` / `NPC` / `SERVICE`).
- **Same framing**: TCP + `[uint32_be size][chirp.gateway.Packet]`, with the `5xxx` msg-id block.

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

## Roadmap

1. ~~Chat service connects as an internal peer and consumes `InjectMessageNotify`~~ — done (loopback-verified end to end); a process-level E2E smoke is still an option for later.
2. Redis Streams fallback broker for integrations that cannot host a long-connection client (ack + replay, no raw pub/sub).
3. Event production on the chat side: NPC quest triggers, sensitive-word penalties, trade state transitions.
