# Chirp Unity 客户端 SDK

Unity 客户端 SDK,直连 Chirp 网关的 WebSocket 协议(`[u32_be len][Packet protobuf]` 帧,序列号关联请求响应)。与 web / mobile 端同构:连接、心跳、重连、踢线、请求超时全部内建,游戏侧只面对 `Task` 与事件。

```
Runtime/
  Chirp/            纯 C# 协议库(无 UnityEngine 依赖,可 dotnet test)
    FrameCodec.cs       帧编解码(16MB 上限)
    ChirpTransport.cs   IChirpTransport 抽象 + ClientWebSocketTransport
    ChirpError.cs       RequestError / RequestErrorKind / 错误码中文文案
    MessageSpec.cs      请求-响应 spec(消息 ID + 响应 parser)
    ChirpClient.cs      连接状态机(心跳/重连/踢线/超时 + Reconnecting/Reconnected 事件)
    ChirpHooks.cs       五钩子接口(拦截/认证/存档/监听/命令)+ SendOptions
    ChirpMessageStore.cs MemoryMessageStore(内存存档,newest-first)
    FileMessageStore.cs 文件版本地存档(append-only 日志 + 启动重放,零依赖)
    WordFilterInterceptor.cs 敏感词预检拦截器(词库格式/替换语义对齐服务端 WordFilter)
    ChirpMessages.cs    Specs 全表:聊天/社交/组队/设备/语音全部 Req/Resp 消息对
  ChirpManager.cs   MonoBehaviour 薄壳(主线程派发 + 常用便捷方法)
dotnet/            纯 .NET 测试工程(CI 里跑真单测,不需要 Unity)
```

## 消息面覆盖

`Specs` 全表与 web 伴侣端 `msg_map` 一一对齐,并多出游戏侧语音面:

| 平面 | 消息对 | 入口 |
| --- | --- | --- |
| 聊天(2001-2234) | 发送/历史/已读、群组管理、回应、编辑/删除 | chat WS 7001,`ChirpManager` 便捷方法或 `RequestAsync` |
| 社交(3001-3024) | 好友/拉黑/在线状态 | social WS 8001(第二连接) |
| 语音(4001-4020) | 建房/进房/退房/房间信息/静音/闭麦(join 带 SDP offer,响应带 TURN 短期凭证) | voice WS 9001(第二连接) |
| 组队(7001-7027) | 建队/邀请-接受/就绪/踢人/转让/快照同步 | party WS 7501(第二连接) |
| 设备(6001-6008) | 推送目标注册/注销/列表 | app_gateway WS 5201(第二连接) |

被踢(1005)、心跳(1001/1002)、消息推送(2005)、已读/正在输入/回应/编辑/删除通知、组队快照等**推送**不走 spec 表:用 `client.OnNotify(MsgID.Xxx, body => ...)` 订阅。

## 接入步骤

1. **协议生成代码**:把 `proto/csharp/*.cs` 拷进 `Assets/Chirp/Proto/`(由仓库根 `./gen_proto.sh` 生成,protoc 内建 `--csharp_out`,无插件)。
2. **SDK 运行时**:把 `Runtime/Chirp/` 与 `Runtime/ChirpManager.cs` 拷进 `Assets/Chirp/Runtime/`。
3. **protobuf 运行时**:需要 Google.Protobuf——用 [Google.Protobuf 的 unity 发布包](https://github.com/protocolbuffers/protobuf/releases)(`protobuf-csharp-*.zip` 内的 `Google.Protobuf.dll`)放进 `Assets/Chirp/Plugins/`,或经 NuForUnity / Assembly Definition 引用同名 nuget 包。SDK 生成代码按 Google.Protobuf **3.27.x** 生成。
4. 场景里挂一个 `ChirpManager`(或 `AddComponent<ChirpManager>()`)。

Unity 2021.2+(C# 9 / netstandard2.1)即可,无其他依赖。

> `dotnet/` 目录与 `Runtime/Chirp/` 不要同时拷进 Assets——前者只是 CI 与本地开发用的 xunit 工程,引用了 xunit 包。

### UPM 包形态

本目录同时是合法的 Unity Package(UPM)布局,可作为**本地包**导入而无需拷文件:

- `package.json`:`com.chirp.unity@0.1.0`,最低 Unity 2021.2。
- `Runtime/Chirp/Chirp.Sdk.asmdef`:纯逻辑程序集,`noEngineReferences: true`(编译期强制零引擎依赖,与 dotnet 测试共享同一份源)。
- `Runtime/ChirpManager.asmdef`:`Chirp.Manager` 程序集(MonoBehaviour 壳),引用 `Chirp.Sdk`。
- 前置依赖 Google.Protobuf 需按上面第 3 步以 auto-referenced 插件 DLL 提供(UPM registry 上没有官方 protobuf 包,故 package.json 不声明 dependencies)。

导入方式任选:工程 `Packages/manifest.json` 加 `"com.chirp.unity": "file:../../sdks/unity"`,或 Package Manager → Add package from tarball/disk。proto 生成代码(`proto/csharp/`)仍按第 1 步拷入 Assets——它属每个工程自己的 gencode,不随 SDK 包分发。

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
        _chirp.AutoRelogin = true;                       // 重连成功后自动重放 LOGIN
        _chirp.OnStatusChanged += s => Debug.Log($"chirp: {s}");
        _chirp.OnChatMessage += HandleMessage;
        _chirp.OnTypingIndicator += t => ShowTyping(t.UserId, t.IsTyping);
        _chirp.OnMessageDeleted += d => Tombstone(d.MessageId);
        _chirp.OnKicked += reason => Debug.LogWarning($"被踢下线: {reason}");
        _chirp.OnReloginFailed += e => ShowReloginPrompt();  // token 过期等

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

- **回调线程**:`On*` 事件已由 `ChirpManager` 派发回主线程(内部队列,`Update()` 排空),可以直接调 Unity API。直接用 `ChirpClient` 时回调在收包线程上。
- **请求-响应**:除便捷方法外,任意消息用 spec 走 `RequestAsync`,如 `await client.RequestAsync(Specs.GetHistory, new GetHistoryRequest { ... })`(超时 10s 抛 `RequestError`,Kind=Timeout)。被动等待用 `client.OnNotify(MsgID.XxxNotify, body => ...)` 返回的退订委托。
- **增强鉴权边缘**:scaffold 网关直接 `LoginAsync(userId, deviceId)`;对接 JWT 认证(`app_auth`)时传第三个参数 `LoginAsync(userId, deviceId, token: jwt)`。
- **AutoRelogin**:自动重连只恢复 socket,不恢复会话(与 web/mobile 一致)。勾上 `AutoRelogin` 后,重连成功会自动重放最后一次 LOGIN;失败走 `OnReloginFailed`。
- **离线投递**:`TargetOffline` 不是发送失败——服务端已把消息滚进对方离线队列,上线后补投。错误码中文文案见 `ChirpErrorText.Of`。
- **踢线是终态**:被顶号后客户端不再自动重连(`Status == ConnStatus.Kicked`),需要玩家重新登录;显式再调 `ConnectAsync()` 会清掉终态。

## 钩子接口

五个钩子与 C++ 核心 SDK(`sdks/core`)逐一对齐,全部可选,注册在 `ChirpClient` 上(manager 用户经 `_chirp.Client`,在 `ConnectAsync()` 之后、`LoginAsync()` 之前注册):

| 钩子 | 形态 | 作用 |
| --- | --- | --- |
| `IMessageInterceptor` | interface,全部默认放行 | 发送/接收的改写与审计点;`OnBeforeSend`/`OnBeforeReceive` 返回 false 即拦截(接收侧全丢:不存档、不触发监听、不分发 `OnNotify`);内置 `WordFilterInterceptor` 参考实现(敏感词预检:Replace 改写/Reject 拦截,词库格式与语义对齐服务端 `WordFilter`,客户端服务端可共用同一词库文件) |
| `IAuthProvider` | interface | `LoginAsync` 不传 token 时经 `GetToken()` 取;`AUTH_FAILED` 时 `RenewTokenAsync()` 给一次续期并自动重登一轮;`OnAuthResult` 报终态 |
| `IMessageStore` | interface(`MarkRead`/`GetUnreadCount`/`Cleanup` 有默认) | 本地存档:收发双路自动 `Save`;配套转发 `LoadHistory`/`MarkRead`/`GetUnreadCount`/`CleanupMessages`;内置 `MemoryMessageStore`(newest-first,超限淘汰最旧,不跟踪已读)与 `FileMessageStore`(持久化:append-only 日志 + 启动重放,跟踪已读,`Compact()`/`Cleanup` 原子重写快照) |
| `IChatEventListener` | interface,全默认空 | 连接状态/登录结果/被踢/重连中/重连成功/消息到达;未读、presence、typing、跑马灯、系统公告本期无触发源(与 C++ 一致) |
| `ICommandHandler` | interface(`Usage` 默认 `"/"+Name`) | `'/cmd args'` 本地路由:`Execute(args, senderId)` 返回 false 轮下一个同名 handler;全 miss 本地丢弃;**零注册时 `/` 消息照常发送** |

```csharp
var client = _chirp.Client!;
client.SetMessageStore(new MemoryMessageStore(maxPerChannel: 200));
client.SetMessageInterceptor(new CleanInterceptor());   // 敏感词/改写
client.SetAuthProvider(gameAuthProvider);               // LoginAsync 自动取 token
client.AddListener(gameListener);
client.RegisterCommand(new TradeCommand());

// 带引用回复的完整管线发送(过拦截器/命令路由/本地存档):
var resp = await _chirp.SendChatMessageAsync(new SendOptions
{
    ChannelType = ChannelType.Private,
    ReceiverId = "peer-7",
    ReplyToMessageId = "m-42",
}, senderId: "u1", text: "gg");
```

语义要点:

- **回调线程**:五个钩子在 `ChirpClient` 的收发线程上直调(与 C++ "io 线程触发、引擎层派发"同一契约)——钩子里不要碰 Unity API,要碰就 `_chirp.RunOnMainThread(...)`。`ChirpManager.On*` 事件仍是主线程。
- **改写范围**:`OnBeforeReceive` 的改写影响存档与监听;`OnNotify` 订阅者(manager 的 `OnChatMessage`)收到的仍是线上原文。
- **完整管线 vs 直发**:`SendChatMessageAsync` 走拦截器/命令路由/存档,消息未上网时抛 `RequestError(Blocked)`;旧 `SendChatMessage(...)` 保持 fire-and-forget 直发(新增 `replyToMessageId` 参数),不经钩子。
- **私聊归一化**:private 发送的 `channel_id` 由双方 id 按字典序归一化为 `"a|b"`,与 C++/服务端同款;非私聊必须显式 `ChannelId`。
- **C# 编排差异**:`IAuthProvider.RenewTokenAsync()` 以 await 返回新 token(由 SDK 重登一轮)替代 C++ 的 `OnTokenExpired(renew)` 闭包;契约相同(AUTH_FAILED 后至多续期一次)。

## 组队 / 语音:第二连接

组队(7501)、语音(9001)、社交(8001)是独立服务,各开一条 `ChirpClient`,回调经 `RunOnMainThread` 借 `ChirpManager` 的主线程泵:

```csharp
// 组队:快照驱动(PARTY_STATE_CHANGED 携带全量成员)
var party = _chirp.CreateClient("ws://127.0.0.1:7501");
party.StatusChanged += s => _chirp.RunOnMainThread(() => Debug.Log($"party: {s}"));
party.OnNotify(MsgID.PartyStateChangedNotify, body =>
{
    var snap = Chirp.Party.PartyStateChangedNotify.Parser.ParseFrom(body);
    _chirp.RunOnMainThread(() => RenderParty(snap));
});
await party.ConnectAsync();
await party.RequestAsync(Specs.GetMyParty, new Chirp.Party.GetMyPartyRequest { UserId = "u1" });

// 语音信令:先走 LOGIN 认证门(scaffold 自报;部署了 --token_secret 时传 JWT),
// join 带 SDP offer,响应带 answer + TURN 短期凭证;
// WebRTC 媒体面用 Unity 的 com.unity.webrtc 包,ICE/SDP 转发走 4007-4009。
var voice = _chirp.CreateClient("ws://127.0.0.1:9001");
await voice.ConnectAsync();
await voice.RequestAsync(Specs.Login, new Chirp.Auth.LoginRequest
{
    Token = "u1",
    DeviceId = SystemInfo.deviceUniqueIdentifier,
    Platform = "unity",
});
var join = await voice.RequestAsync(Specs.JoinVoiceRoom, new Chirp.Voice.JoinRoomRequest
{
    UserId = "u1",
    RoomId = roomId,
    SdpOffer = localOffer,
});
voice.OnNotify(MsgID.SdpOfferMsg, body => /* 对端 offer → RunOnMainThread */);
voice.OnNotify(MsgID.IceCandidateMsg, body => /* candidate → RunOnMainThread */);
```

## 持续集成(CI)

`.github/workflows/unity-sdk.yml` 在每次 push/PR 时用 .NET 10 跑 `dotnet test dotnet/ChirpSdkTests`(44 个用例:帧编解码、序列号关联、超时、通知订阅、踢线终态与恢复、心跳回声、重连生命周期(Reconnecting/Reconnected 事件)、断线 pending 拒绝、语音 spec 往返、钩子接线(拦截改写/拦截丢弃/命令路由/本地存档/登录续期/监听扇出)、reply 引用与私聊归一化),并校验 `proto/csharp` 与 `gen_proto.sh` 无漂移。

## 路线

- 本版交付:连接层 + 全消息 spec 表(含语音面)+ 常用便捷方法 + AutoRelogin + 五钩子接口(C++ core 对齐)。社交/组队的高级封装(好友面板、组队大厅之类)按游戏需求再补。
- iOS/Android 原生构建脚本属于旧桥方案,已随桥一并移除;纯 C# 方案全平台通用(IL2CPP/Mono 均可)。
