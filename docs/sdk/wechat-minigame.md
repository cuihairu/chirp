---
title: 微信小游戏接入
---

# 微信小游戏

**微信小游戏暂无官方 SDK**。小运行时没有 TCP,只有 `wx.connectSocket`——这反而简化了选型:**必须且只能走 chirp 的 WebSocket 边缘**,协议与所有其他端完全一致。chirp 的接入面是纯协议(帧 + Protobuf),与宿主环境无关。

## 接入方式

```ts
const task = wx.connectSocket({ url: "wss://your-domain/chirp-ws" });
task.onOpen(() => { /* 发 LOGIN */ });
task.onMessage((res) => { /* res.data 是 ArrayBuffer:解帧 */ });
```

需要自己实现的三层(与 Cocos/LayaAir 完全相同):

1. **帧编解码**:每帧是 `[uint32_be payload_size][Packet protobuf 字节]`,`Packet` 含 `msg_id` / `sequence` / `body`;
2. **protobuf**:[protobuf.js](https://github.com/protobufjs/protobuf.js) 可在小游戏环境运行(加载 `proto/gateway.proto` 编译产物,注意用 json/commonjs 方式打包,避免动态加载限制);
3. **客户端状态机**:登录往返、心跳、重连、KICK 终态。

**参考实现**:仓库 `apps/web_companion/src/protocol/` 是带单测的 TS 协议栈(`frame.ts` / `msg_map.ts` / `chirp_client.ts`),不依赖 DOM,可直接搬进小游戏工程——只需要把传输层从浏览器 `WebSocket` 换成 `wx.connectSocket`。

## 生命周期

```text
connectSocket → LOGIN(1003/1004) → 收发(2001 发送 / 2005 推送) → 心跳(1001/1002)
        ↓ 断线/切后台                    ↓ 收到 KICK(1005)
      自动重连 + 重新登录              踢线终态:停止重连,需玩家重新登录
```

小游戏特有的两点:

- **切后台挂起**:微信切后台会断 WS。回到前台(`onShow`)后主动重建连接并重新登录;离线期间的消息由服务端离线队列补投,收到后记得 ack。
- **心跳兜底**:若宿主长时间挂起导致心跳断掉,服务端会按超时关闭连接——下个 `onShow` 的重连流程自然会覆盖,不需要为心跳做特殊逻辑。

部署到线上环境时,WS 域名需为 wss 并在小程序后台配置合法域名(这是微信平台要求,与 chirp 无关;本地开发可用 devtools 的"不校验合法域名"直连 `ws://127.0.0.1:7001`)。

## 协议 / API 参考

- 协议总览:[API 概述](/api/overview)(Packet 帧、消息 ID 分段、核心流程)——接入前先读这份
- 消息 ID 全表:`proto/gateway.proto` 的 `MsgID` 枚举(1xxx 认证/会话、2xxx 聊天、3xxx 社交、4xxx 语音、7xxx 组队)
- TS 参考实现:[apps/web_companion/src/protocol/](https://github.com/cuihairu/chirp/tree/main/apps/web_companion/src/protocol)
- C# 版语义对照(消息面覆盖表):[Unity3D](/sdk/unity3d)
- 服务端接入(游戏后端):[服务端 SDK](/sdk/server) 与[服务器平面](/server_plane)

## 现状与边界

- 语音(4xxx,WebRTC 信令 + 媒体面)在小游戏环境没有 `RTCPeerConnection`,**不可用**;聊天/组队/设备推送等纯消息面不受影响。
- 聊天主链路只需要 chat 一条连接;社交(WS 8001)、组队(WS 7501)独立服务,按需再接。
