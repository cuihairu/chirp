> 🌐 Language: [中文](../zh/sdk/layaair.md)) | **English** (this page)

# LayaAir

**There is no official SDK for LayaAir yet.** The integration path is exactly isomorphic to Cocos Creator: a TS/JS project + the engine's native `WebSocket` connecting directly to Chirp's WS edge. Chirp's integration surface is a pure protocol (TCP/WebSocket + Protobuf), independent of any engine.

## How to integrate

LayaAir 3.x (a TS project) can use the browser-standard `WebSocket` directly (either LayaAir's `Laya.Browser.window.WebSocket` or the native global):

```ts
const ws = new WebSocket("ws://127.0.0.1:7001");   // chat WS edge (development topology)
ws.binaryType = "arraybuffer";
```

Three layers you need to implement yourself:

1. **Frame codec**: each frame is `[uint32_be payload_size][Packet protobuf bytes]`, and `Packet` has `msg_id` / `sequence` / `body` (optional `request_id` for cross-plane tracing);
2. **protobuf**: use [protobuf.js](https://github.com/protobufjs/protobuf.js) to load the compiled output of `proto/gateway.proto`;
3. **Client state machine**: the login round trip, heartbeat, reconnect, KICK terminal state.

**Reference implementation**: `sdks/ts/src/` in this repo is a complete TS protocol stack with 149 vitest unit tests (`frame.ts` frame codec, `msg_map.ts` message mapping table, `chirp_client.ts` state machine, line coverage gated by CI thresholds (`vitest.config.ts`: lines/branches/functions/statements ≥90%)) that can be moved directly into a LayaAir project, with semantics aligned with the unity client; for the WeChat Mini Game target, use `createWxSocketFactory(wx)` from `@chirp/protocol/adapters/wx_socket` as the ChirpClient transport factory.

## Lifecycle

```text
connect → LOGIN(1003/1004) → send/receive (2001 send / 2005 push) → heartbeat (1001/1002)
        ↓ disconnect              ↓ KICK received (1005)
      auto reconnect + re-login   kick terminal state: stop reconnecting, the player must log in again
```

- `sequence` increments, and responses are correlated back to requests by sequence;
- after receiving an offline message you must send an ack, otherwise the server redelivers it;
- being replaced (KICK) is a terminal state, so do not auto-reconnect after it.

## Protocol / API reference

- Protocol overview: [API Overview](/api/overview) (Packet frames, message ID ranges, core flows) — read this first before integrating
- Full message ID table: the `MsgID` enum in `proto/gateway.proto` (1xxx auth/session, 2xxx chat, 3xxx social, 4xxx voice, 7xxx party)
- TS reference implementation: [sdks/ts](https://github.com/cuihairu/chirp/tree/main/sdks/ts)
- C# semantics comparison (message-plane coverage table): [Unity3D](/sdk/unity3d)
- Server integration (game backend): [Server SDK](/sdk/server) and [Server Plane](/server_plane)

## Current state and boundaries

- Same as Cocos Creator: the only official SDKs are Unity/Unreal/Go/the C++ core; LayaAir integrates over the protocol, with the TS layer reusing the `sdks/ts` protocol stack.
- The main chat path needs only the one chat connection; social (WS 8001), voice (WS 9001) and party (WS 7501) are separate services to add as needed.
