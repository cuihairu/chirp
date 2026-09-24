---
title: Cocos Creator 接入
---

# Cocos Creator

**Cocos Creator 暂无官方 SDK**。chirp 的接入面是纯协议(TCP/WebSocket + Protobuf),与引擎无关,Cocos Creator(TypeScript 生态)完全可以按协议直连——仓库里的 Web 伴侣 App 就是一份可运行的 TS 参考实现。

## 接入方式

Cocos Creator 3.x 的 `WebSocket` 即可连 chirp 的 WS 边缘(开发拓扑 chat **WS 7001**;TCP 7000 帧协议与 WS 消息体一致,浏览器/小游戏环境用 WS 即可):

```ts
const ws = new WebSocket("ws://127.0.0.1:7001");
ws.binaryType = "arraybuffer";
```

需要自己实现的三层(工作量不大,协议是二进制小帧):

1. **帧编解码**:每帧是 `[uint32_be payload_size][Packet protobuf 字节]`,`Packet` 含 `msg_id` / `sequence` / `body` 三个字段;
2. **protobuf**:用 [protobuf.js](https://github.com/protobufjs/protobuf.js)(或 google-protobuf)加载 `proto/gateway.proto` 编译产物;
3. **客户端状态机**:登录请求-响应关联、心跳、断线重连、KICK 终态处理。

**直接抄现成实现**:仓库 `sdks/ts/src/` 下有完整的 TS 协议栈,可整目录搬进 Cocos 工程(微信小游戏目标用 `@chirp/protocol/adapters/wx_socket` 的 `createWxSocketFactory(wx)` 作传输工厂)——

| 文件 | 职责 |
| --- | --- |
| `frame.ts` | 帧编解码(u32 长度前缀 + Packet) |
| `msg_map.ts` | 消息 ID ↔ 请求/响应类型映射表 |
| `chirp_client.ts` | 客户端状态机(连接/心跳/重连/序列号关联) |

该实现有单测(`*.test.ts`)与端到端测试兜底,语义与 unity/mobile 端逐项对齐。

## 生命周期

与所有客户端一致:

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

- 官方维护的 SDK 只有 Unity(C#)、Unreal(C++ 壳)、Go(服务端)与 C++ 核心;Cocos/LayaAir/微信小游戏按协议接入,TS 层直接复用 web_companion 的协议栈即可。
- 社交(WS 8001)、语音(WS 9001)、组队(WS 7501)是独立 WS 服务,各开一条连接;按需再接,聊天主链路只需要 chat 一条。
