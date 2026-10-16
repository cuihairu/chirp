> 🌐 Language: [中文](./capacity_benchmark.md) | **English** (this page)

# Gateway concurrent-connection capacity benchmark (measured)

> Measured 2026-09-27. Backfills the "load test: gateway 10k+ concurrent-connection benchmark" item in [roadmap_history](/design-notes/roadmap_history).
> **All numbers come from single-instance loopback tests on a shared development machine**; environmental noise and measurement caveats are in [Limitations](#limitations-and-applicability) and must be quoted together with that section.

## TL;DR

| Metric | Measured value | Conditions |
| --- | --- | --- |
| Concurrent online connections | **12000/12000** logged in and online, 0 handshake timeouts | spread over 60s at 200 conn/s, rounds=0 pure-capacity tier |
| Connection rate (no concurrent traffic) | **200 conn/s sustained clean** (500/1000/2000/12000 tiers all at 0 handshake timeouts) | clients connect only, send no messages |
| Connection rate (with concurrent traffic) | **Even 50 conn/s breaks through**: the steady500 tier hit 165/500 pipe handshake timeouts | the connection ramp overlaps with message sending |
| Steady-state private-chat throughput | Bound by server-side pacing (private 1s per user, hard-coded), ceiling ≈ **conns/1.1 msg/s** | round-robin mutual sends, interval 1100ms |
| Steady-state send RTT | See the tiered table in [Steady-state send tiers](#steady-state-send-tiers); on a quiet machine 200 connections p50 ≈ 2-3ms, on this machine at loadavg≈60 p50 ≈ 80-170ms | RTT includes loopback network + two service processes |
| Memory | chat peak RSS ≈ 19.7MB, gateway ≈ 8.6MB at 12k connections | VmHWM, including per-connection pipes and session state |

The meaning of "10k+ concurrent connections": **12000 TCP connections logged in and online at the same time** (each has completed LOGIN_RESP and holds a gateway session plus a gateway→chat pipe). It is not 12000 users sending messages at high frequency simultaneously — see the throughput row and [Bottlenecks and boundaries](#bottlenecks-and-boundaries) for send-plane capacity.

## Topology under test

A minimal game plane isomorphic to smoke-game:

```
chirp_load_client ──TCP──> chirp_game_sdk_gateway ──one pipe per client──> chirp_chat
   N connections              (port G/WS)            ServiceBridge 2xxx forwarding    (port P/WS)
```

- The client wire protocol matches production: `[u32_be len][chirp.gateway.Packet]`; login goes through the gateway scaffold (token=user_id).
- chat and gateway are each a **single-threaded `io_context::run()`** (`services/shared/chat/src/main.cc`, `services/game/sdk_gateway/src/main.cc`).
- The gateway opens one pipe to chat per client; the pipe is authenticated via `SERVER_AUTH_REQ` (`--gateway_service_secret`); the pipe handshake timeout is hard-coded at 5s (`libs/network/service_bridge.cc:25`).

### Dependency shape (critical)

`chirp_chat` (the enhanced main) has Redis/MySQL arguments that **default to 127.0.0.1** (`main_enhanced.cc`); without explicit configuration it connects to the shared instances, and if the shared MySQL rejects the credentials, every message's archive write **fails on a single attempt in the background thread and only warns** (`hybrid_message_store.cc` `StoreMessageAsync`, no retry), but the resulting history-tier skew gets amplified by reads and writes and slows responses — measured, the 200-connection tier ran 9 minutes without exiting and saw 65% pipe handshake timeouts. Formal measurement therefore uses dedicated throwaway dependencies:

- dedicated redis: `redis-server --port 16379 --save "" --appendonly no`
- dedicated MariaDB: `mariadbd --datadir=… --port=13306 --skip-grant-tables` (the store auto-creates `CREATE TABLE IF NOT EXISTS`), then just `CREATE DATABASE chirp`

Known harmless noise: `HybridMessageStore`'s startup self-check uses `GET ping` and misreads the nil reply for "key does not exist" as a connection failure, logging one WARN line (`hybrid_message_store.cc:109-111`); actual reads and writes proceed normally (`RedisClient` is now a reused connection pool, see [Bottlenecks](#bottlenecks-and-boundaries) item 4).

## Tooling and method

- Tool: `chirp_load_client` (`tools/benchmark/load_client.cc`) — one thread per connection, supports `--ramp-ms` to spread out connections, `--barrier on|off`, `--rounds 0` for a pure-connection tier, per-round RTT percentiles, login RTT percentiles, and send-window throughput.
- Runner: `tools/benchmark/run_capacity_bench.sh` — port preflight plus an ownership assertion (when a bind conflicts, probing falsely succeeds, which once produced the garbage data "5000/5000 online, login p99=42s"), taskset core pinning (services 0-3 / clients 5-13), VmHWM and `/proc/$pid/stat` CPU core-seconds, gateway handshake-timeout counting.
- Concurrency guard: `tools/benchmark/bench_guard.sh` (a sourced two-piece set shared by run_capacity_bench.sh and the individual probe scripts) — `bench_acquire_lock` uses flock for machine-level mutual exclusion on `/tmp/chirp_bench.lock` (`LOCK_WAIT=1` queues, `LOCK_TIMEOUT` defaults to 3600s); `bench_preflight_ports` checks each port for a listener, passes idle ones through, and when occupied only cleans up an orphan that is "this uid + the benchmark binary" under `FORCE=1`, otherwise reporting an error and exiting (shared-machine discipline: never kill another session's live process on its behalf). Port blocks per probe: steady 17880-83, scale 17850-53, perf 17840-43, est_rate 17870-73, stack 17830-33, cpu_attribution 17810-13, run_capacity_bench 17800-03 (`CHAT_PORT` etc. can be overridden). Ladder drivers (ladder_local / capacity_ladder_local / capacity_ramp_ladder) do not take the lock again — each tier's inner runner carries its own guard, and taking the same flock at the outer level would self-deadlock.
- Each tier starts and stops the services independently; the user prefix rotates per tier, avoiding cross-tier rate-limit/mute residue.

Reproduce (example):

```bash
REDIS_HOST=127.0.0.1 REDIS_PORT=16379 MYSQL_HOST=127.0.0.1 MYSQL_PORT=13306 \
BIN_DIR=./build-rel bash tools/benchmark/run_capacity_bench.sh 12000 0 1100 60000
```

## Measured data

### Connection-capacity tier (rounds=0, connect only, no messages sent)

| Tier | Connection rate | Online | Handshake timeouts | Login RTT p50/p99/max | chat peak RSS | gateway peak RSS | chat CPU core-seconds | env loadavg |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 500 | 8.3 conn/s | 500/500 | 421 | 3/7/11 ms | 38.0MB | 9.1MB | 22.03 (60s) | ~59 |
| 1000 | 16.7 conn/s | 1000/1000 | 843 | 4/9/17 ms | 37.5MB | 9.9MB | 18.66 (60s) | ~59 |
| 2000 | 33.3 conn/s | 2000/2000 | 1714 | 4/10/63 ms | 36.5MB | 11.0MB | 13.61 (60s) | ~84 |
| **12000** | 200 conn/s | **12000/12000** | **10636** | 3/17/54 ms | **73.0MB** | **24.5MB** | 30.68 (63.5s) | ~73 |

> **Note**: this round used `ramp_ms=60000` to spread the 12k connections over 60 seconds, avoiding a same-instant hammer against the handshake limit. The `handshake_timeouts` count arises from the interaction between the spread-out ramp window and the 5s handshake timeout. `chat CPU core-seconds` is the cumulative delta during the ctest window. The environmental loadavg is affected by parallel builds in multiple sessions.

> **Contrast**: the numbers in the earlier table came from an old build with no MySQL/Redis dependencies; this table is a measurement of the enhanced chat (HybridMessageStore + MySQL) on a shared 14-core development machine, with load shared across sessions.

Key points:

- Connecting is itself cheap: 12000 connections cost chat only 8.24 core-seconds (≈130µs/connection, including pipe handshake and session state).
- Memory barely grows with connection count (chat 15.7→19.7MB, gateway flat at 8.6MB): per-connection state is on the order of a few tens of KB, and 12k connections are nowhere near the memory boundary.

### Steady-state send tiers

(See the table below; both tiers were taken on a shared machine at loadavg≈60, and RTT includes self-interference from the client's 500/1000 threads, so **treat these as order-of-magnitude reference only**.)

| Tier | Surviving senders | Handshake timeouts (connection phase) | Steady round p50 | Steady round p99 | Window throughput |
| --- | --- | --- | --- | --- | --- |
| steady500 (spread at 50/s + sending during the ramp) | 335/500 | 165 | 112-173 ms | 465-546 ms | 82.9 msg/s |
| steady1000 (same) | 536/1000 | 464 | 112-164 ms | 535-749 ms | 73.4 msg/s |
| Quiet-machine reference (200 connections, loadavg≈8, older data from the shared-dependency era) | 200/200 | 0 | **2-3 ms** | 5-8 ms | 141 msg/s |

Why steady-state window throughput is far below the pacing ceiling (500/1.1≈455 msg/s): client rounds are not pipelined (wait for the response, then sleep 1.1s, so the period = RTT+1100ms), ramp spread counts into the window, and machine load pushes RTT up. **Server-side pacing (private 1s per user, hard-coded) is the structural throughput ceiling**: the round-robin mutual-send floor ≈ conns/1.1s, matching the measurements (e.g. 64 connections @1100ms → 58.25 msg/s ≈ 64/1.1).

## Bottlenecks and boundaries

1. **chat's single io thread serializes connection handling with message handling**. Pure connecting is 200/s all-green; but when the connection ramp overlaps with message sending (steady500/1000 tiers), message handling crowds out the io thread and the pipe handshake's 5s hard deadline is breached (165/500 timeouts even at 50 conn/s). Production implication: **a login storm during peak hours and message traffic amplify each other**. Mitigation directions: make the pipe handshake timeout configurable, and split handshakes from message handling onto separate threads / separate priorities.
2. **Barrier synchronization storms** (`--barrier on`): after everyone assembles, rounds are sent in phase-lock, so with 2k connections every round is 2000 simultaneous sends, and the RTT tail reaches 60s (queue drain time). This is a measurement-method pitfall to avoid, and it is also the expected behavior of a real "everyone sends at the same instant" scenario.
3. **Throughput structural ceiling = pacing**: private 1s per user, hard-coded (`ChannelPacer`, in-process); in a multi-instance deployment each instance counts independently; the Redis fuzzy gate (120/min per user) and the login IP rate limit only take effect with `--redis_host`.
4. ~~**`RedisClient` opens a new TCP short connection per command**~~ **A synchronous connection pool has landed (2026-10-11)**: `SendCmd`'s per-command short connections were replaced by a reused idle connection pool (cap `kDefaultMaxIdle=8`, a liveness probe on take, a failed connection is discarded without retry), so the RPush on the message path no longer pays the connect+close handshake every time. This is the throughput-boosting optimization this benchmark called out; async-ification/coroutine-ification is a separate project per the criteria in `asio_coroutine_adoption.md`. Pipe reuse is still not done.
5. **MySQL must be configured correctly**: archiving is a single background-thread attempt that only warns on failure with no retry (`StoreMessageAsync`), so a bad dependency desynchronizes the hot/cold history tiers and slows responses (see [Dependency shape](#dependency-shape-critical); for the delivery-contract criterion see [special audit](/审计-专项审查-投递认证与状态一致性.en) D2).
6. The client is a thread/connection model: tens of thousands of threads contend for cores and push RTT up on their own (a polled barrier was once measured to drive loadavg to 1181), hence taskset core pinning + a condition-variable barrier; above tens of thousands, switch to an event-driven client before measuring.

## Limitations and applicability

- **Shared 14-core development machine**: the environmental loadavg is recorded per tier (55-108); RTT/throughput numbers carry inseparable neighbor noise; structural numbers such as connection capacity, handshake timeouts, RSS and CPU core-seconds are unaffected.
- Loopback network: RTT excludes real network RTT; throughput excludes NIC bandwidth constraints.
- Single-instance chat/gateway: multi-instance horizontal scaling (global Redis rate limiting) is untested.
- The scaffold login path (no real auth service): login RTT for the real authentication chain is untested.
- The client send surface covers private chat only (round-robin mutual sends): group-broadcast fan-out and offline-queue delivery are not part of this benchmark.

## Related

- [roadmap_history](/design-notes/roadmap_history) § operations and testing "load test" item backfill
- [SCALABILITY](/design-notes/SCALABILITY) / [game_chat_architecture](/design-notes/game_chat_architecture)
- Tooling reference: the local README in [tools/benchmark](https://github.com/) (the `chirp_load_client` section)
