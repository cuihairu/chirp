# 部署笔记(历史)

> 本文是早期的部署笔记,混有已过时的模板内容。现行部署指南是 [guide/deployment.md](../guide/deployment.md)(Docker Compose、真实二进制路径、真实命令行参数、实测容量数据);需要下部署结论时以那一页为准。

## 服务依赖

```
┌─────────────┐     ┌─────────────┐
│   Gateway   │────▶│     Auth     │
│  (5000/5001)│     │    (6000)    │
└──────┬──────┘     └─────────────┘
       │
       ├──────────┬──────────┬──────────┐
       ▼          ▼          ▼          ▼
   ┌────────┐ ┌──────┐ ┌────────┐ ┌───────┐
   │ Chat   │ │Social│ │  Voice │ │ Redis │
   │ 7000   │ │ 8000 │ │  9000  │ │ 6379  │
   └────────┘ └──────┘ └────────┘ └───────┘
       │          │          │          ▲
       └──────────┴──────────┴──────────┤
                                          ▼
                                    ┌─────────┐
                                    │  MySQL  │
                                    │  3306   │
                                    └─────────┘
```

## 端口一览

| 服务 | 端口 | 协议 |
|---------|------|----------|
| Gateway(game_sdk_gateway) | 5000, 5001 | TCP, WS |
| Auth(app_auth) | 6000 | TCP |
| Chat(chirp_chat) | 7000, 7001 | TCP, WS |
| Social | 8000, 8001 | TCP, WS |
| Party | 7500, 7501 | TCP, WS |
| Voice | 9000, 9001 | TCP, WS |
| Server Gateway | 8100 | TCP |
| App Notification | 5006, 5016 | TCP, WS |
| App SDK Gateway | 5200, 5201 | TCP, WS |
| Redis | 6379 | TCP |
| MySQL | 3306 | TCP |

## 常用命令

```bash
# 一套起齐(docker compose)
docker compose up -d
docker compose logs -f
docker compose down

# 源码构建与本地验证
./gen_proto.sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

服务配置以命令行参数为准,样例见 `docker-compose.yml` 与 [guide/deployment.md](../guide/deployment.md);chat 存储层另有 `CHIRP_*` 环境变量读取点(`message_store_config.cc`)。日志是纯文本行(`YYYY-MM-DD HH:MM:SS [LEVEL] [tid=...] 消息`,`libs/common/logger.*`),没有 JSON 结构化输出。

## 容量

容量口径以 [capacity_benchmark.md](./capacity_benchmark.md) 的实测为准:单机 12000 并发登录在线、200 conn/s 干净建连(有并发流量时 50 conn/s 也会打穿)、稳态私聊吞吐受 1 秒/用户 pacing 约束。本文不再维护估算表。

## 备份与网络安全

```bash
# MySQL 备份与恢复
mysqldump -u chirp -p chirp > backup_$(date +%Y%m%d).sql
mysql -u chirp -p chirp < backup_20240301.sql

# Redis 快照
redis-cli BGSAVE
```

对外只暴露客户端边缘端口,内部端口(6xxx、7000、8100 等)用防火墙挡住外网:

```bash
ufw allow 5000/tcp
ufw allow 5001/tcp
ufw deny 7000/tcp
```

Redis 高可用(Sentinel)、MySQL 主从复制是部署侧运维选择,仓库不内置对应配置。
