# Chirp Admin Dashboard

Monitoring shell for the Chirp platform: metric cards (users/online/messages/
channels), a users table, a channels table and a messages explorer with
search. React 19 + Vite 8 + MUI 9 + Recharts; tests are vitest + Testing
Library.

## Run in dev

The pages call `/api/*` through Vite's dev proxy (target
`http://localhost:5000`, see `vite.config.ts`). Run both processes:

```bash
pnpm dev:api     # dev API server on :5000 (server/admin_api.mjs)
pnpm dev         # Vite on :3000, proxies /api -> :5000
```

## The /api/* contract

| Route | Response |
|---|---|
| `GET /api/stats` | `{totalUsers, onlineUsers, totalMessages, activeChannels}` (numbers) |
| `GET /api/users` | `[{id, username, email, status, role, joinedAt, lastSeen, messagesSent}]` |
| `GET /api/channels` | `[{id, name, type, category, memberCount, messageCount, createdAt}]` |
| `GET /api/messages?limit=N` | `[{id, content, author, channel, timestamp, reactions}]` |
| `GET /api/messages/search?q=` | `{results: [...]}` (same message shape) |

`server/admin_api.mjs` answers this contract with a fixed seed so the shell
runs end to end; `server/admin_api.test.mjs` locks the shapes
(`pnpm test:api`). The frontend suites (`pnpm test`) pin the same shapes
through mocked `fetch`.

## Production backend: not implemented yet

The C++ services only expose `/metrics` (Prometheus text); none owns an
admin HTTP API. `server/admin_api.mjs` is a **dev fixture** — the seed is
not live service state. Wiring the contract to real data (chat store users/
messages/channels) is future work tracked in todo.md; until then the dev
server is the only `/api/*` implementation and must not be treated as a
production endpoint.
