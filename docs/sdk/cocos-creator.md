> 🌐 Language: [中文](../zh/sdk/cocos-creator.md)) | **English** (this page)

# Cocos Creator

**There is no official SDK for Cocos Creator yet.** Chirp's integration surface is a pure protocol (TCP/WebSocket + Protobuf), independent of any engine, so a Cocos Creator (TypeScript) project can simply connect over the protocol — the `sdks/ts` protocol stack in this repo is a runnable TS reference implementation.

## How to integrate

Cocos Creator 3.x's `WebSocket` can connect straight to Chirp's WS edge (development topology chat **WS 7001**; the TCP 7000 frame protocol is identical to the WS message body, so browser/mini-game environments can just use WS):

```ts
const ws = new WebSocket("ws://127.0.0.1:7001");
ws.binaryType = "arraybuffer";
```

Three layers you need to implement yourself (not much work, the protocol is small binary frames):

1. **Frame codec**: each frame is `[uint32_be payload_size][Packet protobuf bytes]`, and `Packet` has the four fields `msg_id` / `sequence` / `body` and the optional `request_id`;
2. **protobuf**: use [protobuf.js](https://github.com/protobufjs/protobuf.js) (or google-protobuf) to load the compiled output of `proto/gateway.proto`;
3. **Client state machine**: login request-response correlation, heartbeat, reconnect on disconnect, KICK terminal-state handling.

**Just copy an existing implementation**: `sdks/ts/src/` in this repo contains a complete TS protocol stack that can be moved wholesale into a Cocos project (for the WeChat Mini Game target, use `createWxSocketFactory(wx)` from `@chirp/protocol/adapters/wx_socket` as the transport factory) —

| File | Responsibility |
| --- | --- |
| `frame.ts` | Frame codec (u32 length prefix + Packet) |
| `msg_map.ts` | Message ID <-> request/response type mapping table |
| `chirp_client.ts` | Client state machine (connect/heartbeat/reconnect/sequence correlation) |

That implementation is backed by 149 vitest unit tests (`*.test.ts`, line coverage gated by CI thresholds (`vitest.config.ts`: lines/branches/functions/statements ≥90%)) and its semantics are aligned item by item with the unity client.

## Lifecycle

The same as every client:

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

- The only officially maintained SDKs are Unity (C#), Unreal (C++ shell), Go (server) and the C++ core; Cocos/LayaAir/WeChat Mini Game integrate over the protocol, and the TS layer can simply reuse the `sdks/ts` protocol stack.
- Social (WS 8001), voice (WS 9001) and party (WS 7501) are separate WS services, each on its own connection; add them as needed — the main chat path only needs the one chat connection.
