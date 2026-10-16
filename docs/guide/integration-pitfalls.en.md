> 🌐 Language: [中文](./integration-pitfalls.md) | **English** (this page)

# Integration Pitfall Guide

Aimed at game client/server teams integrating chirp for the first time. Every item here is a behavior where **the server rejects the message or silently changes its behavior**; if the client does not handle it, the symptom shows up as "the message was lost", "it won't send", or "I got kicked for no reason". Thresholds and defaults follow the code; this page marks the corresponding defaults; deployments can override them via startup arguments, see each service's `--help`.

For the message frame format, the msg-id table and the error code table, see [API Overview](../api/overview.en.md).

## Send side: six defenses, one chain

`SEND_MESSAGE_REQ` passes through the defenses in a fixed order on the server, and any rejection returns a dedicated code. **A rejected message is neither delivered nor enqueued into the offline queue**; the client picks its retry strategy from the code.

```
登录校验 → 内容长度 → 世界频道等级门槛 → 模糊闸 → 频道节奏 → 重复禁言 → 敏感词 → 投递
```

The delivery stage also has a group-channel gate: channels present in the group ledger reject non-members with `AUTH_FAILED` (the message lands in no storage), and members muted by MODERATOR+ with `MUTED`; channels outside the ledger (the Redis fan-out path) do not go through this gate. Before sending to a group, confirm you are still in it; after being removed, the old channel id can no longer be used to send.

### 1. Coarse rate limiting: `RATE_LIMITED`

- Default **120 messages/minute per user** (sending) and **30 logins/minute per IP** (login); enabled by default in the basic form; adjustable via `--send_rate_limit_per_min` / `--login_rate_limit_per_min`, 0 = disabled.
- This is a "total-volume gate": it does not distinguish channels or content. With multiple instances + Redis it is a global measurement.
- Load tests and bot scripts hit this most often. On receiving `RATE_LIMITED`, back off exponentially and do not resend immediately: resends count the same way and will keep being rejected.

### 2. Content length: `CONTENT_TOO_LONG`

Limited by **channel type** in code points (UTF-8 characters, not bytes; a 3-byte CJK character counts as 1):

| Channel | Limit |
| --- | --- |
| Private PRIVATE | 200 |
| World WORLD | 100 |
| System announcement SYSTEM | 500 |
| TEAM / GUILD / MARQUEE | Unlimited |

- The server **does not truncate**; oversized messages are rejected outright. The client must enforce limits at the input box, or tell the user to shorten the text after receiving `CONTENT_TOO_LONG`.
- Counting is by code point, not byte: judging by `content.size()` mangles Chinese text — 200 Chinese characters are already 600 bytes.

### 3. World channel level requirement: `LEVEL_REQUIRED`

- Game deployments can set `--world_channel_min_level N`: players whose declared level at login (`LoginRequest.level`) is below N are rejected on **world channel** sends (`LEVEL_REQUIRED`); other channels are unaffected. **Default 0 = disabled**, so app/companion deployments feel nothing.
- The gate lives in the auth/param tier: a send that fails the gate does **not consume** the send budget, the pacing window, the repeat-mute counter, and gets no word-filter matching — on receiving `LEVEL_REQUIRED` the client should guide the player to level up, not retry.
- The level is **declared at login** (the chat plane has no level data source), at the same trust level as the login identity; when real-time accuracy is needed, the game plane passes it through (design document form ②, not enabled).

### 4. Channel send pacing: `RATE_LIMITED`

The same user in the same channel has a minimum send interval:

| Channel | Minimum interval |
| --- | --- |
| World WORLD | 5 seconds |
| Guild GUILD | 2 seconds |
| Private PRIVATE | 1 second |
| TEAM / MARQUEE / System announcement | Unlimited |

- The window is anchored to the **last send that was actually let through**: rejected retries inside the window do **not** extend the wait. So the right move is: after being rejected with `RATE_LIMITED`, wait out the remaining window, instead of retrying continuously.
- This is an **in-process** counter: in a multi-instance deployment each instance times independently (only the coarse gate is global).

### 5. Repeat-message mute: `RATE_LIMITED`

- The user's **third consecutive message with identical content** triggers a **5-minute mute**, and the triggering message itself is also rejected (so the spammy third copy reaches no one).
- During the mute, **any content** is rejected (same code `RATE_LIMITED`); sending different content immediately resets the streak counter.
- Triple-clicking a client "resend" button with the same text is exactly this mute. Either disable retries or prompt the user to change the text.

### 6. Banned words: `WORD_FILTERED` (or silent replacement)

- Three server policies (`--word_filter_policy`): `replace` (default; the matched segment is collapsed to `**` and let through) / `reject` (rejected with `WORD_FILTERED`) / `record` (let through, only a Warn log is written).
- The lexicon file `--word_filter_file` is one word per line, `#` comments, case-normalized; it hot-reloads within 5 seconds of an mtime change; if the file is missing or unreadable it **fail-open** (does not block chat).
- Under the `replace` policy, what the client echoes back can differ from what was sent (collapsed); that is expected behavior, not a bug to report.
- On the C++ SDK side, `WordFilterInterceptor` + `ParseWordLexicon` provide the same lexicon format and semantics; a client can share one lexicon file with the server for local pre-checking and save a round trip.

## Receive side: silent semantics

These return **no error code**; they are the server's silent filtering / silent success. When troubleshooting "the message never arrived", look here first:

- **Message blacklist** (2239-2244): after A blocks B, private messages from B to A still **return `OK`** (B's side looks like the send succeeded), B's group/world messages are filtered per member, and A never receives them. Messages already in the offline queue before the block are still delivered. Unblocking is idempotent. Independent of the social-plane friend blacklist (3011), and only affects message delivery.
- **Channel muting** (2235-2238): after muting world/guild/team channels, real-time pushes are skipped and the offline queue no longer enqueues them, but **history is still fetchable** (GET_HISTORY does not filter). MARQUEE/system announcements are server broadcasts and cannot be muted; sending SET_CHANNEL_MUTE for those channels returns `INVALID_PARAM`.
- **Offline queue**: when a private-message recipient is offline, `SEND_MESSAGE_RESP` returns `TARGET_OFFLINE` (not an error — the message is already queued) and is delivered on the recipient's next login. The in-memory fallback caps at **200 messages per user**, dropping the oldest beyond that; capacity for the Redis/MySQL form is in the deployment configuration. Treating `TARGET_OFFLINE` as a failure shown to the user is wrong; the message was actually accepted.

## Recall (message edit/delete plane, 2225-2232)
`DELETE_MESSAGE_REQ` (`is_hard_delete = false`) is "recall" for **the sender themselves**: the server soft-deletes it within the recall window, broadcasts `MESSAGE_DELETED_NOTIFY` to channel members (the client uses this to swap the bubble for a "message recalled" tombstone), and reclaims copies already in recipients' offline queues.

| Item | Default | Startup argument |
| --- | --- | --- |
| Recall window | 120 seconds | `--recall_window_sec` (seconds, 0 = unlimited) |
| Recallable channels | Private PRIVATE + Guild GUILD | `--recall_channels` (comma-separated, empty = no channel is recallable) |

Response code semantics (`DELETE_MESSAGE_RESP.code`):

| Code | Meaning | Client action |
| --- | --- | --- |
| `OK` | Recalled, delete notify already broadcast | Swap the local bubble for a tombstone |
| `AUTH_FAILED` | Not the sender; or the request had `is_hard_delete` but no moderator permission; or `user_id` does not match the authenticated identity | Hide the recall entry, do not retry |
| `INVALID_PARAM` | Past the window / channel not recallable / already recalled | Tell the user "recall window expired", **do not retry** (a repeat recall does not broadcast twice) |
| `USER_NOT_FOUND` | This process has no ledger entry for the message (e.g. it predates this process) | Same, treat as "recall failed" |

- **Moderator deletion is not recall**: a channel moderator (`is_hard_delete`, guild role ≥ MODERATOR) goes through the governance path, **not subject to the window or channel limits**, and `is_hard_delete=true` permanently erases the ledger entry. To open recall for a channel in operations, change `--recall_channels`; do not lean on the moderator path.
- **Render a tombstone instead of deleting the bubble**: `is_hard_delete=false` and `deleted_by` == the sender_id of that message → show a gray "message recalled" placeholder (keep the line height, otherwise chat lists jump); if `deleted_by` is not the author → moderator deletion, render as "message deleted".
- The C++ SDK exposes `RecallMessage(message_id, cb)` (a semantic alias of `DeleteMessage(id, false, cb)`, named for the game-side "recall" button); update the UI only when the server returns `OK`.
- Recall only affects the **real-time path + offline queue**; the archive layer (Redis history/MySQL) has no recall tombstone, so a fresh `GET_HISTORY` still returns the original text. For "the original must no longer be fetchable after recall", the client maintains its own recall list by `message_id` and filters it when pulling history (the server-side tombstone is scheduled in a later batch).
- Recall (or a moderator deletion) also **reclaims** copies already in recipients' offline queues (by `message_id`), so the other side will not receive the original on their next login; a client that needs a trace of "recalled but never seen" messages must record it itself when the recall succeeds.
- `BULK_DELETE` is not bound by the window: it is a governance interface (only deletes your own or what a moderator may delete), does not check the recall window and does not reclaim offline copies. Clients needing strict window semantics must not use it for recall.
- **Form difference**: edit/delete/recall (2225-2228/2231-2232) were added to the **enhanced form** (`main_enhanced.cc`) in the 2026-10-02 batch; still wired only in the **basic dispatch table** (the direct entry in `services/shared/chat/src/main.cc`) is `BULK_DELETE` (2229) — the enhanced side has no such handler, so the request **hangs silently**. Before running an enhanced deployment on MySQL/Redis, confirm this (or move bulk deletes to another RPC plane).

## Sessions and connections

### Heartbeats are the client's responsibility

- The server **never** proactively kicks a silent connection (the chat plane currently has no connection-level idle timer): you can hang without sending a pings, and a TCP half-open connection is only exposed by kernel keepalive or a write failure.
- The official SDK's defaults: send `HEARTBEAT_PING` every 25 seconds; the pong must echo the ping's non-zero sequence to count as answered; 2 consecutive unanswered pings mark the connection dead → auto-reconnect kicks in.
- **Raw-protocol integrators must implement equivalent logic**, otherwise after a network loss the client sits in a "fake online" state receiving nothing until the next write failure.

### Kick (KICK) semantics

- Since 2026-09-27 the kick key is `(user_id, platform)`: logging in again on the same platform kicks the old session, the old connection receives `KICK_NOTIFY` (reason like "logged in on another ios"; legacy clients that do not report a platform normalize to "default", keeping the old one-user-one-session behavior with the reason falling back to "logged in on another device"), and the new connection gets `LOGIN_RESP.kick_previous = true`.
- The same user **across different platforms** coexists and receives messages on each (multi-device semantics); `device_id` is only session metadata and plays no part in the kick decision.
- After receiving `KICK_NOTIFY`, do not reconnect: a kick is terminal, and reconnecting + logging in again kicks each other forever. The official SDK no longer auto-reconnects on KICK; self-developed clients must honor the same convention.
- Multiple web tabs share the same platform (e.g. all "web"), so a new tab's login kicks the old tab — a direct consequence of platform-level kicking, with no special case. Integrators needing concurrent tabs should give each tab a different platform, or accept mutual kicking.
- Online-device visibility: the login response `LoginResponse.online_devices` carries the initial list of the other online devices; afterwards, logins/disconnects/kicks are broadcast via `DEVICES_PRESENCE_NOTIFY` (1020) as `[{platform, device_id, online, ts}]` (local to the registry, excluding the recipient itself; in a multi-node deployment other instances do not hear it).

### Disconnect reconnection (official SDK defaults)

- Exponential backoff 500ms → 15s, ±20% jitter, unlimited retries by default (`max_reconnect_attempts = -1`); explicit `Disconnect()`/`Logout()` cancels reconnects.
- After a successful reconnect you **must LOGIN again** — connection and authentication are two states (`Connected` ≠ `LoggedIn`); reconnect only returns to `Connected`.
- Requests issued during the reconnect window fail immediately (`NotConnected`/`Closed`); SDK single requests default to a 10-second timeout, so the business side needs "resend after reconnect" semantics (a local queue or UI retry); the SDK does not resend for you.

### Don't disconnect right after a fire-and-forget send

- The convenience overload `SendMessage(receiver, content)` is fire-and-forget: the send is posted to the SDK's internal io thread and does **not wait for the receipt**.
- Calling `Disconnect()` right after the send (or exiting the process) stops the io_context, queued send lambdas are dropped outright, and the message **silently vanishes on the client side**, leaving zero trace or log on the server. High load or slow machines widen this window, showing up as "occasional message loss" that cannot be reproduced.
- When delivery confirmation is needed or you are about to exit, use the typed overload with a receipt and wait for the `SEND_MESSAGE_RESP` callback before disconnecting:

```cpp
SendOptions opts;                      // channel_type/receiver_id/...
client.SendMessage(opts, content,
  [&](const std::error_code& ec, const chirp::chat::SendMessageResponse& resp) {
    // 走到这里说明请求已真实发出并收到服务端应答,此时断开是安全的。
  });
```

- Do not forget the two forms' divergence in `resp.code()`: a private message to an offline user returns `TARGET_OFFLINE` (6, already in the offline queue) in the basic form but `OK` in the enhanced form — both count as a completed send.

### Callback threads

- All C++ SDK callbacks fire on the **internal io thread**. Touching UI/engine objects directly inside a callback crashes; the engine adapter layer dispatches back to the game thread (the Unreal shell uses `AsyncTask`; the Unity shell marshals internally).

## Business codes and transport errors are different things

- The SDK's `ec` (transport layer: `NotConnected`/`Timeout`/`Closed`/`Kicked`/`BadResponse`) being OK **does not mean business success**; you must also read `resp.code()` (the error codes in the tables above).
- Conversely, when `resp.code() != OK`, `ec` is also OK — the transport was fine; the server's business logic rejected it.

```cpp
client.SendMessage(opts, "gg", [](const std::error_code& ec,
                                  const chirp::chat::SendMessageResponse& resp) {
  if (ec) { /* 传输层:重连后补发 */ return; }
  switch (resp.code()) {
    case chirp::common::OK:             Use(resp.message_id()); break;
    case chirp::common::CONTENT_TOO_LONG: PromptTooLong(); break;   // 提示精简,别重发
    case chirp::common::RATE_LIMITED:   ScheduleRetry(); break;     // 等窗口,别立刻重发
    case chirp::common::WORD_FILTERED:  PromptFiltered(); break;    // 改文案
    default:                            LogAndDrop(); break;
  }
});
```

## Cross-plane replies (companion app and game)

- When an app player replies to a game channel: the message to app_chat must target the channel as **`<game_id>:<bare channel id>`** (e.g. `game_zx:world`), and the hub routes it to the corresponding game's game_chat injection. A wrong prefix → `INVALID_PARAM` (the player has no binding for that game) or `SERVER_UNAVAILABLE` (that game has no online spoke).
- This path **consumes send budget** (it is intercepted after the send rate limit) and shares the coarse gate/pacing with normal sends.
- What fans back to the app player is an unprefixed private copy, so no loop forms.

## Server integration (game backend, 5xxx)

- Dial **two** connections: `chirp_game_server_gateway` (8100, injection/events) and the app_chat main port (7000, identity binding/channel subscription/unreads). Sending an RPC to the wrong target **times out** rather than erroring — the most common reason for "it hangs 10 seconds before failing".
- Event delivery is **at-least-once**: after handling `EVENT_DELIVER_NOTIFY` you must reply `EVENT_ACK_REQ`, otherwise the hub redelivers after reconnect — make consumer logic idempotent by `event_id`.
- Injected messages carry the `inject_id` idempotency key: a resend with the same id produces no duplicate message, so retries are safe.
- An injection returning `OK` only means the server plane accepted it; it does not confirm delivery on the player's side; offline players go through the offline queue.
- Authentication rejection shows up as periodic retry warnings (fixed delay, unlimited retries) and never exits on its own — when credentials are misconfigured, watch the startup log.
- The Go SDK's semantics align item by item with the C++ reference implementation; for details see [Server SDK](../sdk/server.en.md).

## Quick self-check checklist

When integrating and debugging, go through this list in order — most cases of "messages mysteriously lost" match one of these:

1. Is `LOGIN_RESP.code == 0`? (token issuer/secret/algorithm all consistent — HS256 on the game plane, `--token_secret` the same value on both ends)
2. Are heartbeats being sent? Is the pong's sequence echo verified?
3. On a send failure, are you reading `resp.code()` or only looking at `ec`?
4. Is a `RATE_LIMITED` pacing (wait out the window) or the coarse gate (back off)? Does the retry logic distinguish them?
5. Is `TARGET_OFFLINE` being treated as a failure?
6. Have you blocked/muted the other side? (check `GET_BLOCKED_SENDERS` / `GET_CHANNEL_MUTES`)
7. Is the `<game_id>:` channel prefix written correctly? Is the player binding in place?
8. Are server events acked? Is the `inject_id` idempotency key attached?
9. Is the recall entry's visibility computed from the server rules (sender themselves + within the window + recallable channel), and are the failure codes distinguished?

## Related documents

- [API Overview](../api/overview.en.md): frame format, msg-id table, error code table
- [Server Plane](../server_plane.en.md): the 5xxx contracts and semantics
- [Peer registration protocol](../api/peer_protocol.en.md): cross-plane spoke/hub handshake
- [SDK hooks interface](../design-notes/sdk_hooks.en.md): interceptors/storage/auth providers
- [Game chat feature list](../design-notes/game_chat_features.en.md): the original feature design
