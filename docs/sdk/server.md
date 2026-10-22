> 🌐 Language: [中文](../zh/sdk/server.md)) | **English** (this page)

# Server SDK (game backend integration)

A game backend (trade, matchmaking, NPC engines, etc.) does not act as a player; it joins Chirp's server plane **as a service**: it dials out a persistent connection (dial-out, so the game server exposes no port), passes the trust gate with `service_id` + a shared secret, and then injects non-player messages (SYSTEM/NPC/SERVICE) into chat and publishes reliable events to other services.

The wire contract is in `proto/game_server_gateway.proto`, and the full integration semantics (credentials, injection, events, idempotency keys) are in [Server Plane](/server_plane).

## Connection topology (important)

```text
game backend ──dial-out──▶ chirp_server_gateway (TCP 8100)     injection/events/ack
game backend ──dial-out──▶ chirp_chat main port (TCP 7000)     player identity binding/channel subscription/unread ledger
```

Player identity binding, channel subscriptions and the unread ledger (5013-5030) have moved out of `chirp_server_gateway` into `chirp_chat`'s app_chat instance (PlayerDirectory). These two targets must be dialed with **two separate Client instances**; sending an RPC to an id that has moved away times out because the peer no longer handles it.

## Go (official SDK)

`sdks/go` is the officially maintained Go reference client, with semantics aligned item by item with the C++ reference implementation `libs/network/server_gateway_peer.cc`; CI (`go-sdk.yml`) backs it with 33 in-process fake-hub loopback unit tests + a proto/go regeneration drift check + `go vet` + `-race`; client.go has 100% statement coverage (after batch 3026896, the 2 arms that cannot be triggered naturally are covered by injection/race cases).

Capability surface:

- **Auth handshake**: the first frame is `SERVER_AUTH_REQ`; on rejection it retries after a fixed delay.
- **Heartbeat**: the hub assigns the cadence in the auth response (silence beyond 2x interval gets the connection closed); if unassigned it falls back to `Config.HeartbeatInterval` (default 30s).
- **RPCs**: `InjectMessage` (inject a message, `inject_id` idempotency key), `PublishEvent` (reliable event, queued while offline), `AckEvents` (batch acknowledgment, at-least-once), plus the three groups of player identity binding / channel subscription / unread ledger methods (`binding_id` / `subscription_id` idempotency keys).
- **Push handlers**: `SetInjectHandler` / `SetEventHandler` / `SetDisconnectHandler` / `SetDevicePresenceHandler` (device presence 1020), fired on the internal read goroutine, and **must not block**.

```go
c := chirp.NewClient(chirp.Config{
    Host: "127.0.0.1", Port: 8100,
    ServiceID: "trade-service", Secret: "<shared secret>",
})
c.SetEventHandler(func(n *pbsg.EventDeliverNotify) {
    c.AckEvents(context.Background(), n.GetEventId())  // ack after handling, otherwise the hub redelivers
})
c.Start()
defer c.Stop()

c.InjectMessage(ctx, &pbsg.MessageInjectRequest{
    InjectId:   "raid-5678",                    // caller-supplied idempotency key
    SenderKind: pbsg.SenderKind_SENDER_SERVICE,
    SenderId:   "raid-boss",
    ChannelType: 3,                             // WORLD
    ChannelId:  "world-boss",
    Content:    []byte("world boss has spawned"),
})
```

Details and complete examples: [sdks/go/README.md](https://github.com/cuihairu/chirp/tree/main/sdks/go).

## Node.js

**No official SDK yet**; integrate directly against `game_server_gateway.proto`: use Node's `net` module to open a persistent TCP connection and implement the three layers yourself — frame codec (`[uint32_be][Packet]`), protobuf (`protobufjs` loading `proto/game_server_gateway.proto`), and the state machine (first frame `SERVER_AUTH_REQ`, heartbeat, sequence correlation, reconnect on disconnect, event ack).

Key points:

- Event delivery is at-least-once: you must reply `AckEvents` after handling, otherwise the hub redelivers on reconnect;
- Injection messages use the `inject_id` idempotency key to prevent duplicates; after an auth rejection just retry at a fixed delay (same as the C++/Go reference implementations, with no backoff cap);
- There is no process-level E2E on the Node side; the same wire contract is covered by the C++ path of `./test_services.sh --smoke-npc`.

## Lua

**No official SDK yet.** Common game server frameworks such as skynet integrate over the protocol, with the same key points as Node.js: persistent TCP connection + `SERVER_AUTH_REQ` first frame + frame codec + heartbeat + ack. On the Lua side, `lua-protobuf` / `pb` can handle `game_server_gateway.proto`.

## Python

**No official SDK yet.** Integrate over the protocol using the standard-library `socket` + `google.protobuf`, with the same key points as above. Suitable for integration points that are not throughput-sensitive, such as tooling, operations backends and GM scripts; for high-frequency injection prefer the Go SDK or the C++ reference implementation.

## Protocol / API reference

- Server plane contract (credential model, injection semantics, events, cross-plane reply): [Server Plane](/server_plane)
- Full RPC tables for player identity binding / channel subscriptions / unified unread: the corresponding sections of `docs/server_plane.md`
- `proto/game_server_gateway.proto` (in-repo) defines all request/response messages
- NPC dialog (an example consumer of the injection channel): [NPC Dialog System](/design-notes/npc_dialog_system)

## Current state and boundaries

- The hub plane is a **pure TCP persistent connection**, with no WS/TLS transport;
- Events only trigger the disconnect handler when an authenticated connection drops; wrong credentials show up as periodic warning logs (fixed-delay infinite retries);
- An injected `OK` only means "the server plane has accepted it", not that delivery to the player side is confirmed;
- There is no process-level E2E from the Go SDK against a real `chirp_server_gateway`; the same wire contract is covered by `--smoke-npc` on the C++ path;
- Nightly builds: the Actions nightly `daily-build` artifact bundle includes the Go SDK source package (unsigned); see [SDK Overview](/sdk/) for the entry point.
