> 🌐 Language: [中文](./unreal.md) | **English** (this page)

# Unreal Engine

The official Unreal support takes the form of **plugin shell + native core**: `sdks/unreal` provides `UChirpClientSubsystem` (a GameInstance Subsystem) and Blueprint-exposed events, while all protocol logic (frames, heartbeat, exponential-backoff reconnect, KICK terminal state, request timeouts) lives in the native core `chirp::sdk::ChatClient` (`sdks/core`; the desktop app does not depend on `sdks/`, its app protocol layer lives in `apps/shared/protocol`) and is covered by chirp CI's `sdk_core_tests` (130 cases, 59 of them loopback, in `tests/unit/sdk_core_test.cc`; run via ctest with the 100% line coverage gate).

The plugin does only two things:

1. **Game-thread dispatch**: native callbacks fire on the SDK's internal io thread, and the Subsystem forwards them with `AsyncTask(ENamedThreads::GameThread, ...)` — so callbacks can touch `UWorld`/`UWidget` directly;
2. **Blueprint exposure**: a connection-state enum, bindable events, and BlueprintCallable methods.

See [sdks/unreal/README.md](https://github.com/cuihairu/chirp/tree/main/sdks/unreal) for the complete source.

## How to integrate

1. **Build the native core** (on the host machine, one-time):

   ```bash
   cd <chirp repo>
   cmake -B build -DCMAKE_BUILD_TYPE=Release -DENABLE_TESTS=ON \
         -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake -G Ninja
   cmake --build build --target chirp_core_sdk_static
   ```

2. **Copy the plugin**: copy the whole `sdks/unreal/` directory into the project's `Plugins/ChirpSDK/` (or install it as an engine plugin).
3. **Point at the native artifacts**: set the environment variable `CHIRP_SDK_NATIVE_DIR=<chirp repo path>` before launching UBT (`ChirpSDK.Build.cs` checks that directory and links `libchirp_core_sdk.a`; on Windows it links `chirp_core_sdk.lib` and adds the protobuf/abseil import libraries to `PublicAdditionalLibraries`).
4. Enable the ChirpSDK plugin in the editor and call `Get Chirp Client` (GameInstance Subsystem) from Blueprint.

> The Unreal-side code cannot be compiled in chirp's CI (it needs UBT/engine headers); its correctness contract is the native core's unit tests + a very thin forwarding layer. If the first compile after UE integration hits API version differences, just adjust per the errors.

## Lifecycle

```text
Connect("127.0.0.1", 5000) → Login("user-42") → event callbacks (game thread)
        ↓ disconnect (the core auto-reconnects with exponential backoff)
OnDisconnected → (reconnect succeeds) → Login again
        ↓ KICK received
OnKicked: terminal state, the core no longer auto-reconnects, requires Connect + Login again
```

Key points:

- **There is no OnConnected event** (deliberate): poll `GetState()` or wait for `OnLoginResult`.
- **KICK is terminal**: the core no longer auto-reconnects, `GetState() == Kicked`, and you need to `Connect` + `Login` again.
- **Ports**: the development-topology game-plane gateway (game_sdk_gateway) is TCP **5000**; WS 5001 is the same gateway's WS edge, which web/Unity do not use (they connect to chat WS 7001). Unreal uses TCP frames — the frame protocol is identical to the WS message body.
- All events are on the game thread.

## Blueprint usage

```text
Get Chirp Client
├─ OnChatMessage   → (Sender, Content)  real-time private chat push
├─ OnKicked        → (Reason)           replaced, terminal
├─ OnDisconnected  → (Reason)           disconnected (the core auto-reconnects)
├─ OnLoginResult   → (UserId)           login round-trip result, empty on failure
└─ WatchNotify(6001) → OnRawNotify(MsgId, Body)  generic notify subscription

Connect("127.0.0.1", 5000)
Login("user-42")          // in scaffold mode the token is the user id
SendChatMessage("peer-7", "hello")
```

`WatchNotify` is the generic escape hatch: pushes for any `MsgID` (chat 2005, device 6xxx, etc.) can be subscribed, and you parse the raw body yourself.

## Protocol / API reference

- Protocol overview: [API Overview](/api/overview) (Packet frames, message ID ranges, core flows)
- C++ core header: `sdks/core/include/chirp/sdk_client.h` (the `ChatClient` public API surface)
- Hook interfaces (interceptor/command/storage/auth/listener): [SDK Hooks](/design-notes/sdk_hooks)
- Engine compatibility checklist: [SDK Engine Compatibility](/design-notes/sdk_compatibility)
- Server integration (game backend): [Server SDK](/sdk/server) and [Server Plane](/server_plane)

## Current state and boundaries

- The Unreal plugin shell consumes the native core (`sdks/core`, 140 unit tests); the desktop app does not depend on `sdks/`, its app protocol layer is in `apps/shared/protocol` (`@chirp/app-protocol`). The other client implementations are Unity C# (141 cases) and the TS protocol stack (web/mini games/Cocos/Laya/Godot via C#, 149 cases), with mobile as native twin packages (Android Kotlin / iOS Swift shells). Semantics across clients (sequence correlation, heartbeat, backoff, kick) are aligned one by one; the test matrix is in [sdks/core/README.md](https://github.com/cuihairu/chirp/tree/main/sdks/core).
- Nightly builds: the Actions nightly `daily-build` artifact bundle includes `libchirp_core_sdk` multi-platform libraries and headers (the native artifacts needed by steps 1/3 on this page, unsigned); see [SDK Overview](/sdk/) for the entry point.
- The voice/party/social planes have no ready-made Unreal wrappers yet; extend them yourself via the native core's generic request interface or `WatchNotify`.
