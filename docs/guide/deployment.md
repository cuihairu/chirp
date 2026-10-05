---
title: 部署指南
---

# 部署指南

> 状态说明:成熟主线是 `game_sdk_gateway + auth + game_chat`,其余服务为 Experimental。本页只写仓库里实际存在的东西:服务配置以命令行参数为主(完整样例见 `docker-compose.yml`;chat 存储层另有 `CHIRP_*` 环境变量读取点,见 `services/shared/chat/src/message_store_config.cc`),Kubernetes 清单在 `deploy/k8s/`——那是早期模板,env 配置名与当前代码对不上,本仓 CI 也不验证它,不要直接当生产基线;没有 Grafana 仪表盘。各服务能力边界以[能力矩阵](../CAPABILITY_MATRIX.md)为准。

## Docker Compose 部署

`docker-compose.yml` 起基础设施(Redis、MySQL)加全部服务;`docker-compose.cluster.yml` 是单机集群形态,额外带 haproxy 和 adminer:

```bash
docker compose up --build -d
docker compose logs -f
docker compose down
```

第一次部署时,先关注 redis、auth、gateway、chat 四个服务,见 [CORE.md](../CORE.md)。

`docker-compose.cluster.yml`(单机集群形态,带 haproxy)、`deploy/haproxy.cfg`、`deploy/deploy-cluster.sh`、`deploy/k8s/*.yaml` 和 `scripts/build-cluster.sh` 都在树里;其中 k8s 清单是早期模板(见顶部状态说明),haproxy 配置与集群 compose 脚本与当前服务名/端口的对应关系用前先核对一遍。

## 手动部署

先构建:

```bash
./gen_proto.sh
cmake --preset dev
cmake --build --preset dev
```

二进制按源码路径落在构建目录里,如 `build/services/shared/chat/chirp_chat`。每个服务的配置都是命令行参数,下面两条取自 `docker-compose.yml` 的原样参数:

```bash
# App 平面认证
build/services/app/auth/chirp_app_auth \
  --port 6000 --jwt_secret <共享密钥> --allow_scaffold_login 1

# 聊天服务(Redis + MySQL + 服务器平面全增强路径)
build/services/shared/chat/chirp_chat --port 7000 --ws_port 7001 \
  --redis_host redis --redis_port 6379 \
  --mysql_host mysql --mysql_port 3306 --mysql_database chirp \
  --mysql_user chirp --mysql_password <密码> \
  --server_gateway_host game_server_gateway --server_gateway_secret <secret> \
  --gateway_service_secret <secret> --npc_service_id npc_dialog
```

其余服务(`chirp_game_sdk_gateway`、`chirp_game_server_gateway`、`chirp_app_sdk_gateway`、`chirp_app_notification` 等)同法启动,参数与端口见各服务 `main.cc` 顶部的参数解析和 `docker-compose.yml`。

限流开关在 chat 上:`--login_rate_limit_per_min`(默认 30 次/分钟,按客户端 IP)和 `--send_rate_limit_per_min`(默认 120 次/分钟,按用户)。窗口数据放 Redis,Redis 故障时放行(fail-open);完全不配 `--redis_host` 时不启用限流。

## 生产检查清单

- MySQL 与 Redis 改掉仓库里的示例密码(`chirp_pass` 等),`--jwt_secret` 换成真实密钥
- 限流按负载调整,注意 fail-open 语义意味着 Redis 挂掉时限流同时失效
- TLS:`chirp_app_sdk_gateway` 支持 `--tls_port` / `--ws_tls_port` + `--tls_cert` / `--tls_key`;其余边缘服务目前是明文 TCP/WS,外网暴露时前置 TLS 代理
- 日志级别按需调整;关键错误接告警是部署侧运维,仓库不内置
- Redis 内存与 MySQL 连接数纳入常规监控

## 监控

`chirp_chat`、`chirp_game_sdk_gateway`、`chirp_app_auth`、`chirp_app_notification`、`chirp_app_sdk_gateway`、`chirp_social`、`chirp_party`、`chirp_voice`、`chirp_game_server_gateway`、`chirp_npc_dialog`、`chirp_chat_distributed` 支持 `--metrics_port <port>` 打开 `libs/common` 的 `MetricsHttpServer`(Prometheus 文本格式,`/metrics` 端点);默认 0 即不监听,绑口失败只记 Warn 不阻断主服务。当前导出:chat 三个(`chirp_chat_sessions` 会话槽位 gauge / `chirp_chat_logins_total` / `chirp_chat_packets_total`)、game gateway 三个(`chirp_game_gateway_sessions/logins_total/packets_total`,口径与 chat 相同,logout 与断连两条释放路径都减账)、auth 两个(`chirp_app_auth_logins_total` 仅计成功登录 / `chirp_app_auth_packets_total`)、notification 五个(`chirp_app_notification_packets_total` / `chirp_app_notification_devices_registered_total` / `chirp_app_notification_sent_total` / `chirp_app_notification_failed_total` / `chirp_app_notification_fcm_sent_total`+`chirp_app_notification_apns_sent_total`——后两件只在对应平台发送成功时出现)、app gateway 三个(`chirp_app_gateway_sessions/logins_total/packets_total`,口径与 chat 相同)、social/party 各三个(`chirp_social_*`/`chirp_party_*` 的 sessions/logins_total/packets_total,口径与 chat 相同)、voice 三个(`chirp_voice_sessions` 绑定会话 gauge——一连接一用户,重复 LOGIN 与 join-after-login 不重计,断连/顶号必减 / `chirp_voice_logins_total` / `chirp_voice_packets_total`)、server gateway 两个(`chirp_server_gateway_sessions` 已认证服务面连接 gauge(认证成功才加,断连/顶替/超时都走同一释放漏斗) / `chirp_server_gateway_packets_total`)、npc_dialog 两个(`chirp_npc_dialog_events_total` 收到事件 / `chirp_npc_dialog_replies_total` 注入回复)、distributed chat 三个(`chirp_chat_distributed_sessions` 绑定会话 gauge(重复 LOGIN 不重计,logout/断连同漏斗必减) / `chirp_chat_distributed_logins_total` / `chirp_chat_distributed_packets_total`)。尚未接线:search(服务主循环是睡眠壳,无监听器也无 wire 协议,索引库本身有单测覆盖——接线待协议拍板);更细的观测来自日志和 Redis/MySQL 自身的手段。basic fallback 入口已补齐:无 MySQL 构建时 `chirp_chat` 回落 `main.cc`、`chirp_app_auth` 回落 `main.cc`,两者同款 `--metrics_port`+同名指标(同一 target 的二选一构建,永不同时运行),chat basic 顶号安全口径与 enhanced 一致(单 Bind 点+`HandleDisconnect` 单漏斗,logout 与断连同漏斗减账)。`docker-compose.yml` 开发栈十服务默认打开 metrics 并发布到宿主 19101~19110(chat 19101/game gateway 19102/auth 19103/social 19104/party 19105/voice 19106/notification 19107/app gateway 19108/server gateway 19109/npc_dialog 19110,cluster 生产栈未动)。

## 负载均衡

TCP 长连接服务用四层负载均衡即可,HAProxy 示例:

```
frontend chirp_gateway
    bind *:5000
    mode tcp
    default_backend gateway_servers

backend gateway_servers
    mode tcp
    balance roundrobin
    server gateway1 10.0.1.10:5000 check
    server gateway2 10.0.1.11:5000 check
```

多网关实例要配 Redis(`--redis_host`),否则跨实例 kick 和会话共享不生效。

## 容量参考

数据来自 2026-09-27 的[网关容量基准](../design-notes/capacity_benchmark.md),口径是单机实测:

- 并发在线连接:12000/12000 登录在线,0 握手超时(200 conn/s 铺开 60 秒的纯容量档)
- 建连速率:无并发流量时 200 conn/s 干净;建连与消息发送重叠时 50 conn/s 也会打穿(steady500 档 165/500 管道握手超时)
- 稳态私聊吞吐:受服务端 pacing(私聊 1 秒/用户,硬编码)约束,上限约为 连接数/1.1 条每秒

social、party、voice 没有同口径实测,不提供数字。规划新部署时按上述瓶颈项(建连与流量重叠、pacing 上限)估算,并以自己的压测复核。

## 备份

```bash
# MySQL 每日备份与恢复
mysqldump -u chirp -p chirp > backup_$(date +%Y%m%d).sql
mysql -u chirp -p chirp < backup_20240318.sql

# Redis 快照
redis-cli BGSAVE
cp /var/lib/redis/dump.rdb backup/
```

schema 升级用 `scripts/` 下的 `upgrade_db_*.sql`(如 `upgrade_db_messages.sql`)。这些文件不是幂等的——MySQL 的 `ALTER TABLE ... ADD COLUMN` 没有 `IF NOT EXISTS`,重复执行会报列已存在,执行前先核对当前表结构。

## 回滚

没有发布版本化镜像的流程时,回滚就是回到上一个提交重新部署:

```bash
docker compose down
git checkout <上一个可用提交>
docker compose up --build -d
docker compose logs --tail=50
```

数据库列是新增式的(见升级脚本),旧版本二进制对多出来的列有读端容忍(按 `row.size()` 守卫),因此先回滚二进制、后决定是否回滚数据是可行的;数据回滚只能走备份恢复。

## 故障排查

- CPU 高:先看连接数和 Redis 内存,再用 perf/FlameGraph 对具体进程采样
- 内存高:检查 Redis `maxmemory`,MySQL buffer pool,再查进程堆
- 连接掉线:核对负载均衡健康检查与空闲超时,看服务日志;注意 chat 的私聊 pacing(1 秒/用户)是有意行为,不是故障
- 限流误伤:确认 Redis 状态(fail-open 不生效说明 Redis 是通的,拒绝来自真实限流)与 `--login_rate_limit_per_min` / `--send_rate_limit_per_min` 阈值

## 相关页面

- [总体架构](../architecture.md)
- [可扩展性笔记](../design-notes/SCALABILITY.md)
- [API 总览](../api/overview.md)
