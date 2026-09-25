# 本地验证与压测工具

针对真实服务进程的轻量探针/压测客户端集合。全部走与生产客户端相同的 wire 协议(`[u32_be len][chirp.gateway.Packet]`),先起服务再跑工具。

## 工具矩阵

| 工具 | 用途 |
| --- | --- |
| `chirp_ping_client` | 登录 + 心跳往返延迟 |
| `chirp_login_client` | 登录路径探针 |
| `chirp_ws_login_client` / `chirp_ws_ping_client` | WebSocket 传输同路径 |
| `chirp_chat_send_client` | 单发探针:私聊/群聊/建群,附带 mark_read/typing/reactions 动作 |
| `chirp_chat_listen_client` | 常驻收消息(验证推送链路) |
| `chirp_chat_history_client` | 历史拉取 |
| `chirp_chat_mysql_exporter` | MySQL 存储导出 |
| `chirp_wp8_client` | App 平面 WP-8 RPC(身份绑定/订阅/未读)探针 |
| `chirp_load_client` | **并发压测**:RTT 分位数 + 吞吐 + 按业务码拒收统计 |

## 并发压测(chirp_load_client)

N 条连接登录为 N 个互不相同的用户,环形配对(i → i+1)互发私聊——发送方与接收方全部在线,测的是纯服务端处理 RTT。

```bash
# 起一个 basic chat(默认 7000):
./build/bin/chirp_game_chat --port 7000

# 32 个用户、每用户 20 条、间隔 1.1s(避开私聊 1s 节奏):
./build/bin/chirp_load_client --host 127.0.0.1 --port 7000 --conns 32 --rounds 20

# 输出:ok/拒收/传输失败计数、吞吐 msg/s、RTT p50/p90/p99/max、按码拒收明细
```

### 压测前必读:服务端防线会限制吞吐

压测数字直接受 `docs/guide/integration-pitfalls.md` 里的防线约束,**这不是 bug**:

- **私聊节奏 1s/用户**(硬编码阈值,进程内计数):默认 `--interval-ms 1100` 避开;调低会拿到 `RATE_LIMITED`(计入分码统计,不算失败)。节奏没有服务端开关,压大吞吐的唯一办法是加 `--conns`(吞吐 ≈ conns/interval_ms)。
- **模糊闸 120 条/分/用户**(默认):单用户高频压测会先撞它;要压服务端上限,启动时调大 `--send_rate_limit_per_min`。
- **私聊长度上限 200 码点**:`--size` 默认 32;超过 200 会稳定回 `CONTENT_TOO_LONG`。
- **重复禁言**:同一用户连发 3 条相同内容 → 5 分钟禁言。工具已内置逐轮变体内容(轮号前缀)规避;自写压测脚本时同样注意。连续多轮压测建议轮换 `--prefix` 换一批干净用户。

## 相关文档

- [接入避坑指南](../../docs/guide/integration-pitfalls.md)——全部防线阈值与触发条件
- [API 总览](../../docs/api/overview.md)——帧格式与消息 ID
