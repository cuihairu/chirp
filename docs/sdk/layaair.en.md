> 🌐 Language: [中文](./layaair.md) | **English** (this page)

# LayaAir

**There is no official SDK for LayaAir yet.** The integration path is exactly isomorphic to Cocos Creator: a TS/JS project + the engine's native  connecting directly to Chirp's WS edge. Chirp's integration surface is a pure protocol (TCP/WebSocket + Protobuf), independent of any engine.

## How to integrate

LayaAir 3.x (a TS project) can use the browser-standard  directly (either LayaAir's  or the native global):

```ts
const ws = new WebSocket("ws://127.0.0.1:7001");   // chat WS edge (development topology)
ws.binaryType = "arraybuffer";
```

Three layers you need to implement yourself:

1. **Frame codec**: each frame is , and  has  /  / ;
2. **protobuf**: use [protobuf.js](https://github.com/protobufjs/protobuf.js) to load the compiled output of ;
3. **Client state machine**: the login round trip, heartbeat, reconnect, KICK terminal state.

**Reference implementation**:  in this repo is a complete TS protocol stack with 131 vitest unit tests ( frame codec,  message mapping table,  state machine, 99.8% line coverage) that can be moved directly into a LayaAir project, with semantics aligned with the unity client; for the WeChat Mini Game target, use  from  as the ChirpClient transport factory.

## Lifecycle

```text
connect → LOGIN(1003/1004) → send/receive (2001 send / 2005 push) → heartbeat (1001/1002)
        ↓ disconnect              ↓ KICK received (1005)
      auto reconnect + re-login   kick terminal state: stop reconnecting, the player must log in again
```

-  increments, and responses are correlated back to requests by sequence;
- after receiving an offline message you must send an ack, otherwise the server redelivers it;
- being replaced (KICK) is a terminal state, so do not auto-reconnect after it.

## Protocol / API reference

- Protocol overview: [API Overview](api/overview.en) (Packet frames, message ID ranges, core flows) — read this first before integrating
- Full message ID table: the  enum in  (1xxx auth/session, 2xxx chat, 3xxx social, 4xxx voice, 7xxx party)
- TS reference implementation: [sdks/ts](https://github.com/cuihairu/chirp/tree/main/sdks/ts)
- C# semantics comparison (message-plane coverage table): [Unity3D](sdk/unity3d.en)
- Server integration (game backend): [Server SDK](sdk/server.en) and [Server Plane](server_plane.en)

## Current state and boundaries

- Same as Cocos Creator: the only official SDKs are Unity/Unreal/Go/the C++ core; LayaAir integrates over the protocol, with the TS layer reusing the  protocol stack.
- The main chat path needs only the one chat connection; social (WS 8001), voice (WS 9001) and party (WS 7501) are separate services to add as needed.
