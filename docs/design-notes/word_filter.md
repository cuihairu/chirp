> 🌐 Language: [中文](../zh/design-notes/word_filter.md)) | **English** (this page)

# Word Filter Algorithm Specification

Sensitive-word filtering of chat messages is **enforced server-side**; clients do a send-side precheck. This document is the algorithm specification shared by all four clients - every implementation must match byte for byte on the same semantics, so that a client's verdict can predict the server's verdict.

## Implementation matrix

| End | File | Role |
|---|---|---|
| C++ server (source of truth) | `services/shared/chat/src/word_filter.{h,cc}` | The chat service's enforcement point: three policies + lexicon hot reload |
| C++ core SDK | `sdks/core/include/chirp/{word_filter.h,word_filter_sync.h}` | Send-side precheck (interceptor) + lexicon delivery sync |
| C# (Unity) | `sdks/unity/Runtime/Chirp/{WordFilterInterceptor.cs,WordFilterSync.cs}` | Send-side precheck (interceptor) + lexicon delivery sync |
| Kotlin (native Android) | `apps/android/src/main/kotlin/chirp/mobile/protocol/WordFilter.kt` | Send-side precheck (shared by both gates: the JVM make leg + the Gradle leg) |
| Swift (iOS) | `apps/ios/Sources/ChirpProtocol/WordFilter.swift` | Send-side precheck (matching aligns with Kotlin on UTF-16 code units) |

Cross-end testing convention: each end's tests use the **same vector set** (Kotlin `apps/android/src/test/kotlin/chirp/mobile/protocol/WordFilterTest.kt`, Swift `apps/ios/Tests/ChirpProtocolTests/WordFilterTests.swift`, etc.; the Dart/Flutter SDK was removed on 2026-09-29, and its `word_filter_test.dart` no longer exists). Change semantics here first, then update each end's implementation and vectors in sync.

## Algorithm

One sentence: **ASCII-fold to lowercase, run a naive multi-pattern substring scan, write the hit ranges into a mask, and rebuild the original text from the mask (collapsing consecutive hits into one replacement)**. Not Aho-Corasick, not regex.

### 1. Lexicon parsing (LoadLexicon / parseWordLexicon)

- File format: one word per line; lines starting with `#` are comments; blank lines ignored
- Trim set matches the C locale: only trailing space/CR/LF and leading space (**tab is not trimmed**)
- Each word is ASCII-folded to lowercase, then **deduplicated + sorted** (sorting only for deterministic traversal; it does not affect semantics)
- An empty lexicon disables the filter entirely (`enabled == false`, Filter always lets through)

### 2. Case folding (ToLower / asciiLower)

**ASCII only**: `0x41-0x5A -> 0x61-0x7A`, all other bytes untouched. This aligns with the server-side `std::tolower` byte semantics under the C locale: UTF-8 multibyte sequences (Chinese and so on) are never touched at all, and matching proceeds per byte/code unit. Client implementations **hard-code** this folding as ASCII-only (Dart/Kotlin have no locale concept, and this also avoids host-locale contamination), which is semantically equivalent and more robust.

### 3. Matching (the scan part of Filter)

On the folded copy, loop a `find` scan for each word; each hit resumes from `hit position + word length` (overlapping occurrences of the same word are not counted twice). All hit positions of all words form the hit set; an empty hit set means the original text passes through unchanged.

Complexity is O(words x text length x word length). Designed lexicon sizes are at the "hundreds" of game word tables, and messages are on the order of a hundred characters, so the total workload is microseconds - the constant and interpretability of a naive scan outweigh asymptotic complexity here.

### 4. Mask-based rebuilding (the kReplace path)

- All hit ranges are written into a boolean mask whose length equals the original text length (ranges are a **union**)
- Rebuild from the mask: unmatched bytes keep their **original case**; a consecutive run of matches collapses into a **single** replacement character (lexicon `{ab, bc}` on `"abc"` yields `"#"`, not `"##"`)

### 5. Policies (three server-side, two client-side)

| Policy | Behavior | Implemented client-side |
|---|---|---|
| `replace` (default) | Mask-rewrite the hit ranges and pass through | ✅ |
| `reject` | Reject on any hit (server returns `INVALID_PARAM`; game-plane dedicated code `WORD_FILTERED=10`) | ✅ |
| `record` | Pass through unchanged, log only for after-the-fact auditing | ❌ (pure server-side operations concern, meaningless to clients) |

Policy parsing is case-insensitive; an unknown string **falls back to replace**. The replacement string is configurable, defaulting to `"**"`.

## Server runtime behavior

- **Lexicon hot reload**: any mtime change reloads the whole table, checks throttled by `reload_check_interval_ms` (default 5s) - no restart needed to change the file
- **fail-open**: missing/unreadable lexicon -> empty table + a Warn log, chat continues (a broken filter must not drag chat down with it)
- **Threading model**: only the service io thread may call it (the send path is single-threaded); lazy reload state is lock-free
- Lexicon line buffer is 512 bytes (`fgets`): **entries over 511 bytes get truncated across multiple lines** - do not put very long entries in the word list

## Design trade-offs (FAQ)

**Why not let the user/caller pick the matching algorithm?**
The "user" of filtering is the operator, not the chat user; the algorithm is a contract, not a configuration. The configurable surface is policy, replacement string, and lexicon contents. Once the algorithm becomes multi-choice, client precheck and server verdict can diverge (client passes, server rejects = a wasted round trip plus confusing errors), and proving equivalence across four ends x N algorithms explodes.

**Why not Aho-Corasick?**
The scale does not need it (see the complexity section). And if the lexicon ever grows to a performance-sensitive size, AC and the naive scan produce **exactly the same union of hit ranges** for the same lexicon - swapping `maskHits`' internals for an AC automaton leaves `Filter`'s return value byte-identical. In other words the algorithm can be upgraded as an internal implementation detail (preserving the semantic contract) without existing as an external option.

**What about homophones/pinyin/variant characters?**
Not solved by the matching algorithm: the server adds a normalization layer (pinyin tables/variant mappings fold the text first, then it enters the same matcher). Matching semantics remains single-sourced.

**Why does the client still send when it already knows the result?**
Client precheck is only a **prediction**; the server is the single verdict point (the lexicon/policy can change at any time and is not delivered to clients). The worst case of a failed prediction is one extra round trip that comes back with `INVALID_PARAM`.

## Lexicon delivery protocol (2026-10 proposal, implemented)

### Background

The server lexicon long had only local loading via `--word_filter_file` (mtime-triggered lazy hot reload), and client precheck lexicons were synced manually by deployment (the Android shell reads `filesDir/word_filter.txt`; iOS/TS build them from the caller) - once the two sides drift apart, client precheck no longer predicts the server verdict (wasted round trips, and under the reject policy the confusing case where the client can send what the server rejects). This proposal gives the lexicon a delivery channel: the client precheck lexicon treats the delivered source as authoritative, with the local file downgraded to a fallback.

### Messages (proto/gateway.proto MsgID 2245-2247, bodies in proto/chat.proto)

| MsgID | Name | Direction | Semantics |
|---|---|---|---|
| 2245 | `WORD_FILTER_FETCH_REQ` | client -> server | Fetch, body `WordFilterFetchRequest{known_version}` |
| 2246 | `WORD_FILTER_FETCH_RESP` | server -> client | Fetch response, body `WordFilterFetchResponse{code, lexicon}` |
| 2247 | `WORD_FILTER_UPDATE_NOTIFY` | server -> client | Hot-reload push, body `WordFilterUpdateNotify{lexicon}`, sequence 0, read-only, no ACK required |

`WordFilterLexicon` (shared payload of RESP and NOTIFY): `version` + `enabled` + `policy` + `replacement` + `lexicon` (the normalized lexicon text).

### Semantic specification

1. **Monotonic versioning**: every time the server loads a lexicon (initial load in the constructor, mtime hot reload, and an empty table after a failed reload) `version` increments, starting from 1; `0` means the server has no lexicon assembled. Clients ignore any delivery with `version <= local version` - immune to out-of-order and duplicate delivery.
2. **Fetch = conditional GET**: the client sends `known_version` (0 = none); when the server version matches, RESP omits the `lexicon` text (saving bandwidth), and the client skips loading on version equality.
3. **Push = hot-reload broadcast**: after a successful reload triggered by a lexicon mtime change, the server broadcasts the full new lexicon to all authenticated online sessions (snapshot under the lock, delivery outside it, aligned with the device-list broadcast). A client that never fetched also loads on receiving a push - the push is self-sufficient and does not depend on a prior fetch.
4. **Text format unchanged**: the `lexicon` text is the normalized form of the server lexicon (one word per line, lowercase, deduplicated and sorted), the same format as `--word_filter_file`, and clients pass it through `parseWordLexicon` as usual.
5. **Policy delivered with the lexicon**: client precheck mirrors the server policy - `replace` rewrites, `reject` blocks sending; `record` is a pure server-side auditing semantic, and clients **do not precheck** (pass through, avoiding an experience divergence where the client blocks something the server lets through).
6. **Authentication and guards**: FETCH without login returns `AUTH_FAILED`, a garbage body returns `INVALID_PARAM` (aligned with the guard conventions of the 2239-2244 range); the lexicon is not a secret but is not opened to unauthenticated connections.
7. **Server lexicon disabled** (no `--word_filter_file`): FETCH answers normally with `version=0, enabled=false, empty text`; on receiving it the client clears its local precheck lexicon (the server isn't filtering at all, so precheck is meaningless, and the local lexicon may be newer than the server's - this prevents the client from falsely blocking).

### Client reception (five-end SDKs, `WordFilterSync` component)

The delivered source becomes preferred, with the `WordFilterLoader` local file downgraded to fallback:

| End | Component | Fetch timing | Fallback |
|---|---|---|---|
| TS | `sdks/ts/src/word_filter_sync.ts` | the app calls `fetch()` after login succeeds | `loadLocal(lines)` |
| C++ core | `sdks/core/include/chirp/word_filter_sync.h` | the caller calls `Fetch()` after login succeeds (`Start()` subscribes to pushes) | `LoadLocal(lines)` |
| Unity C# | `sdks/unity/Runtime/Chirp/WordFilterSync.cs` | the caller calls `FetchAsync()` after login succeeds (`Start()` subscribes to pushes) | `LoadLocal(lines)` |
| Kotlin | `apps/android/.../protocol/WordFilterSync.kt` | the shell calls `fetch()` after login succeeds | `loadLocal(lines)` (the file lines the original `WordFilterLoader` read) |
| Swift | `apps/ios/Sources/ChirpProtocol/WordFilterSync.swift` | the caller calls `fetch()` after login succeeds | `loadLocal(lines)` (the text lines the original `WordFilterLoader` read) |

The component itself implements each end's `MessageInterceptor`: it holds the current `WordFilter` internally (hot-swappable) and rebuilds on arrival of `FETCH_RESP` / `UPDATE_NOTIFY` - precheck follows the server lexicon immediately. A failed `fetch()` (disconnect/timeout/non-OK) is not thrown to the UI; the fallback lexicon stays in effect and the next connection fetches again.

### Server wiring

- **basic** (`main.cc`): a new `WORD_FILTER_FETCH_REQ` case in the `HandlePacket` switch; lexicon state population and the conditional GET decision live in the shared unit `services/shared/chat/src/word_filter_push.{h,cc}`
- **enhanced** (`main_enhanced.cc`): `on_packet` interception before `DispatchDistributedPacket` with the same handling (precedent: SERVER_AUTH_REQ)
- **Broadcast**: both builds' `main()` attach a `WordFilter::set_on_reload` callback -> `BroadcastWordFilterUpdate` (iterating all live sessions in the registry)
- the distributed build never wired a lexicon at all (no filtering) and does not wire delivery

### Design trade-offs

- **Version number + full text, no two-step hash fetch**: game lexicons are a few hundred entries and a few KB, so one round trip is enough; hash negotiation turns one round trip into two and requires the client to persist hash state, so the payoff is negative.
- **Pushes are not incremental**: same reason - the full text is a self-sufficient payload, and the client keeps no patch state.
- **Clients do not precheck under the record policy**: see semantic rule 5 - precheck's job is to predict a verdict, and record has no predictable rejection behavior.
- **Clearing the client lexicon when the server is disabled**: this prevents false blocking from a "local lexicon stricter than the server" situation - blocking a message the server lets through hurts the experience more than a missed block.
