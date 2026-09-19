# Chirp Unity SDK

Unity 客户端 SDK,直连 Chirp 网关的 WebSocket 协议(`[u32_be len][Packet protobuf]` 帧,序列号关联请求响应)。与 web / mobile 端同构:连接、心跳、重连、踢线、请求超时全部内建,游戏侧只面对 `Task` 与事件。

```
Runtime/
  Chirp/            纯 C# 协议库(无 UnityEngine 依赖,可 dotnet test)
    FrameCodec.cs       帧编解码(16MB 上限)
    ChirpTransport.cs   IChirpTransport 抽象 + ClientWebSocketTransport
    ChirpError.cs       RequestError / RequestErrorKind / 错误码中文文案
    MessageSpec.cs      请求-响应 spec(消息 ID + 响应 parser)
    ChirpClient.cs      连接状态机(心跳/重连/踢线/超时)
    ChirpMessages.cs    Specs 全表:全部 Req/Resp 消息对
  ChirpManager.cs   MonoBehaviour 薄壳(主线程派发 + 常用便捷方法)
dotnet/            纯 .NET 测试工程(CI 里跑真单测,不需要 Unity)
```

## 接入步骤

1. **协议生成代码**:把 `proto/csharp/*.cs` 拷进 `Assets/Chirp/Proto/`(由仓库根 `./gen_proto.sh` 生成,protoc 内建 `--csharp_out`,无插件)。
2. **SDK 运行时**:把 `Runtime/Chirp/` 与 `Runtime/ChirpManager.cs` 拷进 `Assets/Chirp/Runtime/`。
3. **protobuf 运行时**:Unity Package Manager → *Add package by name* → `com.unity.nuget.newtonsoft-json` 不需要;需要的是 Google.Protobuf——用 [Google.Protobuf 的 unity 发布包](https://github.com/protocolbuffers/protobuf/releases)(`protobuf-csharp-*.zip` 内的 `Google.Protobuf.dll`)放进 `Assets/Chirp/Plugins/`,或经 NuForUnity / Assembly Definition 引用同名 nuget 包。SDK 生成代码按 Google.Protobuf **3.27.x** 生成。
4. 场景里挂一个 `ChirpManager`(或 `AddComponent<ChirpManager>()`)。

Unity 2021.2+(C# 9 / netstandard2.1)即可,无其他依赖。

> `dotnet/` 目录与 `Runtime/Chirp/` 不要同时拷进 Assets——前者只是 CI 与本地开发用的 xunit 工程,引用了 xunit 包。

## 使用示例

```csharp
using Chirp.Chat;
using Chirp.Common;
using Chirp.Sdk;
using UnityEngine;

public class GameChat : MonoBehaviour
{
    private ChirpManager _chirp;

    private async void Start()
    {
        _chirp = gameObject.AddComponent<ChirpManager>();
        _chirp.OnStatusChanged += s => Debug.Log($"chirp: {s}");
        _chirp.OnChatMessage += m =>
            Debug.Log($"[{m.ChannelId}] {m.SenderId}: {m.Content.ToStringUtf8()}");
        _chirp.OnKicked += reason => Debug.LogWarning($"被踢下线: {reason}");

        await _chirp.ConnectAsync();
        var login = await _chirp.LoginAsync(userId: "u1", deviceId: SystemInfo.deviceUniqueIdentifier);
        Debug.Log($"登录成功 userId={login.UserId}");

        _chirp.SendChatMessage("u1", receiverId: "", channelId: "g1",
            ChannelType.Group, "hello from unity");
    }

    // 收到消息后必须 ack,否则服务端会把该消息滚回离线队列重复投递。
    private void HandleMessage(ChatMessage m)
    {
        // ... 游戏逻辑 ...
        _chirp.AckChatMessage(m.MessageId, "u1");
    }
}
```

要点:

- **回调线程**:`OnChatMessage` / `OnKicked` / `OnStatusChanged` 已由 `ChirpManager` 派发回主线程(内部队列,`Update()` 排空),可以直接调 Unity API。直接用 `ChirpClient` 时回调在收包线程上。
- **请求-响应**:除上表便捷方法外,任意消息用 spec 走 `RequestAsync`,如 `await client.RequestAsync(Specs.GetHistory, new GetHistoryRequest { ... })`(超时 10s 抛 `RequestError`,Kind=Timeout)。被动等待用 `client.OnNotify(MsgID.XxxNotify, body => ...)` 返回的退订委托。
- **离线投递**:`TargetOffline` 不是发送失败——服务端已把消息滚进对方离线队列,上线后补投。错误码中文文案见 `ChirpErrorText.Of`。
- **踢线是终态**:被顶号后客户端不再自动重连(`Status == ConnStatus.Kicked`),需要玩家重新登录。

## CI

`.github/workflows/unity-sdk.yml` 在每次 push/PR 时用 .NET 10 跑 `dotnet test dotnet/ChirpSdkTests`(11 个用例:帧编解码、序列号关联、超时、通知订阅、踢线终态、心跳回声、重连生命周期),并校验 `proto/csharp` 与 `gen_proto.sh` 无漂移。

## 路线

- 本版交付连接层 + 全消息 spec 表 + 常用便捷方法;社交/组队的高级封装(好友面板、组队大厅之类)按游戏需求再补。
- iOS/Android 原生构建脚本属于旧桥方案,已随桥一并移除;纯 C# 方案全平台通用(IL2CPP/Mono 均可)。
