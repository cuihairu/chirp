---
title: 部署指南
---

# 部署指南

> 状态说明:本页是部署指南草稿。当前受支持的路径是 `gateway + auth + chat`;social、voice、notification、search 服务仍是实验性的。命令和端点(endpoint)假设请对照 [总体架构](../architecture.md) 与 [能力矩阵](../CAPABILITY_MATRIX.md) 核实。

本指南覆盖把 Chirp 部署到生产环境。

## 部署方式

### 1. Docker 部署(推荐)

用 Docker Compose 做多服务部署:

```bash
# Build and start all services
docker-compose up -d

# View logs
docker-compose logs -f

# Stop services
docker-compose down
```

### 2. Kubernetes 部署

面向大规模部署:

```bash
# Apply Kubernetes manifests
kubectl apply -f k8s/

# Check deployment status
kubectl get pods -l app=chirp

# Scale services
kubectl scale deployment chirp-gateway --replicas=3
```

### 3. 手动部署

逐个部署服务:

```bash
# Start each service
./services/gateway/chirp_gateway &
./services/chat/chirp_chat &
./services/social/chirp_social &
./services/voice/chirp_voice &
./services/auth/chirp_auth &
./services/notification/chirp_notification &
```

## 生产检查清单

### 安全

- [ ] MySQL 和 Redis 使用强密码
- [ ] 所有服务启用 TLS/SSL
- [ ] 配置防火墙规则
- [ ] 启用限流(rate limiting)
- [ ] 设置正确的 CORS 策略

### 监控

- [ ] 启用 Prometheus 指标端点
- [ ] 配置合适级别的日志
- [ ] 为关键故障设置告警
- [ ] 监控 Redis 内存用量
- [ ] 监控 MySQL 连接数

### 可扩展性

- [ ] 配置 Redis 集群实现高可用
- [ ] 启用 MySQL 主从复制(master-replica replication)
- [ ] 用 HAProxy/nginx 做负载均衡(load balancing)
- [ ] 为 pod/容器配置自动扩缩容(autoscaling)
- [ ] 静态资源启用 CDN

## 服务配置

### Gateway 服务

**环境变量:**
```bash
GATEWAY_HOST=0.0.0.0
GATEWAY_PORT=5000
GATEWAY_WS_PORT=5001
REDIS_HOST=redis
REDIS_PORT=6379
LOG_LEVEL=info
```

**Docker Compose 配置:**
```yaml
gateway:
  image: chirp/gateway:latest
  ports:
    - "5000:5000"
    - "5001:5001"
  environment:
    - REDIS_HOST=redis
    - LOG_LEVEL=info
  depends_on:
    - redis
```

### Chat 服务

**环境变量:**
```bash
CHAT_HOST=0.0.0.0
CHAT_PORT=7000
CHAT_WS_PORT=7001
MYSQL_HOST=mysql
MYSQL_PORT=3306
MYSQL_DATABASE=chirp
MYSQL_USER=chirp
MYSQL_PASSWORD=chirp123
REDIS_HOST=redis
```

### Auth 服务

**环境变量:**
```bash
AUTH_HOST=0.0.0.0
AUTH_PORT=6000
JWT_SECRET=your-secret-key
JWT_EXPIRATION=86400
REDIS_HOST=redis
MYSQL_HOST=mysql
```

## 负载均衡配置

### HAProxy 示例

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
    server gateway3 10.0.1.12:5000 check
```

### nginx 示例

```nginx
upstream gateway {
    least_conn;
    server 10.0.1.10:5000;
    server 10.0.1.11:5000;
    server 10.0.1.12:5000;
}

server {
    listen 5000;
    proxy_pass gateway;
    proxy_timeout 3s;
}
```

## 监控设置

### Prometheus 配置

```yaml
scrape_configs:
  - job_name: 'chirp_gateway'
    static_configs:
      - targets: ['localhost:9090']
  - job_name: 'chirp_chat'
    static_configs:
      - targets: ['localhost:9091']
  - job_name: 'chirp_social'
    static_configs:
      - targets: ['localhost:9092']
```

### Grafana 仪表盘

导入提供的仪表盘,监控:
- 消息吞吐
- 连接数
- API 延迟
- 错误率
- 资源用量

## 扩容参考

### Gateway 服务

- **单实例**:约 10K 并发连接
- **建议**:负载均衡后面挂 3-5 个实例
- **扩容依据**:连接数

### Chat 服务

- **单实例**:约 5K 消息/秒
- **建议**:2-3 个实例 + Redis pub/sub
- **扩容依据**:消息队列长度

### Social 服务

- **单实例**:约 10K 在线状态更新/秒
- **建议**:2 个实例做高可用
- **扩容依据**:好友列表规模

## 备份策略

### MySQL 备份

```bash
# Daily backup
mysqldump -u chirp -pchirp123 chirp > backup_$(date +%Y%m%d).sql

# Restore
mysql -u chirp -pchirp123 chirp < backup_20240318.sql
```

### Redis 备份

```bash
# Snapshot
redis-cli BGSAVE

# Copy dump file
cp /var/lib/redis/dump.rdb backup/
```

## 回滚流程

### 服务回滚

```bash
# Stop current version
docker-compose down

# Deploy previous version
docker-compose -f docker-compose.v1.yml up -d

# Verify by checking service logs and running the smoke tests again
docker-compose logs --tail=50
```

### 数据库回滚

```bash
# Stop MySQL
sudo systemctl stop mysql

# Restore from backup
mysql -u chirp -pchirp123 chirp < backup.sql

# Start MySQL
sudo systemctl start mysql
```

## 故障排查

### CPU 过高

1. 检查各服务连接数
2. 查看 Redis 内存占用
3. 在 MySQL 打开查询日志
4. 用 perf/FlameGraph 采样

### 内存过高

1. 检查 Redis maxmemory 设置
2. 查看 MySQL buffer pool 大小
3. 打开堆采样
4. 排查内存泄漏

### 连接掉线

1. 核对负载均衡健康检查
2. 看服务日志里的错误
3. 监控网络延迟
4. 复查限流配置

## 下一步

- [总体架构](../architecture.md)
- [可扩展性笔记](../design-notes/SCALABILITY.md)
- [API 参考](../api/overview.md)
