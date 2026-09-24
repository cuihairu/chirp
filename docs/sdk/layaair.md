---
title: LayaAir 接入
---

# LayaAir

**LayaAir 暂无官方 SDK**,接入方式与 Cocos Creator 完全同构:TS/JS 生态 + 引擎原生 `WebSocket` 直连 chirp 的 WS 边缘。chirp 的接入面是纯协议(TCP/WebSocket + Protobuf),与引擎无关。

## 接入方式

LayaAir 3.x(TS 项目)直接用浏览器标准的 `WebSocket`(LayaAir 的 `Laya.Browser.window.WebSocket` 或原生全局均可):

```ts
const ws = new WebSocket("ws://127.0.0.1:7001");   // chat WS 边缘(开发拓扑)
ws.binaryType = "arraybuffer";
```

需要自己实现的三层:

1. **帧编解码**:每帧是 `[uint32_be payload_size][Packet protobuf 字节]`,`Packet` 含 `msg_id` / `sequence` / `body`;
2. **protobuf**:用 [protobuf.js](https://github.com/protobufjs/protobuf.js) 加载 `proto/gateway.proto` 编译产物;
3. **客户端状态机**:登录往返、心跳、重连、KICK 终态。

**参考实现**:仓库 `sdks/ts/src/` 是一份带单测的完整 TS 协议栈(`frame.ts` 帧编解码、`msg_map.ts` 消息映射表、`chirp_client.ts` 状态机),可直接搬进 LayaAir 工程,语义与 unity/mobile 端对齐。

## 生命周期

```text
connect → LOGIN(1003/1004) → 收发(2001 发送 / 2005 推送) → 心跳(1001/1002)
        ↓ 断线                  ↓ 收到 KICK(1005)
      自动重连 + 重新登录      踢线终态:停止重连,需玩家重新登录
```

- `sequence` 递增,响应按 sequence 关联回请求;
- 收到离线消息后要发 ack,否则服务端重复投递;
- 被顶号(KICK)是终态,不要对它做自动重连。

## 协议 / API 参考

- 协议总览:[API 概述](/api/overview)(Packet 帧、消息 ID 分段、核心流程)——接入前先读这份
- 消息 ID 全表:`proto/gateway.proto` 的 `MsgID` 枚举(1xxx 认证/会话、2xxx 聊天、3xxx 社交、4xxx 语音、7xxx 组队)
- TS 参考实现:[sdks/ts](https://github.com/cuihairu/chirp/tree/main/sdks/ts)
- C# 版语义对照(消息面覆盖表):[Unity3D](/sdk/unity3d)
- 服务端接入(游戏后端):[服务端 SDK](/sdk/server) 与[服务器平面](/server_plane)

## 现状与边界

- 与 Cocos Creator 同:官方 SDK 只有 Unity/Unreal/Go/C++ 核心,LayaAir 按协议接入,TS 层复用 web_companion 协议栈。
- 聊天主链路只需要 chat 一条连接;社交(WS 8001)、语音(WS 9001)、组队(WS 7501)独立服务,按需再接。
