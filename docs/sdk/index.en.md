> 🌐 Language: [中文](./index.md) | **English** (this page)

# SDK Overview

Chirp's integration surface is a pure protocol (TCP/WebSocket + small Protobuf frames), and the SDKs are layered as "protocol core + thin engine shell": **the protocol logic is implemented exactly once per language** (C++ core / Unity C# / TS / Go, one reference implementation each), and engine integration is either a shell (Unreal) or a direct reuse of an existing protocol stack (Cocos/LayaAir/WeChat Mini Game/Godot). This page is the index and testing baseline for all existing SDKs; each row's documentation page has usage and integration details.

**Boundary rule (2026-10-02)**: `sdks/` is the **game** integration surface; the companion/chat app does not depend on `sdks/` — the app-side protocol layer lives inside `apps/` (TS: `apps/shared/protocol` `@chirp/app-protocol`, Android: `apps/android/.../protocol/`, iOS: `apps/ios/Sources/ChirpProtocol/`), locking the same semantics against the game SDKs with the same set of test vectors.

## Official SDKs (implemented and tested in-repo)

| SDK | Location | Shape | Test and coverage status | Docs |
| --- | --- | --- | --- | --- |
| **C++ protocol core** | `sdks/core` | Client protocol stack (connection/heartbeat/backoff reconnect/kick terminal state/hooks), consumed by the Unreal shell; the desktop app does not depend on `sdks/` (the app protocol layer lives in `apps/shared/protocol`, see the boundary rule above) | `sdk_core_tests` 130 cases (10 suites, including 59 `ChatClientLoopback` cases), run via ctest; coverage is governed by the main C++ gate (`run_coverage.sh` 100% line coverage hard gate) | [Unreal](/sdk/unreal) (consumer)<br>[sdks/core/README](https://github.com/cuihairu/chirp/tree/main/sdks/core) |
| **Unity (C#)** | `sdks/unity` | Pure C# protocol stack (zero UnityEngine dependency) + a `ChirpManager` main-thread dispatch shell | `dotnet test` 141 cases (frame codec, sequence correlation, reconnect/kick, hook wiring, FileMessageStore, and a loopback over a real WebSocket transport); CI `unity-sdk.yml` (.NET 10 + proto/csharp drift check); coverlet line coverage **98.5%** (proto generated code excluded, deduplicated by file lines, 1739/1766) | [Unity3D](/sdk/unity3d) |
| **TypeScript** | `sdks/ts` (`@chirp/protocol`) | TS protocol stack, the integration surface for game-side JS/TS engines (mini games/LayaAir/Cocos); includes the `wx.connectSocket` adapter | vitest 149 cases (10 files, including the wx_socket fake socket and a real login round-trip); CI `web.yml`; vitest line coverage gated by the `vitest.config.ts` thresholds (lines/branches/functions/statements ≥90%) | [WeChat Mini Game](/sdk/wechat-minigame)<br>[Cocos Creator](/sdk/cocos-creator)<br>[LayaAir](/sdk/layaair) |
| **Go (server)** | `sdks/go` | Server-plane reference client (dial-out, service_id+secret, injection/events/player binding/device presence) | `go test -race` 33 cases (in-process fake hub loopback); CI `go-sdk.yml` (protoc 33.4 regeneration drift guard + vet + -race); client.go statement coverage **100%** | [Server SDK](/sdk/server) |
| **Unreal plugin shell** | `sdks/unreal` | A thin `UChirpClientSubsystem` shell (619 lines): native callbacks marshaled to the game thread + Blueprint exposure, all protocol work in the C++ core | No CI compile slot (UCLASS/Build.cs need UBT and engine headers, which chirp CI does not have); the correctness contract is the C++ core's `sdk_core_tests` 130 cases + a very thin forwarding layer | [Unreal](/sdk/unreal) |

## Choosing an integration path by engine

| Engine / environment | How to integrate | Basis |
| --- | --- | --- |
| Unity 2021.2+ | The `sdks/unity` pure C# protocol stack + `ChirpManager` | Official SDK, covering the chat/social/voice/party message planes |
| Unreal Engine | The `sdks/unreal` plugin, linking `libchirp_core_sdk` | C++ core shell |
| Godot 4.x (.NET) | Compile the `sdks/unity/Runtime/Chirp/` sources directly (zero engine dependency by design) | Reuses the Unity C# protocol stack; `ClientWebSocket` is a .NET standard library |
| Cocos Creator 3.x | Copy the `sdks/ts/src/` protocol stack; connect to the WS edge with the engine's `WebSocket` | Pure protocol, engine-independent |
| LayaAir 3.x | Same as Cocos (same shape) | Same as above |
| WeChat Mini Game | `@chirp/protocol` + the `adapters/wx_socket` transport factory | No TCP, must use the WS edge |
| Game backend (any language) | The Go SDK, or a direct implementation of `game_server_gateway.proto` (Node/Lua/Python key points on the server page) | Server-plane dial-out |

Selection principle: **first look at the engine's language** (C# -> the Unity stack; TS/JS -> sdks/ts; C++ -> core/Unreal), then check each documentation page's port topology (chat WS 7001 / TCP 5000, server_gateway TCP 8100).

## Nightly builds

The main branch produces an unsigned SDK artifact bundle once a day (C++ core library + Go SDK + TS SDK + desktop app .deb, Linux x86_64/aarch64), **distributed two ways, both without git tags and without GitHub Releases**:

- **One-line install** (recommended): the repo-root `install.sh` (Linux/macOS) / `install.ps1` (Windows) picks the right platform artifacts automatically — see the "one-line install" section of the README;
- **Manual**: the run page -> **Artifacts -> `daily-build`**; or the [nightly-dist branch mirror](https://github.com/cuihairu/chirp/tree/nightly-dist) (fixed file names + a `manifest.json` availability list, anonymously direct-linkable, the download source for one-line installs).

The artifacts are unsigned and intended for integration testing only; do not take them straight to production. See the "nightly builds" section of each SDK documentation page and the [nightly workflow](https://github.com/cuihairu/chirp/blob/main/.github/workflows/nightly.yml).

## Testing notes

- The per-SDK numbers reflect the current repo; commands: `dotnet test sdks/unity/dotnet/ChirpSdkTests`, `go test -race ./sdks/go/...`, `pnpm exec vitest run` under `sdks/ts`, `ctest -R sdk_core_tests`;
- Coverage basis: C++/Go/TS use their own CI's native coverage pipelines; Unity uses coverlet cobertura, **excluding `proto/csharp` generated code and deduplicated by file lines** (98.5%, 1739/1766);
- The Unreal shell has no separate test target: its behavioral contract is the native core's behavioral contract (no protocol logic in the forwarding layer).
