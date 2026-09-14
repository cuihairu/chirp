# Chirp Project Status

> 当前状态快照(2026-09-14)。这不是"一切生产就绪"的声明——各能力的真实完成度以[能力矩阵](docs/CAPABILITY_MATRIX.md)为准。

## 当前验证基线

仓库在默认依赖(无 MySQL/libsodium/CURL)下开箱即可构建、测试:

```bash
./gen_proto.sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev          # 25 个套件全部通过
```

行覆盖率(CI 同款门槛,每包 98% 硬失败):

```bash
scripts/run_coverage.sh     # 13 个包全部 100%(仅含已注明理由的 KNOWN_UNCOVERABLE 排除项)
```

进程级 smoke:

```bash
./test_services.sh --smoke        # auth + gateway + TCP/WS 登录
./test_services.sh --smoke-chat   # chat + 聊天客户端
./test_services.sh --smoke-sdk    # 游戏客户端 SDK + chat:登录/双向收发/离线队列
./test_services.sh --smoke-npc    # 服务器平面 + NPC 对话回环
./test_services.sh --smoke-redis  # Redis session/kick 路径
```

CI(`ci.yml`)跑 Debug + Release 构建与 ctest,另有覆盖率 job 卡 98% 包门槛。

## 与 2026-04 快照的差异

- 单测从 2 个套件(`common_tests`、`network_tests`)增长到 25 个;libs、auth、chat、gateway、notification、npc_dialog、search、server_gateway、social 等全部后端包行覆盖 100%。
- 测试基建成型:`fake_mysql` / `fake_sodium`(C API 影子实现)、`InMemoryRedis` + `FakeRedisServer`(回环 TCP),详见 `tests/unit/`。
- 服务器平面注入链路、NPC 对话回环、SDK 直连 chat 均有进程级 E2E smoke。
- 早期"过度乐观"的总结文档已由 2026-09 重写的 [TODO.md](TODO.md)(活路线图)与[能力矩阵](docs/CAPABILITY_MATRIX.md)取代;本文只保留可复现的验证命令。

## 已知未验证区域

- `app_gateway`、`voice` 尚未接入任何单测套件(TODO.md P2)。
- `test_services.sh` 的五条 smoke 均未纳入 CI(TODO.md Current Focus)。
- notification 的真实 APNs/FCM HTTP 投递仍是 `PushTransport` 日志 stub。

路线图与架构债见 [TODO.md](TODO.md)。
