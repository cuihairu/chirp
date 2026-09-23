---
title: Unity3D 接入
---

# Unity3D

Unity 是官方 SDK 覆盖最完整的引擎:仓库里的 `sdks/unity` 是一份**纯 C# 协议栈**(无 UnityEngine 依赖,可独立跑 dotnet 单测),外加一个 `ChirpManager` MonoBehaviour 薄壳负责主线程派发。连接、心跳、重连、踢线、请求超时全部内建,游戏侧只面对 `Task` 与事件。

完整源码与细节见 [sdks/unity/README.md](https://github.com/cuihairu/chirp/tree/main/sdks/unity)。

## 接入方式

1. **协议生成代码**:把 `proto/csharp/*.cs` 拷进 `Assets/Chirp/Proto/`(仓库根 `./gen_proto.sh` 生成,protoc 内建 `--csharp_out`,无插件)。
2. **SDK 运行时**:把 `Runtime/Chirp/` 与 `Runtime/ChirpManager.cs` 拷进 `Assets/Chirp/Runtime/`。
3. **protobuf 运行时**:放入 `Google.Protobuf.dll`(protobuf 3.27.x 的 unity 发布包,或经 NuForUnity 引用同名 nuget 包)。
4. 场景里挂一个 `ChirpManager`(或 `AddComponent<ChirpManager>()`)。

Unity 2021.2+(C# 9 / netstandard2.1)即可,IL2CPP 与 Mono 均可。

## 生命周期

```text
ConnectAsync → LoginAsync → 收发消息/事件(主线程回调) → 断线自动重连(可 AutoRelogin 重放登录)
                                  ↓ 收到 KICK(1005)
                              踢线终态:不再自动重连,需玩家重新登录
```

- **回调线程**:`On*` 事件已由 `ChirpManager` 派发回主线程(内部队列,`Update()` 排空),可以直接调 Unity API。
- **请求-响应**:序列号(sequence)关联;除便捷方法外,任意消息用 spec 表走 `RequestAsync`(超时 10s 抛 `RequestError`)。
- **被动推送**:心跳、消息推送(2005)、已读/正在输入/回应/编辑/删除通知、组队快照等不走 spec 表,用 `client.OnNotify(MsgID.Xxx, body => ...)` 订阅。
- **离线投递**:发送返回 `TargetOffline` 不是失败——消息已滚进对方离线队列,上线补投;收到消息后必须 `AckChatMessage`,否则服务端会重复投递。
- **踢线是终态**:被顶号后客户端不再自动重连,需要玩家重新登录。

## 最小示例

```csharp
_chirp = gameObject.AddComponent<ChirpManager>();
_chirp.AutoRelogin = true;                       // 重连成功后自动重放 LOGIN
_chirp.OnChatMessage += HandleMessage;
_chirp.OnKicked += reason => Debug.LogWarning($"被踢下线: {reason}");

await _chirp.ConnectAsync();
var login = await _chirp.LoginAsync(userId: "u1", deviceId: SystemInfo.deviceUniqueIdentifier);
_chirp.SendChatMessage("u1", receiverId: "", channelId: "g1", ChannelType.Group, "hello");

private void HandleMessage(ChatMessage m)
{
    // ... 游戏逻辑 ...
    _chirp.AckChatMessage(m.MessageId, "u1");   // 必须 ack,否则重复投递
}
```

scaffold 网关直接 `LoginAsync(userId, deviceId)`;对接 JWT 认证(`app_auth`)时传第三个参数 `token: jwt`。

## 多服务连接拓扑

聊天、社交、语音、组队是独立服务,各开一条 `ChirpClient`(默认开发拓扑端口):

| 平面 | 消息段 | 入口 |
| --- | --- | --- |
| 聊天 | 2001-2234 | chat WS 7001 |
| 社交(好友/拉黑/在线状态) | 3001-3024 | social WS 8001(第二连接) |
| 语音(WebRTC 信令) | 4001-4020 | voice WS 9001(第二连接) |
| 组队 | 7001-7027 | party WS 7501(第二连接) |
| 设备推送注册 | 6001-6008 | app_gateway WS 5201(第二连接) |

第二连接的回调经 `RunOnMainThread` 借 `ChirpManager` 的主线程泵派发;语音媒体面用 Unity 官方 `com.unity.webrtc` 包,信令(ICE/SDP 转发)走 chirp 的 4007-4009。

## 协议 / API 参考

- 协议总览:[API 概述](/api/overview)(Packet 帧、消息 ID 分段、核心流程)
- 消息 ID 全表:`proto/gateway.proto` 的 `MsgID` 枚举(仓内)与 `sdks/unity/Runtime/Chirp/ChirpMessages.cs` 的 spec 表
- C# 层参考:[sdks/unity/README.md](https://github.com/cuihairu/chirp/tree/main/sdks/unity)
- 服务端接入(游戏后端):[服务端 SDK](/sdk/server) 与[服务器平面](/server_plane)

## 现状与边界

- CI(`.github/workflows/unity-sdk.yml`)用 .NET 10 跑 16 个单测(帧编解码、序列号关联、超时、踢线终态、心跳回声、重连生命周期等),并校验 `proto/csharp` 无漂移。
- 社交/组队的高级封装(好友面板、组队大厅之类 UI 组件)尚未提供,按游戏需求再补;iOS/Android 原生构建脚本属于旧桥方案,已随桥一并移除。
