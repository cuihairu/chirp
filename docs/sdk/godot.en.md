> 🌐 Language: [中文](./godot.md) | **English** (this page)

# Godot

**There is no official SDK for Godot yet.** Godot 4.x's .NET (C#) runtime can reuse the **pure C# portion** of the [Unity SDK](/sdk/unity3d) directly — `sdks/unity/Runtime/Chirp/` was designed with zero engine dependencies from the start (`Chirp.Sdk.asmdef`'s `noEngineReferences` hardens that constraint into a compile-time check on the Unity side), and the transport layer uses `System.Net.WebSockets.ClientWebSocket`, a .NET standard library that Godot .NET ships with, requiring no adapter code at all.

There is no GDScript route: `GDScript` lacks a mature protobuf library, so reusing C# is the only realistic path.

## How to integrate

Taking a Godot 4.3+ .NET project as an example, bring the Unity SDK's pure C# sources into the project:

```xml
<!-- Your .csproj: either compile-reference the source files directly
     (following repo updates), or precompile them into a class library dll
     and reference it with <Reference> - pick one -->
<ItemGroup>
  <Compile Include="../../sdks/unity/Runtime/Chirp/**/*.cs" />
</ItemGroup>
<ItemGroup>
  <PackageReference Include="Google.Protobuf" Version="3.27.0" />
</ItemGroup>
```

After that the usage is the same as on the Unity side: `ChirpClient` + the five hook interfaces (MessageInterceptor / AuthProvider / MessageStore / ChatEventListener / CommandHandler in `ChirpHooks.cs`), with the login round trip, heartbeat, reconnect and KICK terminal-state semantics identical to what the unity3d documentation describes. This C# source's regression suite (`ChirpSdkTests`, 141 cases, 98.5% coverlet runtime line coverage) depends on no engine, so `dotnet test` can rerun it outside a Godot project.

## Bridging to Godot nodes (pattern sketch)

The SDK's callbacks all happen on its internal thread; when bridging to the Godot scene tree, remember to switch back to the main thread with `CallDeferred` / `SynchronizationContext.Post` before emitting a Godot signal:

```csharp
// Pattern sketch (not part of the repo's tested surface): forward a C# listener callback as a Godot signal
public partial class ChatBridge : Node, IChatEventListener
{
    [Signal] public delegate void MessageReceivedEventHandler(string text);

    private readonly ChirpClient _client =
        new("ws://127.0.0.1:7001");   // the default transport is the System.Net.WebSockets adapter

    public override void _Ready()
    {
        _client.AddListener(this);
    }

    // IChatEventListener: the SDK calls back on an internal thread, switch to the main thread before emitting
    public void OnMessageReceived(Chirp.Chat.ChatMessage message)
    {
        var text = message.Content.ToStringUtf8();
        CallDeferred(MethodName.EmitSignal, SignalName.MessageReceived, text);
    }
}
```

The official ChatClient node wrapper (built-in signal forwarding, instantiable from a scene) is still on the to-do list and will be delivered after verification against a real Godot project.

## Platform boundaries

- **Desktop/mobile exports**: the reuse path is the same as Unity (Mono/IL2CPP both derive from the .NET standard library).
- **Web export**: **unavailable**, exactly as with Unity WebGL — `System.Net.WebSockets` cannot work inside the browser sandbox; for a web target use the TS route of [Cocos/LayaAir](/sdk/cocos-creator).
- For the semantics of heartbeat interval, reconnect backoff and similar parameters, see [API Overview](/api/overview).

## Protocol / API reference

- Full message ID table: the `MsgID` enum in `proto/gateway.proto`
- C# semantics comparison (message-plane coverage table): [Unity3D](/sdk/unity3d)
- Server integration (game backend): [Server SDK](/sdk/server)
