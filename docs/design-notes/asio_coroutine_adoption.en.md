> 🌐 Language: [中文](./asio_coroutine_adoption.md) | **English** (this page)

# ASIO coroutines: should we, when, and how

> Status: technical decision analysis (2026-09). The conclusion is "no wholesale rewrite, adopt for new modules", with the reasoning below; if the evidence changes, re-evaluate using the decision table at the end. Capability boundaries are defined by [CAPABILITY_MATRIX](../CAPABILITY_MATRIX.en.md).

## TL;DR

1. Premise correction: asio's support for C++20 coroutines did not start at 1.36 — the native coroutine facilities `awaitable`/`co_spawn`/`use_awaitable` have been available since **asio 1.16 (late 2019, corresponding to Boost.Asio 1.70)**, and before that there were TS-style stackless pseudo-coroutines (`asio::coroutine` + `yield`).
2. This project already uses a version that supports coroutines: **asio 1.32.0** provided by vcpkg (standalone mode), compile standard **C++23** (`CMakeLists.txt:4`), with the GCC toolchain having complete native coroutine support. Enabling coroutines requires no dependency upgrade at all.
3. What coroutines mainly improve is code shape, not throughput: turning callbacks into coroutines does not make message forwarding faster. What genuinely delivers a latency win is converting "blocking calls on the io thread" into real async — that is complementary to coroutines (coroutines make async code read like sync), not the same thing.
4. Recommendation: no big-bang rewrite. The callback baseline already has 100% line coverage, shallow nesting, and no performance evidence; but **newly written multi-step flows** (the device-level session core, migration-worker style tasks, distributed routing) should be written with `co_await`, to avoid creating deep callback state machines.

## 1. Fact check: the asio coroutines timeline

| Capability | When it entered asio | Notes |
| --- | --- | --- |
| stackless pseudo-coroutines (`asio::coroutine` + `yield_context`) | asio ~1.12 (the Boost.Asio 1.66 era) | "Pseudo-coroutines" built on C++98 tricks, with no language-level support |
| **C++20 native coroutines** (`awaitable`/`co_spawn`/`use_awaitable`/`this_coro`) | **asio 1.16 (2019-12, Boost.Asio 1.70)** | Language-level stackless coroutines, today's mainstream style |
| `asio::experimental::coro` (value-carrying, channel-style coroutines) | asio 1.20+ | Experimental, later graduated |
| asio 1.30 / 1.32 (what this project uses) | 2024 | The coroutine facilities have been stable for years |

So the question was never "is the version new enough", but "is it worth changing".

## 2. Two coroutine routes: don't misquote WeChat

Any discussion of "WeChat servers use coroutines heavily" must distinguish two technical routes:

**Stackful coroutines** — the open-sourced [libco](https://github.com/Tencent/libco) by WeChat is of this kind. Every coroutine holds its own (or a shared) call stack; suspension/resumption uses assembly-level context switching, **suspension can happen inside any function**, and business code can take a fully synchronous shape (including blocking-style socket APIs — libco achieves "looks blocking, actually yields" by hooking syscalls). WeChat's scenario is massive long connections + complex synchronous-style business logic; this solution captured "concurrency at billion-connection scale" and "maintainability of synchronous code" at the same time.

**Stackless coroutines** — C++20 coroutines and asio `awaitable` are of this kind. The coroutine frame is allocated on the heap at the maximum local state required by the suspension points; suspension can only happen at a `co_await` and can only propagate across coroutine functions. Memory use is small and the switch cost is extremely low, but the "synchronization" only covers the inside of a coroutine chain — you can't just suspend inside an ordinary function.

Chirp uses asio standalone, so the natural route is **C++20 stackless coroutines**. Copying libco (introducing a stackful coroutine library) means building your own scheduler integration, a hook layer, and a context-switching mechanism independent of asio — paying real cost for "code that looks like WeChat's", while the constraints that led WeChat to libco (massive connections, no C++20 in 2013, a fully synchronous legacy codebase) do not all hold here.

## 3. The performance truth: what coroutines improve, and what they don't

### What they don't improve

- **Single-hop I/O throughput and latency**. asio's event loop ready queue + callbacks is essentially the same as co_spawn + co_await: one state machine, one resumption. The heap allocation of a coroutine frame (once per coroutine) and the capture in a callback object (once per hop) are the same order of magnitude; benchmarks flip either way, and it is never an order-of-magnitude difference.
- **Connection ceiling**. There is no qualitative change in the memory model: lambda captures in a callback chain vs coroutine frames — both are constant per connection.

### What they genuinely improve

- **Writableness of deeply nested flows**. Every extra async step adds a level of callback nesting plus a hand-carried state object; a coroutine writes n steps as n sequential lines, and error handling becomes `try/catch` instead of "check and forward the error code at every level".
- **Propagation of cancellation and timeout**. asio's `cancellation_slot`/`cancellation_state` propagate automatically through a coroutine chain; in a callback chain every link has to be wired by hand.
- **Blocking calls on the io thread can be "casually" eliminated**. This one matters most for chirp; see section 4.

In one sentence: **coroutines are the tool that makes a "correct async architecture" maintainable; they do not produce performance themselves.**

## 4. Against chirp's current state

### 4.1 Current shape

- The edge/session layer has shallow callback nesting: `libs/network/tcp_session.cc`'s `DoRead`→`on_frame`→`DoWrite` is a simple "read a frame → callback → write back" chain, with little state and clear strand protection. Converting this kind of code to coroutines has **near-zero benefit**, and would touch a 100%-covered baseline.
- Fire-and-forget is already the status quo: chat's NPC upstream (`SendEventPublish(..., lambda)`) is already "send it out, log the failure". Coroutines wouldn't make it faster, only slightly more readable.
- The genuinely deep, state-machine-heavy spots: `libs/network/server_gateway_peer.cc` (the reconnect/auth/heartbeat state machine), `message_migration_worker` (a multi-step migration flow). These are the **high-yield zone** for coroutines, but they currently work fine and are covered.
- One fact larger than "callbacks vs coroutines": every command in `libs/network/redis_client.cc` is a **synchronous blocking implementation** (resolve → connect → read, with `SendCmd` opening a new connection each time). When it runs on the io thread, a single Redis round trip **blocks the scheduling of every connection on that thread**. This is the most practical latency amplifier in the current architecture — bigger than any code-style issue. The correct fix is a truly asynchronous Redis client, and **coroutines are exactly the means that keeps the code using an async Redis client readable**. In other words: if we ever do Redis async-ification, the coroutine payoff materializes on its own; doing "convert callbacks to coroutines" alone would not.

### 4.2 Before/after (using a "fetch history [page-fill]" style flow)

Current style (one hop per step, state carried by hand):

```cpp
void Retriever::Start(std::string channel) {
  store_->GetHistoryAsync(channel, [self](auto page) {
    self->page_ = page;
    self->archive_->FetchOlder(channel, page.oldest(), [self](auto older) {
      if (older.empty()) { self->Finish(); return; }
      self->Merge(older);
      self->archive_->FetchOlder(channel, /* one more level… */);
    });
  });
}
```

Coroutine style (the same logic):

```cpp
asio::awaitable<void> Retriever::Run(std::string channel) {
  auto page = co_await store_->GetHistoryAsync(channel);
  for (;;) {
    auto older = co_await archive_->FetchOlder(channel, page.oldest());
    if (older.empty()) break;
    Merge(page, older);
  }
  Finish();
}
// Entry point: asio::co_spawn(strand_, retriever->Run(channel), asio::detached);
```

The compiler generates that state machine for us, and exceptions, cancellation and timeouts can all propagate along the chain.

## 5. Pros and cons checklist (for this project)

### Benefits of adopting

1. Multi-step async flows return to sequential expression, eliminating deep callback nesting (migration worker, hub peer reconnect, the future device-level session core).
2. `try/catch` unifies error paths, replacing "check and forward the error code at every level".
3. Cancellation/timeout propagates automatically through the coroutine chain, with no hand-wiring in each callback.
4. Prepares a maintainable style for future async Redis/push transport (true async I/O).
5. Consistent with the direction of the C++23 standard library/ecosystem; new code no longer accumulates "historical style debt".

### Costs and risks of adopting

1. **Rewrite risk out of proportion to the reward**: the existing callback code has a 100% line-coverage baseline, and a style migration fixes no bug while introducing equivalence-verification cost.
2. **Mental barrier**: the lifetime traps of coroutines (coroutine frames holding dangling references, `co_spawn` ownership, `this` becoming invalid in a detached coroutine) are more insidious than callback capture traps, and the team/future contributors need one round of learning.
3. **A different stack/memory model**: a coroutine frame is allocated at the maximum local state across suspension points — a large buffer accidentally left alive across a suspension amplifies per-coroutine memory; per-hop allocations in a callback chain are instead more "what you see is what you get".
4. **Danger of mixing blocking calls into a coroutine chain**: one blocking call (such as today's synchronous `RedisClient`) inside a `co_await` chain still blocks the io thread, and because it looks "async" it is harder for review to spot.
5. **Debugging experience**: coroutine backtraces depend on the toolchain version (new GCC has `-g` coroutine-frame awareness, older versions produce incomplete frame stitching).
6. **Not directly comparable to the WeChat route**: libco's "any function can suspend + synchronous APIs used directly" pleasure cannot be given by C++20 stackless coroutines; citing WeChat as grounds for "switch to awaitable" is quoting the wrong lesson.

## 6. Recommendation

1. **No wholesale rewrite**. There is no performance evidence today (the capacity benchmark is itself a deferred TODO item), the callback baseline is 100% covered, and a rewrite only buys style.
2. **Default new modules to coroutines**. The P1 device-level session core and any new multi-step async flow (real push transport, distributed routing enhancements) should be written with `co_await`, entering via `asio::co_spawn`, keeping the strand discipline (one strand per session, coroutines spawned onto the strand).
3. **Bundle coroutine adoption with Redis async-ification**. Whichever day we replace the synchronous `RedisClient` with a real async one, coroutine-ify the flows using it at the same time — that is when the coroutine payoff (maintainable async) actually materializes. A standalone "style migration" should not be scheduled.
4. **Conventions first**: if we start writing coroutines, add an internal convention at the same time (ownership of co_spawn / detached usage limits, no buffers in coroutine frames, no direct blocking I/O calls inside a coroutine chain), placed in a header comment under `libs/network` or in docs.

## 7. Triggers for re-evaluation

Any of the following should prompt a re-examination of this document's conclusion:

- A load test (TODO P3 capacity benchmark) shows the callback chain / blocking Redis is a measurable latency bottleneck;
- The P1 device-level session core's flow diagram becomes complex enough that callback nesting is ≥ 4 levels and hand-carried state objects are ≥ 3;
- Redis async-ification or real push transport (APNs HTTP/2) is scheduled — both should naturally be written with coroutines;
- An asio upgrade brings significant improvement to multithreaded `co_await` affinity/scheduling, lowering migration cost.
