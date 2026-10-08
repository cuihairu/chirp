# 网关并发连接容量基准（实测）

> 2026-09-27 实测。回填 [roadmap_history](/design-notes/roadmap_history) 「压测：网关 10k+ 并发连接基准」一项。
> **所有数字来自共享开发机上的单实例回环测试**，环境噪声与口径限制见[局限](#局限与适用范围)，对外引用时必须连同该节一起。

## 结论速览

| 指标 | 实测值 | 条件 |
| --- | --- | --- |
| 并发在线连接 | **12000/12000** 登录在线，0 握手超时 | 200 conn/s 铺开 60s，rounds=0 纯容量档 |
| 建连速率（无并发流量） | **200 conn/s 持续干净**（500/1000/2000/12000 档全部 0 握手超时） | 客户端只建连不发消息 |
| 建连速率（有并发流量） | **50 conn/s 也会打穿**：steady500 档 165/500 管道握手超时 | 建连斜坡与消息发送重叠 |
| 稳态私聊吞吐 | 受服务端 pacing（私聊 1s/用户，硬编码）约束，上限 ≈ **conns/1.1 msg/s** | 环形互发、interval 1100ms |
| 稳态发送 RTT | 见[稳态档](#稳态发送档)分档表；安静机器 200 连接 p50 ≈ 2-3ms，本机 loadavg≈60 时 p50 ≈ 80-170ms | RTT 含回环网络+两个服务进程 |
| 内存 | chat 峰值 RSS ≈ 19.7MB、gateway ≈ 8.6MB @12k 连接 | VmHWM，含每连接管道与 会话状态 |

「10k+ 并发连接」的口径：**12000 条 TCP 连接同时登录在线**（每条都完成 LOGIN_RESP、持有网关会话与网关→chat 管道）。不是 12000 用户同时高频发消息——发送面容量见吞吐行与[瓶颈分析](#瓶颈与边界)。

## 被测拓扑

与 smoke-game 同构的最小游戏平面：

```
chirp_load_client ──TCP──> chirp_game_sdk_gateway ──每客户端一条管道──> chirp_chat
   N 连接                   (端口 G/WS)        ServiceBridge 2xxx 转发      (端口 P/WS)
```

- 客户端线协议与生产一致：`[u32_be len][chirp.gateway.Packet]`；登录走网关 scaffold（token=user_id）。
- chat 与 gateway 各自是**单线程 `io_context::run()`**（`services/shared/chat/src/main.cc`、`services/game/sdk_gateway/src/main.cc`）。
- 网关对每个客户端向 chat 建一条管道，管道经 `SERVER_AUTH_REQ`（`--gateway_service_secret`）认证；管道握手超时硬编码 5s（`libs/network/chat_bridge.cc:24`）。

### 依赖形态（关键）

`chirp_chat`（enhanced 主程序）的 Redis/MySQL 参数**默认指向 127.0.0.1**（`main_enhanced.cc`），不显式配置就会连共享实例；共享 MySQL 若凭据不符，每条消息都触发**同步**落库重试，把 chat 单线程拖死——实测 200 连接档因 此 9 分钟不退出、65% 管道握手超时。因此正式测量使用专属一次性依赖：

- 专属 redis：`redis-server --port 16379 --save "" --appendonly no`
- 专属 MariaDB：`mariadbd --datadir=… --port=13306 --skip-grant-tables`（store 自动 `CREATE TABLE IF NOT EXISTS`），`CREATE DATABASE chirp` 即可

已知无害噪声：`HybridMessageStore` 启动自检用 `GET ping`，把「key 不存在」的 nil 回复误判为连接失败打一行 WARN（`hybrid_message_store.cc:105`）；实际读写照常（且 `RedisClient` 每条命令新建短连接，见[瓶颈](#瓶颈与边界)）。

## 工具与方法

- 工具：`chirp_load_client`（`tools/benchmark/load_client.cc`）——每连接一线程，支持 `--ramp-ms` 铺开建连、`--barrier on|off`、`--rounds 0` 纯连接档、逐轮 RTT 分位、登录 RTT 分位、发送窗口吞吐。
- 运行器：`tools/benchmark/run_capacity_bench.sh`——端口预检+归属断言（bind 冲突时探测会假成功，曾产出「5000/5000 在线、登录 p99=42s」的废数据）、taskset 分核（服务 0-3 / 客户端 5-13）、VmHWM 与 `/proc/$pid/stat` CPU 核·秒、网关握手超时计数。
- 并发守卫：`tools/benchmark/bench_guard.sh`（被 source 的两件套，run_capacity_bench.sh 与各 probe 脚本共用）——`bench_acquire_lock` 用 flock 对 `/tmp/chirp_bench.lock` 机器级互斥（`LOCK_WAIT=1` 排队，`LOCK_TIMEOUT` 默认 3600s）；`bench_preflight_ports` 逐端口查监听者，空闲放行，被占时只有「本 uid + 基准二进制」的孤儿在 `FORCE=1` 下才清理，其余一律报错退出（共享机纪律：绝不代杀别的会话的活体进程）。各 probe 的端口块：steady 17880-83、scale 17850-53、perf 17840-43、est_rate 17870-73、stack 17830-33、cpu_attribution 17810-13、run_capacity_bench 17800-03（`CHAT_PORT` 等可覆盖）。阶梯驱动（ladder_local / capacity_ladder_local / capacity_ramp_ladder）不重复拿锁——每档内层运行器自带守卫，外层再拿同一把 flock 会自锁。
- 每档独立起停服务；用户前缀按档轮换，规避跨档限流/禁言残留。

复现（示例）：

```bash
REDIS_HOST=127.0.0.1 REDIS_PORT=16379 MYSQL_HOST=127.0.0.1 MYSQL_PORT=13306 \
BIN_DIR=./build-rel bash tools/benchmark/run_capacity_bench.sh 12000 0 1100 60000
```

## 实测数据

### 连接容量档（rounds=0，只建连不发消息）

| 档 | 建连速率 | 在线 | 握手超时 | 登录RTT p50/p99/max | chat 峰值RSS | gateway 峰值RSS | chat CPU 核·秒 | 环境loadavg |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 500 | 8.3 conn/s | 500/500 | 421 | 3/7/11 ms | 38.0MB | 9.1MB | 22.03 (60s) | ~59 |
| 1000 | 16.7 conn/s | 1000/1000 | 843 | 4/9/17 ms | 37.5MB | 9.9MB | 18.66 (60s) | ~59 |
| 2000 | 33.3 conn/s | 2000/2000 | 1714 | 4/10/63 ms | 36.5MB | 11.0MB | 13.61 (60s) | ~84 |
| **12000** | 200 conn/s | **12000/12000** | **10636** | 3/17/54 ms | **73.0MB** | **24.5MB** | 30.68 (63.5s) | ~73 |

> **注**：本轮测试使用 `ramp_ms=60000` 把 12k 建连铺开到 60 秒，避免同瞬冲击握手上限。`handshake_timeouts` 计数因 ramp 铺开窗口与 5s 手shake 超时的交互而产生。`chat CPU 核·秒` 为 ctest 期间的累计增量。环境 loadavg 受多会话并行构建影响。

> **对比**：先前表格的数字来自无 MySQL/Redis 依赖的旧构建；本表为增强聊天（HybridMessageStore + MySQL）在共享 14 核开发机上实测，负载受多会话共享。

要点：

- 建连本身很便宜：12000 条连接只花 chat 8.24 核·秒（≈130µs/连接，含管道握手与会话状态）。
- 内存几乎不随连接数涨（chat 15.7→19.7MB、gateway 8.6MB 持平）：每连接状态在两位数 KB 量级，12k 连接远未逼近内存边界。

### 稳态发送档

（数字见下表；两档均在 loadavg≈60 的共享机上取得，RTT 含客户端 500/1000 线程自扰动，**只作数量级参考**。）

| 档 | 存活发送者 | 握手超时(建连期) | 稳态轮 p50 | 稳态轮 p99 | 窗口吞吐 |
| --- | --- | --- | --- | --- | --- |
| steady500（50/s 铺开+ramp 期即发送） | 335/500 | 165 | 112-173 ms | 465-546 ms | 82.9 msg/s |
| steady1000（同上） | 536/1000 | 464 | 112-164 ms | 535-749 ms | 73.4 msg/s |
| 安静机参照（200 连接、loadavg≈8、共享依赖时代旧数据） | 200/200 | 0 | **2-3 ms** | 5-8 ms | 141 msg/s |

稳态窗口吞吐远低于 pacing 上限（500/1.1≈455 msg/s）的原因：客户端轮次非流水（等响应再睡 1.1s，周期=RTT+1100ms）、ramp 扩散计入窗口、以及机器负载抬高 RTT。**服务端 pacing（私聊 1s/用户硬编码）才是吞吐的结构性上限**：环形互发下限 ≈ conns/1.1s，与实测相符（如 64 连接@1100ms → 58.25 msg/s ≈ 64/1.1）。

## 瓶颈与边界

1. **chat 单 io 线程把建连与消息处理串行化**。纯建连 200/s 全绿；但建连斜坡与消息发送重叠时（steady500/1000 档），消息处理挤占 io 线程，管道握手的 5s 硬截止被打穿（50 conn/s 也 165/500 超时）。生产含义：**高峰期登录风暴会与消息流量互相放大**。缓解方向：管道握手超时可配化、握手与消息处理分线程/分优先级。
2. **栅栏同步风暴**（`--barrier on`）：全员会合后锁相轮发，2k 连接时每轮都是 2000 条齐发，RTT 尾部到 60s（排队排水时间）。这是测量方法要避开的坑，也是真实「同刻齐发」场景的预期行为。
3. **吞吐结构上限 = pacing**：私聊 1s/用户硬编码（`ChannelPacer`，进程内），多实例部署下每实例独立计数；Redis 模糊闸（120/min/用户）与登录 IP 限流需 `--redis_host` 才生效。
4. **`RedisClient` 每命令新建 TCP 短连接**（`redis_client.cc` `SendCmd`），无连接池/管道复用——消息路径上每次 RPush 都是一次 connect+命令+close，这是单线程 CPU 的重要去向，也是吞吐抬升的明显优化点。
5. **MySQL 必须配好**：落库失败是同步重试，坏依赖会让 chat 单线程卡死（见[依赖形态](#依赖形态关键)）。
6. 客户端为线程/连接模型：万级线程自身抢核会抬高 RTT（曾实测轮询栅栏把 loadavg 打到 1181），故分核 taskset + 条件变量栅栏；万级以上建议改事件驱动客户端再测。

## 局限与适用范围

- **共享 14 核开发机**：环境 loadavg 每档记录在案（55-108），RTT/吞吐数字含不可剥离的邻居噪声；连接容量、握手超时、RSS、CPU 核·秒等结构性数字不受影响。
- 回环网络：RTT 不含真实网络 RTT；吞吐不含网卡带宽约束。
- 单实例 chat/gateway：多实例水平扩展（Redis 全局限流口径）未测。
- scaffold 登录路径（无真实 auth 服务）：真实认证链路的登录 RTT 未测。
- 客户端发送面只覆盖私聊（环形互发）：群播扇出、离线队列投递未纳入本基准。

## 关联

- [roadmap_history](/design-notes/roadmap_history) §运维与测试「压测」项回填
- [SCALABILITY](/design-notes/SCALABILITY) / [game_chat_architecture](/design-notes/game_chat_architecture)
- 工具说明：[tools/benchmark](https://github.com/) 本地 README（`chirp_load_client` 章节）
