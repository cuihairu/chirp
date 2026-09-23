# Chirp API 说明

权威的 API 总览已迁移到 [api/overview.md](./api/overview.md)。

请到那个页面查看:

- 包帧格式(packet framing)
- `chirp.gateway.Packet` 信封规则
- 受支持端点的状态
- 当前消息 ID 对照
- 登录与聊天流程
- 常见错误码

消息定义(Schema)的事实来源仍然是 `proto/*.proto`。

状态提醒:受支持的运行时路径是 `gateway + auth + chat`。网关(gateway)目前还不转发 chat、social、voice 的业务包;当前聊天客户端直连 Chat 服务。在把更大的 proto 面当成稳定能力之前,先核对 [Capability Matrix](./CAPABILITY_MATRIX.md)。
