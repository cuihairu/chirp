---
title: Godot 接入
---

# Godot

**Godot 暂无官方 SDK**。Godot 4.x 的 .NET(C#)运行时可以直接复用 [Unity SDK](/sdk/unity3d) 的**纯 C# 部分**——`sdks/unity/Runtime/Chirp/` 从一开始就按零引擎依赖设计(`Chirp.Sdk.asmdef` 的 `noEngineReferences` 在 Unity 侧把这条约束固化成编译期检查),传输层用 `System.Net.WebSockets.ClientWebSocket`,这是 .NET 标准库,Godot .NET 自带,无需任何适配代码。

GDScript 版没有路线:`GDScript` 缺少成熟的 protobuf 生态,复用 C# 是唯一现实的路径。

## 接入方式

以 Godot 4.3+ .NET 工程为例,把 Unity SDK 的纯 C# 源文件引入工程:

```xml
<!-- 你的 .csproj:直接编译引用源文件(跟随仓库更新),或
     预编译成类库 dll 后 <Reference> 引用,二选一 -->
<ItemGroup>
  <Compile Include="../../sdks/unity/Runtime/Chirp/**/*.cs" />
</ItemGroup>
<ItemGroup>
  <PackageReference Include="Google.Protobuf" Version="3.27.0" />
</ItemGroup>
```

之后与 Unity 侧同一套用法:`ChirpClient` + 五钩子接口(`ChirpHooks.cs` 的 MessageInterceptor / AuthProvider / MessageStore / ChatEventListener / CommandHandler),登录往返、心跳、重连、KICK 终态语义与 unity3d 文档描述一致。

## 桥到 Godot 节点(模式示意)

SDK 的回调都发生在其内部线程,桥到 Godot 场景树时注意用 `CallDeferred` / `SynchronizationContext.Post` 切回主线程,再发 Godot signal:

```csharp
// 模式示意(未进仓库测试范围):把 C# listener 回调转发成 Godot signal
public partial class ChatBridge : Node, IChatEventListener
{
    [Signal] public delegate void MessageReceivedEventHandler(string text);

    private readonly ChirpClient _client =
        new("ws://127.0.0.1:7001");   // 传输缺省即 System.Net.WebSockets 适配

    public override void _Ready()
    {
        _client.AddListener(this);
    }

    // IChatEventListener:SDK 在内部线程回调,切回主线程再发 signal
    public void OnMessageReceived(Chirp.Chat.ChatMessage message)
    {
        var text = message.Content.ToStringUtf8();
        CallDeferred(MethodName.EmitSignal, SignalName.MessageReceived, text);
    }
}
```

官方的 ChatClient node 封装(内置 signal 转发、场景可实例化)仍在待开发清单,需要真实 Godot 工程验证后交付。

## 平台边界

- **桌面/移动导出**:复用路径与 Unity 相同(Mono/IL2CPP 同源于 .NET 标准库)。
- **Web 导出**:与 Unity WebGL 同样**不可用**——浏览器沙箱里 `System.Net.WebSockets` 无法工作;Web 目标请走 [Cocos/LayaAir](/sdk/cocos-creator) 的 TS 路线。
- 心跳间隔、重连退避等参数语义见 [API 概述](/api/overview)。

## 协议 / API 参考

- 消息 ID 全表:`proto/gateway.proto` 的 `MsgID` 枚举
- C# 语义对照(消息面覆盖表):[Unity3D](/sdk/unity3d)
- 服务端接入(游戏后端):[服务端 SDK](/sdk/server)
