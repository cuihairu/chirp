---
title: 服务端 SDK
---

# 服务端 SDK(游戏后端接入)

游戏后端(trade、matchmaking、NPC 引擎等)不扮演玩家,而是以**服务身份**接入 chirp 的服务器平面:出站长连接(dial-out,游戏服不暴露端口)、`service_id` + 共享 secret 过信任门,然后向 chat 注入非玩家消息(SYSTEM/NPC/SERVICE)、向其他服务发布可靠事件。

wire 契约见 `proto/server_gateway.proto`,完整接入语义(凭证、注入、事件、幂等键)见[服务器平面](/server_plane)。

## 连接拓扑(重要)

```text
游戏后端 ──dial-out──▶ chirp_server_gateway(TCP 8100)     注入/事件/ack
游戏后端 ──dial-out──▶ chirp_chat 主端口(TCP 7000)         玩家身份绑定/频道订阅/未读账本
```

玩家身份绑定、频道订阅、未读账本(5013-5030)已从 `chirp_server_gateway` 迁入 `chirp_chat` 的 app_chat 实例(PlayerDirectory)。这两个目标要用**两个 Client 实例**分别拨;向已迁走的 id 发 RPC 会因对端不处理而超时。

## Go(官方 SDK)

`sdks/go` 是官方维护的 Go 参考客户端,语义逐项对齐 C++ 参考实现 `libs/network/server_gateway_peer.cc`,CI 用 10 例环回单测 + `go vet` + `-race` 兜底。

能力面:

- **认证握手**:首帧 `SERVER_AUTH_REQ`;被拒则固定延迟重试。
- **心跳**:hub 在认证响应里指派节奏(静默超过 2×interval 会被关闭),未指派回落 `Config.HeartbeatInterval`(默认 30s)。
- **RPC**:`InjectMessage`(注入消息,`inject_id` 幂等键)、`PublishEvent`(可靠事件,离线入队)、`AckEvents`(批量确认,at-least-once)、玩家身份绑定 / 频道订阅 / 未读账本三组方法(`binding_id` / `subscription_id` 幂等键)。
- **推送 handler**:`SetInjectHandler` / `SetEventHandler` / `SetDisconnectHandler`,在内部读 goroutine 触发,**不得阻塞**。

```go
c := chirp.NewClient(chirp.Config{
    Host: "127.0.0.1", Port: 8100,
    ServiceID: "trade-service", Secret: "<shared secret>",
})
c.SetEventHandler(func(n *pbsg.EventDeliverNotify) {
    c.AckEvents(context.Background(), n.GetEventId())  // 处理完 ack,否则 hub 重投
})
c.Start()
defer c.Stop()

c.InjectMessage(ctx, &pbsg.MessageInjectRequest{
    InjectId:   "raid-5678",                    // 调用方幂等键
    SenderKind: pbsg.SenderKind_SENDER_SERVICE,
    SenderId:   "raid-boss",
    ChannelType: 3,                             // WORLD
    ChannelId:  "world-boss",
    Content:    []byte("世界 Boss 已刷新"),
})
```

细节与完整示例:[sdks/go/README.md](https://github.com/cuihairu/chirp/tree/main/sdks/go)。

## Node.js

**暂无官方 SDK**,按 `server_gateway.proto` 直接接入:Node 的 `net` 模块建 TCP 长连接,自己实现三层——帧编解码(`[uint32_be][Packet]`)、protobuf(`protobufjs` 加载 `proto/server_gateway.proto`)、状态机(首帧 `SERVER_AUTH_REQ`、心跳、sequence 关联、断线重连、事件 ack)。

要点:

- 事件投递是 at-least-once:处理完必须回 `AckEvents`,否则 hub 重连后重投;
- 注入消息用 `inject_id` 幂等键防重;认证被拒后按固定延迟重试即可(C++/Go 参考实现同款,无退避上限);
- Node 侧暂无进程级 E2E,同一 wire 契约由 `./test_services.sh --smoke-npc` 的 C++ 链路覆盖。

## Lua

**暂无官方 SDK**。skynet 等常见游戏服框架按协议接入,要点与 Node.js 相同:TCP 长连接 + `SERVER_AUTH_REQ` 首帧 + 帧编解码 + 心跳 + ack。Lua 侧可用 `lua-protobuf` / `pb` 库处理 `server_gateway.proto`。

## Python

**暂无官方 SDK**。用标准库 `socket` + `google.protobuf` 按协议接入,要点同上。适合工具链、运营后台、GM 脚本这类对吞吐不敏感的接入方;高频注入建议用 Go SDK 或 C++ 参考实现。

## 协议 / API 参考

- 服务器平面契约(凭证模型、注入语义、事件、跨平面回复):[服务器平面](/server_plane)
- 玩家身份绑定 / 频道订阅 / 统一未读的 RPC 全表:`docs/server_plane.md` 对应章节
- `proto/server_gateway.proto`(仓内)定义全部请求/响应消息
- NPC 对话(注入通道的消费方示例):[NPC 对话系统](/design-notes/npc_dialog_system)

## 现状与边界

- hub 平面为**纯 TCP 长连接**,无 WS/TLS 传输;
- 事件只在已认证连接断开时触发 disconnect handler;凭据配错表现为周期性告警日志(固定延迟无限重试);
- 注入的 `OK` 只表示"服务平面已受理",未确认玩家侧送达;
- Go SDK 对真 `chirp_server_gateway` 的进程级 E2E 未建立,同一 wire 契约由 C++ 链路的 `--smoke-npc` 覆盖。
