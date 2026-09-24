# @chirp/protocol

Framework-agnostic TypeScript chat protocol core for chirp — the same code that
used to live in `apps/web_companion/src/protocol/`, now a standalone workspace
package so any JS/TS target (web, minigame, LayaAir, Cocos, future Electron
shells) can depend on it without dragging React along.

## Contents

| File | Role |
|---|---|
| `src/frame.ts` | Frame codec (u32 length prefix + `Packet`) |
| `src/msg_map.ts` | Message id ↔ request/response type mapping |
| `src/chirp_client.ts` | `ChirpClient` connection state machine (login, heartbeat, reconnect, kick) |
| `src/adapters/wx_socket.ts` | WeChat minigame transport adapter: `wx.connectSocket` SocketTask → `WebSocketLike`, for LayaAir / Cocos / minigame targets |
| `src/errors.ts` | Error taxonomy shared across the pipeline |
| `src/hooks.ts` | The five hook interfaces (MessageInterceptor / AuthProvider / MessageStore / ChatEventListener / CommandHandler) |
| `src/chat_pipeline.ts` | `ChatPipeline` — hook wiring with C++-aligned send/receive ordering |
| `src/word_filter.ts` | Sensitive-word pre-check interceptor (server-aligned lexicon + masking) |
| `src/proto.test.ts` | Wire-compat conformance tests against the generated TS protos |

Zero React/DOM *runtime* dependencies; tests run in jsdom because
`ChirpClient`'s fake sockets exercise the browser `WebSocket` event surface
(`CloseEvent` / `MessageEvent`).

## Usage

```jsonc
// package.json — repo-internal consumers use the file: specifier via the
// root npm workspaces; no registry publish (red line: no releases).
"dependencies": { "@chirp/protocol": "file:../../sdks/ts" }
```

```ts
import { ChirpClient } from '@chirp/protocol/chirp_client';
import { ChatPipeline } from '@chirp/protocol/chat_pipeline';
```

Generated protobuf code is **not** a dependency of this package: consumers
bring their own `@chirp/proto` (see `proto/ts/`) and map it via the tsconfig
`paths` / bundler alias shown in `apps/web_companion/tsconfig.json`.

## Development

```bash
cd sdks/ts     # from the repo root (npm workspaces)
npm run typecheck
npm test -- --coverage   # threshold: 90% statements/branches/functions/lines
```
