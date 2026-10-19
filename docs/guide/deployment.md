> 🌐 Language: **中文** (this page) | [English](./deployment.en.md)

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

二进制按源码路径落在构建目录里,如 `build/services/shared/chat/chirp_chat`。每个服务的配置都是命令行参数,下面两条取自 `docker-compose.yml` 的参数（已剔除仅开发用的 `--allow_scaffold_login`）:

```bash
# App 平面认证
build/services/app/auth/chirp_app_auth \
  --port 6000 --jwt_secret <共享密钥>

# 聊天服务(Redis + MySQL + 服务器平面全增强路径)
build/services/shared/chat/chirp_chat --port 7000 --ws_port 7001 \
  --redis_host redis --redis_port 6379 \
  --mysql_host mysql --mysql_port 3306 --mysql_database chirp \
  --mysql_user chirp --mysql_password <密码> \
  --server_gateway_host game_server_gateway --server_gateway_secret <secret> \
  --gateway_service_secret <secret> --npc_service_id npc_dialog
```

其余服务(`chirp_game_sdk_gateway`、`chirp_game_server_gateway`、`chirp_app_sdk_gateway`、`chirp_app_notification` 等)同法启动,参数与端口见各服务 `main.cc` 顶部的参数解析和 `docker-compose.yml`。

所有带监听器的服务都支持 `--host <addr>`(默认 `0.0.0.0` 全网卡),参数直接传给 TCP/WS 监听器构造、实控绑定地址(2026-10-19 起全部服务接线完毕;此前多数服务解析了该 flag 却未传给监听器);非法地址在启动时即失败,不静默回落全网卡。收窄暴露面:只经网关触达的后台服务(`chirp_app_auth`、`chirp_chat`/`chirp_chat_distributed`、`chirp_social`、`chirp_party`、`chirp_voice`、`chirp_app_notification`、`chirp_search`)建议绑 `127.0.0.1` 或内网 IP;直接面向客户端的边缘入口(`chirp_game_sdk_gateway`、`chirp_app_sdk_gateway`)与带服务面认证的 `chirp_game_server_gateway` 保持 `0.0.0.0` 或按网络拓扑绑定。同一服务的 TCP 与 WS 监听(如 chat 的 `--port`/`--ws_port`)绑定同一 `--host` 地址。

**推送平面服务间认证（2026-10-19,方案已代决为共享密钥握手）**:`chirp_app_notification` 配 `--service_secret <secret>` 后,每条新连接必须先以 `SERVER_AUTH_REQ`(共享密钥,与 `chirp_game_server_gateway` 的服务面认证同一消息族)完成握手才能调用 6xxx 设备/推送接口;密钥不符或未握手的包会被拒绝并断开。配置了 `--notification_host` 的调用方(`chirp_app_sdk_gateway`、`chirp_chat`/`chirp_chat_distributed`)用 `--notification_secret <secret>` 传同一密钥;握手失败按普通传输失败处理(回调 `INTERNAL_ERROR`,调用方不区分密钥错误与断连)。不传 `--service_secret` 时平面保持开放(兼容既有开发环境)。Compose 两份示例(`docker-compose.yml`、`deployments/docker/docker-compose.deploy.yml`)已配齐成对密钥,且不再把 5006/5016 发布到宿主机(开发 compose)。

**`--allow_scaffold_login` 是开发脚手架,不是部署参数**:开启后 auth 对既非有效 JWT 也非活跃会话的 token 直接按「token 即 user_id」放行(`services/app/auth/src/main_enhanced.cc` 的 LOGIN_REQ 回退分支),任何持有该服务网络可达性的调用方都可以冒充任意用户。仅限完全隔离的本地开发演示使用,生产一律不传此参数(`docker-compose.yml` 里带它是因为那份 compose 本身是开发演示形态)。

**设备注册表持久化(2026-10-19)**:`chirp_app_notification` 支持 `--device_store_file <path>`(缺省空 = 纯内存,行为与不传时逐位一致):注册/注销/换 token 在注册表锁内快照写盘,启动时先回灌再开监听,重启不丢推送目标。演示站部署 compose(`deployments/docker/docker-compose.deploy.yml`)已将该 flag 指到命名卷 `notification_data` 内的 `/var/lib/chirp/notification/devices.json`,容器重建不再静默清空设备表;裸机部署传一个可写路径即可,开发 compose 保持无状态(整栈重置即干净)。

**推送出向与提供方凭据(2026-10-19,凭据侧待用户项)**:`chirp_app_notification` 的真实投递分传输与凭据两层。传输:`--push_transport <logging|http>`,默认 `logging`=只记日志不外发(桩期语义);`http` 走真实 HTTP/1.1 POST(手写 asio+OpenSSL 栈,`services/app/notification/src/http_push_transport.h`:https 端点 TLS 1.2+ 证书校验+SNI,信任面 `--push_ca_file <path>`(空=OpenSSL 默认信任库)与 `--push_verify_tls on|off`)。两份 compose 示例(`docker-compose.yml`、`deployments/docker/docker-compose.deploy.yml`)均未传该 flag——默认即不外发,要真实投递必须显式 `--push_transport http`。FCM 凭据:`--fcm-key <key>`(+可选 `--fcm-endpoint <url>`,默认 legacy `https://fcm.googleapis.com/fcm/send`,HTTP/1.1 兼容)。APNs 凭据:`--apns-key <path>`(.p8 PEM 私钥文件)+ `--apns-key-id` + `--apns-team-id` + `--apns-bundle-id`;沙盒 `--apns-sandbox`(端点切 `https://api.development.push.apple.com:443`),默认生产端点 `https://api.push.apple.com:443`,可用 `--apns-endpoint <url>` 覆盖;ES256 provider JWT 签名已落地(`apns_jwt.{h,cc}`,50 分钟 Bearer 缓存)。**HTTP/2 约束(如实)**:APNs 提供方 API 只收 HTTP/2,仓内传输是 HTTP/1.1(设计立场与 `main.cc` 传输选择处注释一致:不为单一出向端点引入 h2/HPACK 客户端栈;`services/app/notification/CMakeLists.txt` 里的 `find_package(CURL)` 是历史残留——`vcpkg.json` 无 curl 依赖,源码零引用 `USE_CURL`)。生产真实投递 APNs 需在出向前置协议转换:本地反代(envoy/haproxy 类,h2 上游)暴露 HTTP/1.1 入口,`--apns-endpoint` 指到该入口;FCM legacy 端点无此约束。失败语义:每条推送单次尝试、无重试队列;成功/失败计 `chirp_app_notification_sent_total`/`chirp_app_notification_failed_total`(分平台另有 `..._fcm_sent_total`/`..._apns_sent_total`)。当前凭据(Team ID/Key ID/.p8/Bundle ID)为用户项未到位(TODO.md:195),本段即凭据到位时的接线 runbook。

限流开关在 chat 上:`--login_rate_limit_per_min`(默认 30 次/分钟,按客户端 IP)和 `--send_rate_limit_per_min`(默认 120 次/分钟,按用户)。窗口数据放 Redis,Redis 故障时放行(fail-open);完全不配 `--redis_host` 时不启用限流。

## 生产检查清单

- MySQL 与 Redis 改掉仓库里的示例密码(`chirp_pass` 等),`--jwt_secret` 换成真实密钥
- `--jwt_secret` 用高熵随机值(建议 ≥32 字节,如 `openssl rand -hex 32`),auth 现已拒绝无密钥启动;轮换时同步更新所有共享该密钥的服务(auth 与边缘网关的 `--token_secret` 同源)
- auth 不带 `--allow_scaffold_login`(默认即关;若从 `docker-compose.yml` 抄参数,删掉它)
- 限流按负载调整,注意 fail-open 语义意味着 Redis 挂掉时限流同时失效
- 后台服务用 `--host` 收窄绑定面:只经网关触达的服务绑 `127.0.0.1` 或内网 IP,边缘网关保持 `0.0.0.0`(见上文绑定说明)
- TLS:`chirp_app_sdk_gateway` 支持 `--tls_port` / `--ws_tls_port` + `--tls_cert` / `--tls_key`;其余边缘服务目前是明文 TCP/WS,外网暴露时前置 TLS 代理
- 日志级别按需调整;关键错误接告警是部署侧运维,仓库不内置
- Redis 内存与 MySQL 连接数纳入常规监控

## 监控

`chirp_chat`、`chirp_game_sdk_gateway`、`chirp_app_auth`、`chirp_app_notification`、`chirp_app_sdk_gateway`、`chirp_social`、`chirp_party`、`chirp_voice`、`chirp_game_server_gateway`、`chirp_npc_dialog`、`chirp_chat_distributed`、`chirp_search` 支持 `--metrics_port <port>` 打开 `libs/common` 的 `MetricsHttpServer`(Prometheus 文本格式,`/metrics` 端点);默认 0 即不监听,绑口失败只记 Warn 不阻断主服务。当前导出:chat 三个(`chirp_chat_sessions` 会话槽位 gauge / `chirp_chat_logins_total` / `chirp_chat_packets_total`)、game gateway 三个(`chirp_game_gateway_sessions/logins_total/packets_total`,口径与 chat 相同,logout 与断连两条释放路径都减账)、auth 两个(`chirp_app_auth_logins_total` 仅计成功登录 / `chirp_app_auth_packets_total`)、notification 五个(`chirp_app_notification_packets_total` / `chirp_app_notification_devices_registered_total` / `chirp_app_notification_sent_total` / `chirp_app_notification_failed_total` / `chirp_app_notification_fcm_sent_total`+`chirp_app_notification_apns_sent_total`——后两件只在对应平台发送成功时出现)、app gateway 三个(`chirp_app_gateway_sessions/logins_total/packets_total`,口径与 chat 相同)、social/party 各三个(`chirp_social_*`/`chirp_party_*` 的 sessions/logins_total/packets_total,口径与 chat 相同)、voice 三个(`chirp_voice_sessions` 绑定会话 gauge——一连接一用户,重复 LOGIN 与 join-after-login 不重计,断连/顶号必减 / `chirp_voice_logins_total` / `chirp_voice_packets_total`)、server gateway 两个(`chirp_server_gateway_sessions` 已认证服务面连接 gauge(认证成功才加,断连/顶替/超时都走同一释放漏斗) / `chirp_server_gateway_packets_total`)、npc_dialog 两个(`chirp_npc_dialog_events_total` 收到事件 / `chirp_npc_dialog_replies_total` 注入回复)、distributed chat 三个(`chirp_chat_distributed_sessions` 绑定会话 gauge(重复 LOGIN 不重计,logout/断连同漏斗必减) / `chirp_chat_distributed_logins_total` / `chirp_chat_distributed_packets_total`)、search 三个(`chirp_search_packets_total` / `chirp_search_logins_total` / `chirp_search_queries_total`——search 本体已在 5007 监听,2248/2249 经双 gateway 的 `--search_host`(默认 5007)接入,桥失败降级回 `SERVER_UNAVAILABLE` 不踢客户端);更细的观测来自日志和 Redis/MySQL 自身的手段。basic fallback 入口已补齐:无 MySQL 构建时 `chirp_chat` 回落 `main.cc`、`chirp_app_auth` 回落 `main.cc`,两者同款 `--metrics_port`+同名指标(同一 target 的二选一构建,永不同时运行),chat basic 顶号安全口径与 enhanced 一致(单 Bind 点+`HandleDisconnect` 单漏斗,logout 与断连同漏斗减账)。`docker-compose.yml` 开发栈十服务默认打开 metrics,其中九个发布到宿主(19101~19106 与 19108~19110:chat 19101/game gateway 19102/auth 19103/social 19104/party 19105/voice 19106/app gateway 19108/server gateway 19109/npc_dialog 19110);notification 的 19107 已配置但自 2026-10-19 起不再发布到宿主(6xxx 平面撤销 host 发布后,该 metrics 端口仅在 compose 网络内可达;cluster 生产栈未动)。

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
