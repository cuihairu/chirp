> 🌐 Language: [中文](./index.md) | **English** (this page)

# SDK Overview

Chirp's integration surface is a pure protocol (TCP/WebSocket + small Protobuf frames), and the SDKs are layered as "protocol core + thin engine shell": **the protocol logic is implemented exactly once per language** (C++ core / Unity C# / TS / Go, one reference implementation each), and engine integration is either a shell (Unreal) or a direct reuse of an existing protocol stack (Cocos/LayaAir/WeChat Mini Game/Godot). This page is the index and testing baseline for all existing SDKs; each row's documentation page has usage and integration details.

**Boundary rule (2026-10-02)**:  is the **game** integration surface; the companion/chat app does not depend on  — the app-side protocol layer lives inside  (TS:  , Android: , iOS: ), locking the same semantics against the game SDKs with the same set of test vectors.

## Official SDKs (implemented and tested in-repo)

| SDK | Location | Shape | Test and coverage status | Docs |
| --- | --- | --- | --- | --- |
| **C++ protocol core** |  | Client protocol stack (connection/heartbeat/backoff reconnect/kick terminal state/hooks), consumed by the Unreal shell; the desktop app does not depend on  (the app protocol layer lives in , see the boundary rule above) |  130 cases (10 suites, including 59  cases), run via ctest; coverage is governed by the main C++ gate ( 100% line coverage hard gate) | [Unreal](sdk/unreal.en) (consumer)<br>[sdks/core/README](https://github.com/cuihairu/chirp/tree/main/sdks/core) |
| **Unity (C#)** |  | Pure C# protocol stack (zero UnityEngine dependency) + a  main-thread dispatch shell |  107 cases (frame codec, sequence correlation, reconnect/kick, hook wiring, FileMessageStore, and a loopback over a real WebSocket transport); CI  (.NET 10 + proto/csharp drift check); coverlet line coverage **89.2%** (proto generated code excluded, deduplicated by file lines, 1436/1609) | [Unity3D](sdk/unity3d.en) |
| **TypeScript** |  () | TS protocol stack, the integration surface for game-side JS/TS engines (mini games/LayaAir/Cocos); includes the  adapter | vitest 131 cases (9 files, including the wx_socket fake socket and a real login round-trip); CI ; vitest line coverage 99.8% | [WeChat Mini Game](sdk/wechat-minigame.en)<br>[Cocos Creator](sdk/cocos-creator.en)<br>[LayaAir](sdk/layaair.en) |
| **Go (server)** |  | Server-plane reference client (dial-out, service_id+secret, injection/events/player binding) |  33 cases (in-process fake hub loopback); CI  (protoc 33.4 regeneration drift guard + vet + -race); client.go statement coverage **100%** | [Server SDK](/sdk/server) |
| **Unreal plugin shell** |  | A thin  shell (619 lines): native callbacks marshaled to the game thread + Blueprint exposure, all protocol work in the C++ core | No CI compile slot (UCLASS/Build.cs need UBT and engine headers, which chirp CI does not have); the correctness contract is the C++ core's  130 cases + a very thin forwarding layer | [Unreal](/sdk/unreal) |

## Choosing an integration path by engine

| Engine / environment | How to integrate | Basis |
| --- | --- | --- |
| Unity 2021.2+ | The  pure C# protocol stack +  | Official SDK, covering the chat/social/voice/party message planes |
| Unreal Engine | The  plugin, linking  | C++ core shell |
| Godot 4.x (.NET) | Compile the  sources directly (zero engine dependency by design) | Reuses the Unity C# protocol stack;  is a .NET standard library |
| Cocos Creator 3.x | Copy the  protocol stack; connect to the WS edge with the engine's  | Pure protocol, engine-independent |
| LayaAir 3.x | Same as Cocos (same shape) | Same as above |
| WeChat Mini Game |  + the  transport factory | No TCP, must use the WS edge |
| Game backend (any language) | The Go SDK, or a direct implementation of  (Node/Lua/Python key points on the server page) | Server-plane dial-out |

Selection principle: **first look at the engine's language** (C# -> the Unity stack; TS/JS -> sdks/ts; C++ -> core/Unreal), then check each documentation page's port topology (chat WS 7001 / TCP 5000, server_gateway TCP 8100).

## Nightly builds

The main branch produces an unsigned SDK artifact bundle once a day (C++ core library + Go SDK + TS SDK + desktop app .deb, Linux x86_64/aarch64), **distributed two ways, both without git tags and without GitHub Releases**:

- **One-line install** (recommended): the repo-root  (Linux/macOS) /  (Windows) picks the right platform artifacts automatically — see the "one-line install" section of the README;
- **Manual**: the run page -> **Artifacts -> **; or the [nightly-dist branch mirror](https://github.com/cuihairu/chirp/tree/nightly-dist) (fixed file names + a  availability list, anonymously direct-linkable, the download source for one-line installs).

The artifacts are unsigned and intended for integration testing only; do not take them straight to production. See the "nightly builds" section of each SDK documentation page and the [nightly workflow](https://github.com/cuihairu/chirp/blob/main/.github/workflows/nightly.yml).

## Testing notes

- The per-SDK numbers reflect the current repo; commands: , ,  under , ;
- Coverage basis: C++/Go/TS use their own CI's native coverage pipelines; Unity uses coverlet cobertura, **excluding  generated code and deduplicated by file lines** (89.2%, 1436/1609);
- The Unreal shell has no separate test target: its behavioral contract is the native core's behavioral contract (no protocol logic in the forwarding layer).
