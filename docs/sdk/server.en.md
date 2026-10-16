> 🌐 Language: [中文](./server.md) | **English** (this page)

# Server SDK (game backend integration)

A game backend (trade, matchmaking, NPC engines, etc.) does not act as a player; it joins Chirp's server plane **as a service**: it dials out a persistent connection (dial-out, so the game server exposes no port), passes the trust gate with  + a shared secret, and then injects non-player messages (SYSTEM/NPC/SERVICE) into chat and publishes reliable events to other services.

The wire contract is in , and the full integration semantics (credentials, injection, events, idempotency keys) are in [Server Plane](/server_plane).

## Connection topology (important)

```text
game backend ──dial-out──▶ chirp_server_gateway (TCP 8100)     injection/events/ack
game backend ──dial-out──▶ chirp_chat main port (TCP 7000)     player identity binding/channel subscription/unread ledger
```

Player identity binding, channel subscriptions and the unread ledger (5013-5030) have moved out of  into 's app_chat instance (PlayerDirectory). These two targets must be dialed with **two separate Client instances**; sending an RPC to an id that has moved away times out because the peer no longer handles it.

## Go (official SDK)

 is the officially maintained Go reference client, with semantics aligned item by item with the C++ reference implementation ; CI () backs it with 33 in-process fake-hub loopback unit tests + a proto/go regeneration drift check +  + ; client.go has 100% statement coverage (after batch 3026896, the 2 arms that cannot be triggered naturally are covered by injection/race cases).

Capability surface:

- **Auth handshake**: the first frame is ; on rejection it retries after a fixed delay.
- **Heartbeat**: the hub assigns the cadence in the auth response (silence beyond 2x interval gets the connection closed); if unassigned it falls back to  (default 30s).
- **RPCs**:  (inject a message,  idempotency key),  (reliable event, queued while offline),  (batch acknowledgment, at-least-once), plus the three groups of player identity binding / channel subscription / unread ledger methods ( /  idempotency keys).
- **Push handlers**:  /  / , fired on the internal read goroutine, and **must not block**.

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

**No official SDK yet**; integrate directly against : use Node's  module to open a persistent TCP connection and implement the three layers yourself — frame codec (), protobuf ( loading ), and the state machine (first frame , heartbeat, sequence correlation, reconnect on disconnect, event ack).

Key points:

- Event delivery is at-least-once: you must reply  after handling, otherwise the hub redelivers on reconnect;
- Injection messages use the  idempotency key to prevent duplicates; after an auth rejection just retry at a fixed delay (same as the C++/Go reference implementations, with no backoff cap);
- There is no process-level E2E on the Node side; the same wire contract is covered by the C++ path of .

## Lua

**No official SDK yet.** Common game server frameworks such as skynet integrate over the protocol, with the same key points as Node.js: persistent TCP connection +  first frame + frame codec + heartbeat + ack. On the Lua side,  /  can handle .

## Python

**No official SDK yet.** Integrate over the protocol using the standard-library  + , with the same key points as above. Suitable for integration points that are not throughput-sensitive, such as tooling, operations backends and GM scripts; for high-frequency injection prefer the Go SDK or the C++ reference implementation.

## Protocol / API reference

- Server plane contract (credential model, injection semantics, events, cross-plane reply): [Server Plane](server_plane.en)
- Full RPC tables for player identity binding / channel subscriptions / unified unread: the corresponding sections of 
-  (in-repo) defines all request/response messages
- NPC dialog (an example consumer of the injection channel): [NPC Dialog System](design-notes/npc_dialog_system.en)

## Current state and boundaries

- The hub plane is a **pure TCP persistent connection**, with no WS/TLS transport;
- Events only trigger the disconnect handler when an authenticated connection drops; wrong credentials show up as periodic warning logs (fixed-delay infinite retries);
- An injected  only means "the server plane has accepted it", not that delivery to the player side is confirmed;
- There is no process-level E2E from the Go SDK against a real ; the same wire contract is covered by  on the C++ path;
- Nightly builds: the Actions nightly  artifact bundle includes the Go SDK source package (unsigned); see [SDK Overview](/sdk/) for the entry point.
