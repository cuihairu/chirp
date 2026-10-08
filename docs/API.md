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

状态提醒:受支持的运行时路径是 `gateway + auth + chat`。网关(gateway)配置 `--chat_host` 后会把 2xxx(2001-2999)业务包经每客户端 ServiceBridge 管道转发到对应 chat 实例,其中 2248 在 2xxx 通配前先分流到 search 服务(`--search_host`,默认 5007);social(3xxx)/voice(4xxx)不在转发路径。聊天客户端既可走网关转发,也可直连 Chat 服务(可选路径)。在把更大的 proto 面当成稳定能力之前,先核对 [Capability Matrix](./CAPABILITY_MATRIX.md)。
