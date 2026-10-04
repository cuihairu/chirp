# Chirp 实现概览

> 实现构成一览(2026-10-05)。能力状态以[能力矩阵](docs/CAPABILITY_MATRIX.md)为权威;验证命令见 [PROJECT_COMPLETE.md](PROJECT_COMPLETE.md)。

## 仓库构成

- `proto/`:协议定义(`gateway.proto`、`server_gateway.proto` 等),MsgID 按 1xxx 认证 / 2xxx 聊天 / 3xxx 社交 / 4xxx 语音 / 5xxx 服务器平面 / 6xxx 推送分段
- `libs/common`:日志、JWT/HS256、base64、sha256、指标原语、metrics HTTP server
- `libs/network`:ASIO TCP/WS server/session、长度前缀 framing、Redis RESP 客户端与 Pub/Sub 路由、chat peer 注册协议(hub/link)、server gateway peer
- `services/game/sdk_gateway`:游戏客户端边缘(登录/心跳/会话绑定/跨实例 kick,`game_sdk_gateway`)
- `services/shared/chat`:聊天服务源码(同一 `chirp_chat` 二进制按启动参数部署为 game_chat / app_chat / 分布式形态);私聊、群组、已读回执、正在输入、表情回应、消息编辑/删除/撤回、@提及;Redis 热存储,MySQL 就绪时构建 hybrid 存储与迁移 worker;服务器平面注入消费
- `services/game/server_gateway`:服务器平面枢纽(服务凭证认证、消息注入、事件离线排队/重投,`game_server_gateway`)
- `services/game/npc_dialog`:NPC 对话(关键词规则引擎,纯服务器平面客户端)
- `services/app/auth`:App 平面认证(`app_auth`;MySQL+libsodium 就绪时构建 enhanced:注册、密码登录、refresh token、限流、防爆破)
- `services/app/notification`:设备注册与推送平面(6xxx,`app_notification`);`PushTransport` 默认日志传输,`--push_transport http` 打开真实 HTTP provider 客户端(APNs 要求 HTTP/2,生产在本通道前置协议转换)
- `services/app/sdk_gateway`:App 平面边缘(登录/心跳 + 设备消息转发 + 聊天管道,`app_sdk_gateway`)
- `services/social` / `services/voice` / `services/search` / `services/party`:实验性能力面
- `sdks/core`:C++ 客户端 SDK(`chirp::sdk::ChatClient`,进程级 smoke 覆盖)
- `apps/`、`tools/`:CLI、benchmark、移动端协议核与管理台雏形
- `tests/`:40 个单测套件 + `test_services.sh` 十条进程级 smoke

## 验证现状

- 全部被测后端包行覆盖 100%(`scripts/run_coverage.sh`,CI 卡 98% 包门槛)
- 测试基建:C API 影子 fake(`fake_mysql`/`fake_sodium`)、`InMemoryRedis` + 回环 `FakeRedisServer`,单测不依赖外部服务
- 冒烟路径:`--smoke` / `--smoke-chat` / `--smoke-sdk` / `--smoke-game` / `--smoke-npc` / `--smoke-edge` / `--smoke-jwt` / `--smoke-redis` / `--smoke-voice` / `--smoke-party`,CI smoke job 全量覆盖

## 边界提醒

代码面大于已验证面:`social`/`voice`/`search`/`party`/SDK/移动端协议核/管理台不应默认视为生产稳定能力;voice 的 WebRTC **媒体面**(真实浏览器/音频链路)未验证;notification 的真实 APNs/FCM 凭据接入留待部署环境。对外表述以[能力矩阵](docs/CAPABILITY_MATRIX.md)为准。
