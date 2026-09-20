# Chirp Go Server SDK

游戏后端服务(trade、matchmaking、NPC 引擎等)接入 chirp **服务器平面**的 Go 参考客户端。dial-out 连接 `chirp_server_gateway`(默认 TCP 8100),以 `service_id` + 共享 secret 过信任门,然后向 chat 平面注入非玩家消息、向其他服务发布可靠事件。

语义逐项对齐 C++ 参考实现 `libs/network/server_gateway_peer.cc`(wire 契约见 `proto/server_gateway.proto`)。

## 能力面

- **认证握手**:首帧 `SERVER_AUTH_REQ`(sequence 0);`ServerAuthResponse` 非 OK → `*AuthError` + 固定延迟重试(与 C++ 一致,无退避上限)。
- **心跳**:hub 在认证响应里指派节奏(连接静默超过 2×interval 会被 hub 关闭);未指派时回落 `Config.HeartbeatInterval`(默认 30s)。ping 带递增 sequence,pong 仅用于 hub 侧存活判定(C++ 同款)。
- **RPC**:sequence 关联 + 响应 msg_id 校验(不匹配告警丢弃);`context` 控制超时;hub 返回非 OK 码 → `*ServerError`。
  - `InjectMessage` — 注入 SYSTEM/NPC/SERVICE 发送者的消息(`inject_id` 幂等键)
  - `PublishEvent` — 发布可靠事件(目标离线则入队,重连重投直到 ack)
  - `AckEvents` — 批量确认已处理的 `EVENT_DELIVER_NOTIFY`(at-least-once)
  - `BindPlayerIdentity` / `UnbindPlayerIdentity` / `GetPlayerIdentities` / `ResolveGameUser` — 玩家身份绑定(`binding_id` 幂等键;详见 `docs/server_plane.md`「Player identity bindings」)
  - `SubscribePlayerChannel` / `UnsubscribePlayerChannel` / `GetPlayerSubscriptions` — 玩家频道订阅(`subscription_id` 幂等键,留空由服务端铸造;详见 `docs/server_plane.md`「Player channel subscriptions」)
  - `MarkChannelsRead` / `GetUnreadSummary` — 统一未读 badge 账本(分层标记选择器:单频道/整游戏/全部,幂等;summary 按 (game, channel) 稳定排序 + 过滤后总数;详见 `docs/server_plane.md`「Unified unread」)
- **推送 handler**:`SetInjectHandler` / `SetEventHandler` / `SetDisconnectHandler`,在内部读 goroutine 触发,**不得阻塞**。
- **断线**:在途调用收到 `ErrConnectionLost`,客户端自动重连;已认证连接断开才触发 disconnect handler(认证拒绝/拨号失败只记日志)。

## 使用

```go
import (
    "context"
    "time"

    chirp "github.com/cui/chirp/sdks/go"
    pbsg "github.com/cui/chirp/proto/go/server_gateway"
)

c := chirp.NewClient(chirp.Config{
    Host: "127.0.0.1", Port: 8100,
    ServiceID: "trade-service", Secret: "<shared secret>",
})
c.SetEventHandler(func(n *pbsg.EventDeliverNotify) {
    // 读 goroutine:处理事件,处理完 ack,否则 hub 重投
    _ = n
    c.AckEvents(context.Background(), n.GetEventId())
})
c.Start()
defer c.Stop()

ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
defer cancel()
c.InjectMessage(ctx, &pbsg.MessageInjectRequest{
    InjectId:   "trade-1234", // 调用方幂等键
    SenderKind: pbsg.SenderKind_SENDER_SERVICE,
    SenderId:   "trade",
    ReceiverId: "player-7",
    Content:    []byte("订单已发货"),
})
// fan-in(WP-8 分片 3):设置 GameId + 非 PRIVATE 频道,
// 消息会扇出给 (GameId, ChannelId) 的每个订阅者一份私聊副本。
c.InjectMessage(ctx, &pbsg.MessageInjectRequest{
    InjectId:    "raid-5678",
    SenderKind:  pbsg.SenderKind_SENDER_SERVICE,
    SenderId:    "raid-boss",
    ChannelType: 3, // WORLD
    ChannelId:   "world-boss",
    GameId:      "game-a",
    Content:     []byte("世界 Boss 已刷新"),
})
```

## 模块与代码生成

仓库根是单一 Go module(`github.com/cui/chirp`):`sdks/go` 是 SDK 包,`proto/go/<name>/` 是入库的 protobuf 生成物(每 `.proto` 一个包,protoc-gen-go;`go_package` 在 `.proto` 文件里)。CI(`go-sdk.yml`)用固定 protoc 33.4 + protoc-gen-go v1.36.12 重生成并漂移校验,`go vet` + `go test -race` 跑 10 例环回单测(fake hub 进程内回环,无需任何服务)。

本地重新生成:`PATH="$HOME/go/bin:$PATH" bash gen_proto.sh`(需 `go install google.golang.org/protobuf/cmd/protoc-gen-go@v1.36.12`)。

## 局限

- 对真 `chirp_server_gateway` 的进程级 E2E 未在 Go 侧建立;同一 wire 契约由 `./test_services.sh --smoke-npc` 的 C++ 链路覆盖。
- 认证被拒后按固定延迟无限重试(与 C++ 参考一致);凭据配错时表现为周期性告警日志。
- 无 WS/TLS 传输(hub 平面为纯 TCP 长连接)。
