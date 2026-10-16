> 🌐 Language: [中文](./wechat-minigame.md) | **English** (this page)

# WeChat Mini Game

**There is no official SDK for WeChat Mini Game yet.** The mini runtime has no TCP, only `wx.connectSocket` — which actually simplifies the choice: **it must and can only use Chirp's WebSocket edge**, with a protocol identical to every other client. Chirp's integration surface is a pure protocol (frames + Protobuf), independent of the host environment.

## How to integrate

```ts
const task = wx.connectSocket({ url: "wss://your-domain/chirp-ws" });
task.onOpen(() => { /* send LOGIN */ });
task.onMessage((res) => { /* res.data is an ArrayBuffer: decode the frame */ });
```

Three layers you need to implement yourself (exactly the same as Cocos/LayaAir):

1. **Frame codec**: each frame is `[uint32_be payload_size][Packet protobuf bytes]`, and `Packet` has `msg_id` / `sequence` / `body`;
2. **protobuf**: [protobuf.js](https://github.com/protobufjs/protobuf.js) can run in the mini-game environment (load the compiled output of `proto/gateway.proto`; note that you should bundle it as json/commonjs to avoid dynamic-loading restrictions);
3. **Client state machine**: the login round trip, heartbeat, reconnect, KICK terminal state.

**Reference implementation**: `sdks/ts/src/` in this repo is a TS protocol stack with 131 vitest unit tests (`frame.ts` / `msg_map.ts` / `chirp_client.ts`, 99.8% line coverage) that does not depend on the DOM and can be moved directly into a mini-game project. The transport adapter is already written: `sdks/ts/src/adapters/wx_socket.ts` bridges `wx.connectSocket`'s SocketTask (callback registration + a `{ data }` envelope object) to the `WebSocketLike` interface ChirpClient expects:

```ts
import { ChirpClient } from '@chirp/protocol/chirp_client';
import { createWxSocketFactory } from '@chirp/protocol/adapters/wx_socket';

const client = new ChirpClient({
  url: 'wss://chat.example.com/ws',
  wsFactory: createWxSocketFactory(wx), // wx: the mini-game runtime global
});
```

Binary frames pass through in both directions as ArrayBuffers; the adapter has fake-socket unit tests plus one real ChirpClient login round-trip test (`adapters/wx_socket.test.ts`).

## Lifecycle

```text
connectSocket → LOGIN(1003/1004) → send/receive (2001 send / 2005 push) → heartbeat (1001/1002)
        ↓ disconnect/backgrounded        ↓ KICK received (1005)
      auto reconnect + re-login         kick terminal state: stop reconnecting, the player must log in again
```

Two points specific to mini games:

- **Suspension when backgrounded**: WeChat drops the WS when the game is backgrounded. After returning to the foreground (`onShow`), actively rebuild the connection and log in again; messages from the offline period are redelivered by the server's offline queue, and you should ack them on receipt.
- **Heartbeat fallback**: if the host stays suspended long enough for the heartbeat to lapse, the server closes the connection on timeout — the reconnect flow on the next `onShow` naturally covers it, so no special heartbeat logic is needed.

When deploying to a production environment, the WS domain must be wss and registered as a legal domain in the mini-program backend (this is a WeChat platform requirement, unrelated to chirp; for local development you can use devtools' "do not verify legal domains" option to connect straight to `ws://127.0.0.1:7001`).

## Protocol / API reference

- Protocol overview: [API Overview](/api/overview) (Packet frames, message ID ranges, core flows) — read this first before integrating
- Full message ID table: the `MsgID` enum in `proto/gateway.proto` (1xxx auth/session, 2xxx chat, 3xxx social, 4xxx voice, 7xxx party)
- TS reference implementation: [sdks/ts](https://github.com/cuihairu/chirp/tree/main/sdks/ts)
- C# semantics comparison (message-plane coverage table): [Unity3D](/sdk/unity3d)
- Server integration (game backend): [Server SDK](/sdk/server) and [Server Plane](/server_plane)

## Current state and boundaries

- Voice (4xxx, WebRTC signaling + media plane) has no `RTCPeerConnection` in the mini-game environment and is **unavailable**; pure message planes such as chat/party/device push are unaffected.
- The main chat path needs only the one chat connection; social (WS 8001) and party (WS 7501) are separate services to add as needed.
- Nightly builds: the Actions nightly `daily-build` artifact bundle includes the `@chirp/protocol` npm package (unsigned); see [SDK Overview](/sdk/) for the entry point.
