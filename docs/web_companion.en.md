> 🌐 Language: [中文](./web_companion.md) | **English** (this page)

# Web Companion App (apps/web_companion)

Status: **Experimental** —— phase one delivered: login, private chat, groups, friends and presence; phase two delivered: party (third WS) and device management (fourth WS, via app_gateway). It runs on the **transitional path** (the browser connects directly to the WS edges of chat 7001, social 8001, party 7501 and app_gateway 5201), not the final topology; the unified edge depends on the P1 login-semantics unification (see [TODO.md](../TODO.md)).

## What it is

A browser-based, Discord-style companion app (Vite + React 19 + TypeScript strict + MUI 9, Chinese UI). Same stack as `apps/admin_dashboard`, but with a fully real data layer: five WebSockets + Protobuf binary frames, no mocks.

| Capability | Status | Notes |
| --- | --- | --- |
| Login / mutual kick | ✅ | scaffold mode (token is the user_id); `--token_secret` JWT mode logs in too; a new device logging in with the same account kicks the older session (KICK bounces back to the login page) |
| Private chat | ✅ | real-time send/receive, history pagination, read receipts, typing, emoji reactions, edit/delete, unread badges, offline replay |
| Groups | ✅ | create group, add members, kick, leave, member list, group chat (all synced via notify) |
| Friends | ✅ | pending-based friend request, accept/reject, delete, block; the friend list is **server-authoritative** (pulled on login + synced via notify) |
| Presence | ✅ | online/offline badges (green dot), status broadcasts, batch pull; the server broadcasts OFFLINE immediately on disconnect |
| Party | ✅ | create / invite-accept join / kick / transfer leader / ready / leave / disband; **snapshot-driven** (STATE_CHANGED pushes the full PartyInfo, the store is rebuilt from that single source), the entry carries an invited-count badge; in-party state is restored automatically on login (GET_MY_PARTY) |
| Device management | ✅ | on login the current browser registers itself as a push target (REGISTER_DEVICE, platform=web, auth-forwarded through app_gateway, user_id pinned server-side); device list/remove (GET_USER_DEVICES/UNREGISTER); the entry badge signals the unregistered state |
| Desktop notifications | ◐ | the Notification API fed from the existing chat real-time stream: a system notification pops when the tab is hidden or the message is not in the current channel, and clicking it focuses and jumps to it; permission is granted manually, never auto-prompted. True Web Push (reachable with the page closed) depends on a backend Web-Push/FCM HTTP v1 transport (TODO: real push transport); pushes currently go through the logging transport |
| Voice | ◐ | the backend signaling plane is ready (auth gate / TURN credentials / mute-deafen / timeout cleanup, see roadmap B); the client WebRTC media side is phase three |

The five connections are independent and degradable: if social/party/voice/device drop, chat stays fully usable and the corresponding entries are hidden.

## Running it

```bash
# 1. Backends (in-memory mode is fine; the device plane needs app_sdk_gateway + app_notification)
./build/services/shared/chat/chirp_chat --port 7000 --ws_port 7001 &
./build/services/social/chirp_social --port 8000 --ws_port 8001 &
./build/services/party/chirp_party --port 7500 --ws_port 7501 &
./build/services/app/notification/chirp_app_notification --port 5006 --ws_port 5016 &
./build/services/app/sdk_gateway/chirp_app_sdk_gateway --port 5200 --ws_port 5201 --notification_host 127.0.0.1 --notification_port 5006 &

# 2. Frontend (pnpm workspace: install deps at the repo root)
pnpm install
cd apps/web_companion && pnpm run dev    # http://localhost:3001
```

The dev proxy (`vite.config.ts`) routes `/ws/chat` → 7001, `/ws/social` → 8001, `/ws/party` → 7501, `/ws/voice` → 9001, `/ws/device` → 5201, so the page is same-origin with no CORS. For direct backend connection (production, or bypassing the proxy) point `VITE_CHAT_WS_URL` / `VITE_SOCIAL_WS_URL` / `VITE_PARTY_WS_URL` / `VITE_VOICE_WS_URL` / `VITE_DEVICE_WS_URL` at the real addresses; when deployed behind a reverse proxy, point the same five variables at the wss:// entry points. Whichever backend is missing simply removes that block of functionality (degradable).

### Tests

```bash
cd apps/web_companion
pnpm run typecheck && pnpm run lint
pnpm test -- --coverage        # gate: global >=70% (the protocol-core >=90% gate
                              # has moved to the sdks/ts=@chirp/protocol package)
pnpm run build

# E2E: the script starts a real chirp_chat + chirp_social (and app_gateway+notification
#    binaries when available, so the 3 device tests run, otherwise auto-skipped), then runs
#    the integration suite (18 tests: chat 6 + device 3 + social 5 + voice 4)
bash apps/web_companion/scripts/web_smoke.sh
```

## Layers (bottom-up)

```
src/
├── api/         # chat_api.ts, social_api.ts, party_api.ts, device_api.ts: one connection each, translating notify/resp into store operations
├── state/       # createStore + useSyncExternalStore thin stores (auth/conversation/message/typing/presence/friend/party/device)
├── pages/       # LoginPage, ChatPage
└── components/  # ConversationList, ChatWindow, MessageBubble, FriendsDialog, GroupDialogs...
```

The protocol core (frame encode/decode / msg_id↔message mapping / error codes / ChirpClient state machine, framework-agnostic) is not in this app's `src/` — it lives in `apps/shared/protocol` (`@chirp/app-protocol`, a file dependency, see `package.json`), shared by web and desktop; that package deliberately keeps zero React dependencies. Mobile once landed a Flutter version off this blueprint (`apps/mobile_companion`, 2026-09-19); it was removed on 2026-09-29 along with the native migration, and the protocol core is now carried by the `apps/android` (Kotlin) and `apps/ios` (Swift) native packages, reusing the same set of test vectors. Proto generated code is committed under `proto/ts/` (ts-proto, generated by `gen_proto.sh`), with zero toolchain dependency at runtime; CI has a proto-sync job guarding `.proto`-vs-generated drift.

## Protocol semantics you must know (also in the code comments)

1. **The sender gets no self-echo**: your own message reaches the screen via SEND_RESP and is deduped/merged against the real-time push by message_id.
2. **TARGET_OFFLINE is not a failed send**: the message is already in the peer's offline queue and triggered a push; the UI renders it as "delivered (peer offline)".
3. **After a KICK, auto-reconnect must stop**, otherwise both ends kick each other in a ping-pong; the client sets `kicked` and bounces to the login page.
4. **The roster is server-authoritative**: after social login, GET_FRIEND_LIST + GET_PENDING_REQUESTS fill a local mirror; from then on everything is notify-synced (3022 incoming request, 3023 ACCEPTED both ways, 3024 REMOVED). Friends are no longer persisted locally (localStorage is retired); a refresh rebuilds from the server.
5. **The ACCEPTED notify goes to both sides with user_id set to the other party**: each side records the peer into its own roster; the client ignores self-echo (purely defensive — the server cannot send it today).
6. **REMOVE_FRIEND/BLOCK are symmetric and idempotent**: the server deletes the bidirectional relation and pushes 3024 to the peer; BLOCK additionally breaks off and clears pending. Repeating REMOVE/BLOCK still returns OK.
7. **pendingOut (requests I sent) is lost on refresh**: the server only has an incoming query (GET_PENDING_REQUESTS returns only to_user_id==me); outgoing is tracked locally from ADD_RESP — a known limitation.
8. **Unread counts have no server-side increment path** (direct-to-chat has no GET_UNREAD_COUNT maintenance plane): the unread badge is a local counter.
9. **Background-tab heartbeats are throttled**: `visibilitychange` is watched and heartbeats are made up immediately on returning to the foreground; the dead-heartbeat-chain verdict is 2× (25s+10s).
10. **scaffold token=user_id is for development only**; in production, start chat **and social, voice** with `--token_secret` (the same secret — for voice that flag doubles as the signaling auth gate), and pick JWT mode on the login page.
11. **social is used as a single-instance deployment**: roster/pending/blocklist are persisted via Redis write-through (enabled by `--redis_host`, restored automatically on restart; without it they are pure memory), but presence and the session table live in instance memory with no fan-out between instances — if friends land on two different instances, online broadcasts and incoming-request pushes do not cross.
12. **Party is snapshot-driven; the client does no event merging**: the server pushes the full PartyInfo to **every member (including the operator itself)** and the client store only applies the snapshot (JOINED/LEFT/INVITE_RESULT notifications are deliberately unhandled); KICKED/DISBANDED clear the state when they arrive.
13. **Invites are in-memory and lost on refresh**: the server has no GET_MY_INVITES top-up (7028+ reserved), so unhandled join invites are not restored on page refresh; login/reconnect only restores **in-party state** (GET_MY_PARTY).
14. **The device plane is a forwarding plane, not a push plane**: web only does REGISTER/UNREGISTER/LIST via app_gateway (6001/6003/6007; 6009 is unavailable to clients, user_id is pinned server-side). True "push delivery" goes through a vendor transport, currently the logging transport; until the Web Push transport lands, messages with the page closed are unreachable, and messages while the page is open are covered by desktop notifications (Notification API). The device table lives in notification service memory and empties on restart (the next login re-registers automatically).

## Continuous Integration (CI)

`.github/workflows/web.yml` (paths filtered on `apps/web_companion/**`, `proto/**`, so C++ pushes do not run it for nothing):

- **test**: node 24 (matching the dev environment) → `pnpm install --frozen-lockfile` (repo root, pnpm workspace) → typecheck → lint → vitest --coverage (hard thresholds) → build.
- **proto-sync**: re-runs `gen_proto.sh` then checks with `git diff` that `proto/ts` did not drift (excluding the protoc version comment line).

## Roadmap

```
[Phase one · done] This doc: Web Companion App (login/private chat/groups/friends/presence)
   ├─→ [Backend fill-in A: social usable · done] GET_FRIEND_LIST/REMOVE_FRIEND/BLOCK family implemented,
   │    Redis persistence, bidirectional ACCEPTED notify, presence disconnect broadcast, JWT aligned
   ├─→ [Backend fill-in B: voice media · done] SDP targeted relay (offer/answer/candidate), TURN (coturn REST
   │    short-lived credentials, ice_servers in the join response), mute/deafen as independent dual booleans,
   │    LOGIN signaling auth, heartbeat-timeout kick; the client media side (WebRTC wiring) is still phase three
   ├─→ [Backend fill-in C: party protocol · done] 7xxx range + services/party (TCP 7500/WS 7501): invite-accept
   │    join (no invite code, repeat invites idempotent), ready check, leader succession on leave/disconnect,
   │    silent disband when the last member leaves; member snapshots via Redis write-through, invites in memory
   │    (10min lazy expiry); disconnect leaves the party (reason="offline"), notifications reach all member devices
   ├─→ [Web party UI · done] third WS (/ws/party, degradable), PartyApi snapshot-driven store, PartyDialog
   │    (create/invite/ready/kick/transfer/leave/disband + invited badge)
   ├─→ [Web device plane · done] fourth WS (/ws/device → app_gateway 5201, degradable), on login the current
   │    browser registers itself as a push target, device list/remove UI, desktop notifications (Notification API
   │    fed from the chat real-time stream); real Web Push awaits the backend transport (TODO: real push transport)
   │    the voice client is still phase three
   ├─→ [Mobile app · delivered 2026-09-19 (WP-4), removed 2026-09-29] the Flutter UI was once ported to Dart off
   │    the sdks/ts blueprint; after the native migration it is replaced by the apps/android (Kotlin) and
   │    apps/ios (Swift) protocol cores (same vector set cross-checked)
   ├─→ [Desktop · delivered 2026-09-19 (WP-5), retired with the Flutter removal] macOS/Windows/Linux targets
   │    from the same codebase; a desktop client would be a separate project if re-enabled
   └─→ [Phase three] voice client (web first); mobile revisited with the native shell batch
```

After migrating to the app_gateway aggregation edge, this doc's "direct chat/social" transitional path is retired; the page and store layers stay essentially unchanged, only the connection assembly is swapped.
