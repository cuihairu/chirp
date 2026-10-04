# Chirp 项目现状

> 当前状态快照(2026-10-05)。这不是"一切生产就绪"的声明——各能力的真实完成度以[能力矩阵](docs/CAPABILITY_MATRIX.md)为准。

## 当前验证基线

仓库在默认依赖(无 MySQL/libsodium/CURL)下开箱即可构建、测试:

```bash
./gen_proto.sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev          # 44 个测试目标全部通过
```

行覆盖率(CI 同款门槛,每包 98% 硬失败;本地按 100% 报):

```bash
scripts/run_coverage.sh     # 12 个包全部 100%(仅含已注明理由的 KNOWN_UNCOVERABLE 排除项;
                            #  本地 gcc15 另有 4 行已知 inline 伪影,CI 侧无此差异)
```

进程级 smoke:

```bash
./test_services.sh --smoke        # auth + gateway + TCP/WS 登录
./test_services.sh --smoke-chat   # chat + 聊天客户端
./test_services.sh --smoke-sdk    # 游戏客户端 SDK + chat:登录/双向收发/离线队列
./test_services.sh --smoke-npc    # 服务器平面 + NPC 对话回环
./test_services.sh --smoke-redis  # Redis session/kick 路径
./test_services.sh --smoke-voice  # voice 房间生命周期(真实 chirp_voice)
./test_services.sh --smoke-party  # party 快照生命周期(真实 chirp_party)
```

CI(`ci.yml`)跑 Debug + Release 构建与 ctest,另有覆盖率 job 卡 98% 包门槛;smoke job 把十条 smoke 腿(含 voice/party)各跑一步。

## 与 2026-04 快照的差异

- 单测从 2 个套件(`common_tests`、`network_tests`)增长到 40 个;libs、auth、chat、gateway、notification、npc_dialog、search、server_gateway、social 等全部后端包行覆盖 100%。
- 测试基建成型:`fake_mysql` / `fake_sodium`(C API 影子实现)、`InMemoryRedis` + `FakeRedisServer`(回环 TCP),详见 `tests/unit/`。
- 服务器平面注入链路、NPC 对话回环、SDK 直连 chat 均有进程级 E2E smoke。
- 早期"过度乐观"的总结文档已由 2026-09 重写的 [TODO.md](TODO.md)(活路线图)与[能力矩阵](docs/CAPABILITY_MATRIX.md)取代;本文只保留可复现的验证命令。

## 已知未验证区域

- voice/party/app_gateway 均已接入单测套件(`voice_tests` 61 例、`party_tests` 46 例、`app_sdk_gateway_tests`);仍未验证的是 WebRTC **媒体面**端到端——语音信令面(房间/名单/静音)有单测与 `--smoke-voice` 进程级覆盖,真实浏览器/音频链路未验证。
- notification 的真实 APNs/FCM HTTP 投递仍是 `PushTransport` 日志 stub。

路线图与架构债见 [TODO.md](TODO.md)。
