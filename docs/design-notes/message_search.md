> 🌐 Language: [中文](../zh/design-notes/message_search.md)) | **English** (this page)

# Message Search — Decision Record and Delivery Notes

> Status: decided on 2026-10-08 (user instruction: settle it myself per the repo's decision-record convention and record the rationale, then implement). This file is the
> decision record for the search surface graduating from "an in-process index shell" to "a server-side search service". The delivery shape is
> authoritative as written here; "decided text" in this file refers to the decision proposal recorded in the 2026-10-07 TODO (written after
> surveying the Wildfire IM reference), and "delivery revision" refers to adjustments made during implementation on the strength of code facts — every
> revision states its reason, and the decided text is not rewritten back.

## 1. Background

- `chirp_search` (services/search) was a 100 ms sleep shell before it was promoted: the io_context never
  ran, there was no listener, yet it printed "listening"; `MessageSearchService` was an in-process inverted-index
  library (with unit tests), and there was no search wire protocol anywhere in the repo — no proto, no client, and no
  process dialing 5007.
- Reference survey (2026-10-07, Wildfire IM): Wildfire has **no server-side message search protocol**
  (`t_messages._searchable_key` is written on insert but the community edition never queries it); real search = client-side
  local SQLite. That route is unreachable for chirp: neither the web nor the app client has a local persisted message store.

## 2. Decision

### ① Topology: client → gateway → search 5007

- The search service listens on TCP 5007 (port realigned, consistent with the port table in docs/api/overview.md).
- Both client edges (`chirp_game_sdk_gateway` / `chirp_app_sdk_gateway`) attach via
  `--search_host` each; the wiring reuses ServiceBridge's per-client pipe mechanism (see
  ⑤): one internal connection per client, with the SERVER_AUTH_REQ service trust gate + LOGIN_REQ replayed verbatim,
  2248 forwarded as-is and replies flowing back as-is. **Delivered (2026-10-08, ✅)**: the default branch of both
  gateways intercepts
  2248 before the chat 2xxx catch-all
  (`id == SEARCH_MESSAGE_REQ` is checked against the search bridge first); when `--search_host` is left empty
  it falls back to the chat catch-all, byte-identical to the pre-search-attachment boundary behavior; service_id/secret
  share the chat bridge's gate (reusing the `--chat_service_id`/`--chat_service_secret` configuration; on the search side just
  set `--gateway_service_secret` to the same value).
- **One behavioral difference from the chat bridge**: a failed chat bridge = kick the client (all chat traffic dies, so kicking
  and reconnecting is cleanest); a failed search bridge = **degrade without kicking** — the client stays online, 2248 returns
  `SERVER_UNAVAILABLE`, and after search recovers the next query rebuilds the pipe automatically. Search is an optional enhancement
  capability and its availability must not be allowed to drag down the main chat path.

### ② Protocol: 2248 / 2249 (body in chat.proto)

- `SEARCH_MESSAGE_REQ = 2248` / `SEARCH_MESSAGE_RESP = 2249` (the free id pair after the 2247 word-list batch).
- `SearchMessageRequest{keyword, channel_id?, content_types[], composite cursor
  before_timestamp+before_message_id, limit≤50}`; a missing `channel_id` = the requester's entire visible surface.
- `SearchMessageResponse{code, matches[], has_more}`; a match carries
  message_id/channel/channel_type/sender/sender_kind/msg_type/timestamp/content
  **verbatim** — highlighting is done client-side, and the server does not return a snippet (to avoid pushing server-side truncation semantics into the protocol).
- Cursor semantics: a `(before_timestamp, before_message_id)` composite key, ordered by
  `timestamp DESC, message_id DESC`, consistent in direction with the existing GET_HISTORY.

### ③ Index: search-side in-process SQLite FTS5 persistent index

- vcpkg `sqlite3[fts5]`, with zero new operational components (no ES, no separate index process).
- Full backfill from the MySQL `messages` table at startup, plus the 100 ms shell loop turned into an incremental
  `id`-cursor tail pump (the sleep loop that used to print a fake "listening" was perfectly repurposed into a tail pump).
- **Recalled/deleted messages are not indexed**: chat writes the recall tombstone as `is_recalled=1, content=''`, and both
  backfill and tail pump skip `is_recalled=1`; for the "indexed first, recalled later" window, queries cross-check candidates in batch
  against MySQL by message_id (a single `IN` query), drop recalled hits from the results and incidentally delete them from the
  index (self-healing). MySQL unreachable ⇒ the query fails closed (INTERNAL_ERROR); it does not degrade into
  "answer raw from the index".

### ④ Visibility: direct MySQL read for the decision

**Delivery revision (recorded honestly)**: the decided text said "DM sender/receiver + group group_members
checks". On implementation it was verified: chat's groups (2101-2121, `GroupManager`) are **pure in-process memory**
state — the two MySQL tables `groups`/`group_members` are created in init_db.sql but not a single line of code
in the whole repo reads or writes them, so there is no way to do group-membership checks by "direct same-DB read". And the existing `GET_HISTORY` already has no membership gate for
group/guild/team channels (`ValidateGetHistoryRequest` only validates the
`userA|userB` containment relation for PRIVATE). Making search stricter than GET_HISTORY would invert the
permissions of the two read paths. So this batch delivered as:

- **PRIVATE**: the requester must be one side of the channel's `userA|userB` (same rule as
  GET_HISTORY), decided by a direct read of the sender/receiver on the MySQL `messages` row (the authoritative fact; the index columns
  are only a mirror);
- **All other channels** (WORLD/GUILD/TEAM/SYSTEM/MARQUEE and chat group channels): aligned to
  GET_HISTORY's current baseline — visible to anyone authenticated;
- Tightening the group-membership gate is listed as a follow-up for when chat group persistence lands (at that point the group_members table becomes
  real data, search can switch its check over, and the protocol does not change).

**Implementation supplement (2026-10-08, settled when the server side landed)**: filtering happens at query time (recall removal +
PRIVATE gate), while 2248's pagination cursor advances on "the hits that were returned" — if every candidate on a page is
filtered out, the client gets no hits and therefore has no cursor to advance, so resending the same query spins in place. So the server
**paginates internally**: it only replies once it has gathered `limit` visible hits or exhausted the index scan, with `has_more` = the original candidates
not yet exhausted. The next page's candidates have at least some original hits, so the client cursor (composite ts,id) advances monotonically
and is guaranteed to converge; the page after the most-filtered one may be 0 hits + has_more=false, and the semantics are still
"there really is nothing left".

Rejection record: client-side local search (no attainable cost profile, see §1), externalized ES (operations surface does not match), querying via the
hub's event surface (does not fit req/resp semantics).

### ⑤ Renamed ChatBridge to ServiceBridge along the way ✅ (delivered 2026-10-08)

Once search reused the per-client pipe mechanism, the name "ChatBridge" was
wrong for the search pipe. Renamed to `ServiceBridge` (`libs/network/service_bridge.{h,cc}`), with one instance each
for chat and for search; the chat instance keeps the existing kick semantics (`kKickClient`), and the search instance
uses degrade semantics (`kDegrade`). Zero change in protocol semantics.

**Delivery supplement (recorded as actually implemented)**: the two failure semantics are distinguished by the same class's
constructor switch
`search_degrade` (a bool, not an enum). The entire degrade chain is contained inside the bridge, with
zero synthesizing logic on the gateway edge:

- `Attach` retains each client's login credentials (token/device + a weak reference to the client);
- a 2248 with no pipe → synthesize `SEARCH_MESSAGE_RESP(SERVER_UNAVAILABLE)` on the spot (sequence
  reusing the request's) and send it back to the client, without touching the connection, and redial on the spot with the credentials;
- a 2248 during the handshake enters a bounded pending queue (64); queue full = the backend keeps not responding → the whole pipe degrades
  (each pending query is backfilled with a degraded return code), and the current one also gets a code;
- the pipe drops mid-stream (including a rejected/timed-out handshake) → each pending query is backfilled with `SERVER_UNAVAILABLE`,
  credentials are retained, and the client's next query redials automatically (replaying the original token/device);
- the chat instance's path is preserved byte for byte (including the kick reason strings: `chat
  unavailable`/`chat session lost`/`chat session rejected`).

Unit tests: `tests/unit/service_bridge_test.cc` (29 cases, including the four degrade states: unreachable returns a code,
dropped pipe drains pending + redials, full queue degrades the whole pipe, a rejected handshake does not kick); the original chat bridge cases were moved over verbatim.

## 3. Same-batch item: group nickname alias (same batch as search, both belonging to the retrieval/presentation surface)

- Same shape as Wildfire's `t_group_member._alias`: a group member can set their own display alias inside the group.
- Three-piece delivery:
  - **schema**: the `group_members.alias` column (new DB in init_db.sql + an upgrade script for existing DBs;
    currently there is no code reading or writing that table, and this batch still does not change that — the column comes first, and persisted writes come with chat group persistence
    delivery, see the ④ revision);
  - **setting surface**: `SET_MEMBER_ALIAS_REQ/RESP = 2122/2123` + a whole-group notification
    `GROUP_MEMBER_ALIAS_UPDATED_NOTIFY = 2124`; permission = the person themselves, or a MODERATOR+ setting someone else's;
  - **two consumption points in rendering**: the member list (`GroupMember.alias` in `GET_GROUP_MEMBERS_RESP`)
    and the sender name of group-chat messages (the client maps from the member list, zero protocol change).
- The alias is stored in `GroupManager`'s in-memory state (same lifecycle as the group itself — a group is already lost after a restart,
  which is the existing boundary, and alias does not make it look more durable than it is).

## 4. Not batched in

- **Suppressing mobile push when desktop is online** (same shape as Wildfire's MessagesPublisher isPcOnline): not part of this
  batch, to be scheduled separately after search lands (user instruction 2026-10-08).
- The second review of prototype 03's revision draft remains pending user review; not reviewed on the user's behalf, not touched (user instruction 2026-10-08).

## 5. SDK wiring (word_filter batch precedent: proto → three SDKs → server chain) ✅ (delivered 2026-10-08)

| End | Delivery |
|---|---|
| C++ core (sdks/core) | `ChatClient::SearchMessages` (keyword required + all of channel_id/content_types/composite cursor/limit optional) + `SetMemberAlias` (ca1bf4b); returns `NotConnected` when not connected, short-circuits an empty keyword as `InvalidParam` without sending a packet, and has an all-options wire round-trip case (0fca589) |
| TS dual packages | The game side in sdks/ts (ca1bf4b) and the companion side in apps/shared/protocol as `@chirp/app-protocol` (0fca589, required by the sdks/ vs apps/ boundary ruling to keep the two protocol packages in sync) each delivered `searchMessages`/`setMemberAlias` + the 2124 `onGroupMemberAliasUpdated` notify fan-out (including ignoring undecodable bodies and isolating throwing listeners) + the `SEARCH_MESSAGE`/`SET_MEMBER_ALIAS` specs, and each added the same-shape lock-test vectors (all-options round-trip and decode, the validation order where connection state comes before keyword, alias clear echo, notify fan-out, spec encode/decode round-trip) |
| C# (sdks/unity) | `SearchMessagesAsync`/`SetMemberAliasAsync` + `Specs.SearchMessage`/`SetMemberAlias` (ca1bf4b; the nullable annotation fix for contentTypes in 0fca589 — the original form would not compile with Nullable enabled + TreatWarningsAsErrors) |

Android / iOS / HarmonyOS are not in this batch (per the 2026-10-05 instruction, iOS/HarmonyOS only get code written when there is free time;
the Android native pipeline runs on a separate rhythm). The web/desktop consumption side (ChatApi + the alias setting surface +
the two consumption points in rendering) is the next increment: the ChatApi layer is in place on both ends (desktop 226d912;
web filled in `searchMessages`/`setMemberAlias` + start() subscribing to the 2124 fan-out
`onGroupMemberAliasUpdated` to refresh the group list, with malformed bodies silently dropped, 7 vitest cases —
the three search short-circuit/transparent cases, the alias short-circuit echo, 2124 valid refresh/malformed ignore). **The web search
UI has landed (2026-10-09)**: `SearchDialog` (sidebar entry) — keyword required,
"load more" carrying the (timestamp, message_id) cursor of the last item on the page; a private-chat hit jumps after locally backfilling the
conversation, a group hit jumps only if a local conversation exists, and other channels (world/system etc., which have no conversation surface on web) are read-only
display; SERVER_UNAVAILABLE shows "search service not connected"; highlighting per the decision record is done client-side,
and the first version kept it simple by not doing it. 5 vitest cases (empty state disabled/hit rendering and private-chat jump/cursor paging/degraded copy/
empty result). The desktop search UI (9310b7f) and the 2248 end-to-end smoke leg have both landed;
the alias setting surface and the two rendering points are also closed (2026-10-10): both ends' ChatApi gained a group-nickname snapshot
(`groupAliasOf` + `subscribeGroupAlias`; maintained both by roster pulls and 2124 notifications,
empty string = clear and remove); the setting surface — a prompt on web's group settings member row (the person can set theirs at any time,
MODERATOR+ can set others', same permission line as mute; prefilled with the current value, empty string clears), and inline
input on desktop (wry WebView has no prompt, same precedent as mute-duration input); the two rendering points — the member list row
prefers the nickname (web shows the real name as a subordinate line, desktop annotates the real name in brackets), the sender name of group-chat messages maps via the snapshot
with a fallback to the real name, and 2124 re-renders immediately. web vitest 258/258, typecheck green on both ends.
The web/desktop consumption chain for the search batch is now fully closed.

## 6. Acceptance criteria

- ✅ Both trees build with 0 warnings, full ctest (44 suites), and all existing smoke legs green;
  search's new unit tests cover tokenization, the FTS index, visibility filtering (including DM permission removal),
  the cursor, the two-connection tampering defense line and degrade synthesis; alias's new unit tests cover permissions and notifications
  (1ab9c02; service_bridge 28 cases including the four degrade states 0bd1ded).
- ✅ `chirp_search` starts and really listens on 5007 (SERVER_AUTH_REQ trust gate +
  LOGIN replay + retrieval + 100 ms tail pump + optional metrics, 1ab9c02),
  and gateway `--search_host` attachment is in place; **the client 2248 end-to-end round-trip smoke leg is now standing
  (2026-10-09)**: `--smoke-search` (test_services.sh) — chat(MySQL) +
  chirp_search + game_sdk_gateway as real processes with both bridges, a seeded private-chat message lands in MySQL via the chat bridge, and `chirp_chat_send_client --act search` sends 2248 via the gateway search bridge
  and hits after retrying (tail pump latency absorbed, capped at 10 s); MySQL is probed at the effective coordinates and skipped when unreachable
  (CI service containers connect via the binary defaults); a same-named step was added to the CI smoke job; a local real run
  hit `code=0 matches=1`. Note: existing DBs need `scripts/upgrade_db_messages.sql`
  to add the sender_kind column, otherwise StoreMessage fails silently (hit this locally).
- ✅ Documentation reconciliation: this file §5, docs/api/overview.md (the 2122-2124 rows,
  the 2248/2249 rows, the 5007 status row), CAPABILITY_MATRIX (the two edge rows),
  and TODO.md (the search decision entry) were kept in sync with each increment.
