# Chirp 部署指南

> 状态说明:本指南混合了当前的本地部署笔记和生产导向的模板。当运维手册用之前,请对照当前服务参数逐一核实每条命令和端点。受支持的核心路径是 `gateway + auth + chat`;其余服务除 [CAPABILITY_MATRIX.md](../CAPABILITY_MATRIX.md) 中注明外均为实验性。

## 目录

- [部署概览](#部署概览)
- [开发环境](#开发环境)
- [生产部署](#生产部署)
- [Docker 部署](#docker-部署)
- [配置](#配置)
- [监控](#监控)
- [扩容](#扩容)

---

## 部署概览

Chirp 服务有几种部署方式:

1. **Docker Compose**(开发/测试推荐)
2. **Kubernetes**(生产推荐)
3. **手动部署**(传统服务器)

### 服务依赖

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

---

## 开发环境

### 用 Docker Compose 快速起步

1. **克隆仓库:**
   ```bash
   git clone <repository-url>
   cd chirp
   ```

2. **启动服务:**
   ```bash
   docker compose up -d
   ```

3. **看日志:**
   ```bash
   docker compose logs -f
   docker compose logs -f gateway
   ```

4. **停止服务:**
   ```bash
   docker compose down
   ```

### 从源码构建

```bash
# Generate protobuf files
./gen_proto.sh
protoc --proto_path=. --cpp_out=proto/cpp \
  proto/social.proto proto/voice.proto

# Build
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
cmake --build . --config Debug

# Run individual services
./services/gateway/chirp_gateway --port 5000
./services/auth/chirp_auth --port 6000
./services/chat/chirp_chat --port 7000
```

---

## 生产部署

### 要求

**硬件(每服务实例):**
- CPU:至少 2 核,建议 4 核
- 内存:至少 2GB,建议 4GB
- 网络:建议 1 Gbps

**软件:**
- OS:Linux(Ubuntu 20.04+、CentOS 8+)
- Docker:20.10+
- Docker Compose:2.0+(本地测试用)

### 端口要求

| 服务 | 端口 | 协议 | 说明 |
|---------|------|----------|-------|
| Gateway | 5000, 5001 | TCP, WS | 对外 |
| Auth | 6000 | TCP | 内部 |
| Chat | 7000, 7001 | TCP, WS | 内部 |
| Social | 8000, 8001 | TCP, WS | 内部 |
| Voice | 9000, 9001 | TCP, WS | 内部 |
| Redis | 6379 | TCP | 内部 |
| MySQL | 3306 | TCP | 内部 |

---

## Docker 部署

### 生产 Dockerfile

```dockerfile
FROM ubuntu:22.04 AS base

# Install dependencies
RUN apt-get update && apt-get install -y \
    g++ \
    cmake \
    libprotobuf-dev \
    protobuf-compiler \
    libssl-dev \
    libmysqlclient-dev \
    redis-tools \
    && rm -rf /var/lib/apt/lists/*

# Copy source
COPY . /app
WORKDIR /app

# Build
RUN mkdir build && cd build && \
    cmake .. -DCMAKE_BUILD_TYPE=Release && \
    cmake --build . --config Release -j$(nproc)

# Runtime image
FROM ubuntu:22.04
RUN apt-get update && apt-get install -y \
    libprotobuf23 \
    libmysqlclient21 \
    libssl3 \
    redis-tools \
    && rm -rf /var/lib/apt/lists/*

COPY --from=base /app/build/services /services
COPY --from=base /app/build/libs /libs

EXPOSE 5000 5001
CMD ["/services/gateway/chirp_gateway"]
```

### Docker Compose(生产)

```yaml
version: '3.8'

services:
  redis:
    image: redis:7-alpine
    command: redis-server --appendonly yes
    volumes:
      - redis_data:/data
    deploy:
      resources:
        limits:
          cpus: '1'
          memory: 1G
    restart: unless-stopped

  mysql:
    image: mysql:8.0
    environment:
      MYSQL_ROOT_PASSWORD: ${MYSQL_ROOT_PASSWORD}
      MYSQL_DATABASE: chirp
      MYSQL_USER: chirp
      MYSQL_PASSWORD: ${MYSQL_PASSWORD}
    volumes:
      - mysql_data:/var/lib/mysql
      - ./scripts/init_db.sql:/docker-entrypoint-initdb.d/init_db.sql
    deploy:
      resources:
        limits:
          cpus: '2'
          memory: 2G
    restart: unless-stopped
    healthcheck:
      test: ["CMD", "mysqladmin", "ping", "-h", "localhost"]
      interval: 10s
      timeout: 5s
      retries: 5

  gateway:
    image: chirp:latest
    ports:
      - "5000:5000"
      - "5001:5001"
    environment:
      - AUTH_HOST=auth
      - AUTH_PORT=6000
      - REDIS_HOST=redis
      - REDIS_PORT=6379
      - INSTANCE_ID=gw_1
    depends_on:
      - auth
      - redis
    deploy:
      replicas: 3
      resources:
        limits:
          cpus: '2'
          memory: 2G
    restart: unless-stopped

  auth:
    image: chirp:latest
    environment:
      - JWT_SECRET=${JWT_SECRET}
    deploy:
      replicas: 2
      resources:
        limits:
          cpus: '1'
          memory: 1G
    restart: unless-stopped

  chat:
    image: chirp:latest
    environment:
      - REDIS_HOST=redis
      - REDIS_PORT=6379
      - MYSQL_HOST=mysql
      - MYSQL_PORT=3306
      - MYSQL_DB=chirp
      - MYSQL_USER=chirp
      - MYSQL_PASSWORD=${MYSQL_PASSWORD}
    depends_on:
      - redis
      - mysql
    deploy:
      replicas: 2
      resources:
        limits:
          cpus: '2'
          memory: 2G
    restart: unless-stopped

  social:
    image: chirp:latest
    environment:
      - REDIS_HOST=redis
      - REDIS_PORT=6379
    depends_on:
      - redis
    deploy:
      replicas: 2
      resources:
        limits:
          cpus: '1'
          memory: 1G
    restart: unless-stopped

  voice:
    image: chirp:latest
    deploy:
      replicas: 1
      resources:
        limits:
          cpus: '1'
          memory: 1G
    restart: unless-stopped

volumes:
  redis_data:
  mysql_data:
```

---

## Kubernetes 部署

### 命名空间与 ConfigMap

```yaml
# namespace.yaml
apiVersion: v1
kind: Namespace
metadata:
  name: chirp

---
# configmap.yaml
apiVersion: v1
kind: ConfigMap
metadata:
  name: chirp-config
  namespace: chirp
data:
  AUTH_HOST: "auth"
  AUTH_PORT: "6000"
  REDIS_HOST: "redis"
  REDIS_PORT: "6379"
  MYSQL_HOST: "mysql"
  MYSQL_PORT: "3306"
  MYSQL_DB: "chirp"
```

### Gateway Deployment

```yaml
apiVersion: apps/v1
kind: Deployment
metadata:
  name: gateway
  namespace: chirp
spec:
  replicas: 3
  selector:
    matchLabels:
      app: gateway
  template:
    metadata:
      labels:
        app: gateway
    spec:
      containers:
      - name: gateway
        image: chirp:latest
        ports:
        - containerPort: 5000
          name: tcp
        - containerPort: 5001
          name: websocket
        env:
        - name: AUTH_HOST
          valueFrom:
            configMapKeyRef:
              name: chirp-config
              key: AUTH_HOST
        - name: REDIS_HOST
          valueFrom:
            configMapKeyRef:
              name: chirp-config
              key: REDIS_HOST
        resources:
          requests:
            memory: "512Mi"
            cpu: "500m"
          limits:
            memory: "2Gi"
            cpu: "2000m"
        livenessProbe:
          tcpSocket:
            port: 5000
          initialDelaySeconds: 30
          periodSeconds: 10
        readinessProbe:
          tcpSocket:
            port: 5000
          initialDelaySeconds: 5
          periodSeconds: 5
---
apiVersion: v1
kind: Service
metadata:
  name: gateway
  namespace: chirp
spec:
  selector:
    app: gateway
  ports:
  - port: 5000
    targetPort: 5000
    name: tcp
  - port: 5001
    targetPort: 5001
    name: websocket
  type: LoadBalancer
```

---

## 配置

### 环境变量

**Gateway:**
| 变量 | 说明 | 默认 |
|----------|-------------|---------|
| `AUTH_HOST` | Auth 服务主机 | `localhost` |
| `AUTH_PORT` | Auth 服务端口 | `6000` |
| `REDIS_HOST` | Redis 主机 | 空 |
| `REDIS_PORT` | Redis 端口 | `6379` |
| `REDIS_TTL` | 会话 TTL(秒) | `3600` |
| `INSTANCE_ID` | 实例 ID | 随机 |

**Chat:**
| 变量 | 说明 | 默认 |
|----------|-------------|---------|
| `REDIS_HOST` | Redis 主机 | 空 |
| `REDIS_PORT` | Redis 端口 | `6379` |
| `OFFLINE_TTL` | 离线消息 TTL | `604800` |
| `MYSQL_HOST` | MySQL 主机 | 空 |
| `MYSQL_PORT` | MySQL 端口 | `3306` |
| `MYSQL_DB` | 数据库名 | `chirp` |

**Auth:**
| 变量 | 说明 | 默认 |
|----------|-------------|---------|
| `JWT_SECRET` | JWT 签名密钥 | 必填 |

### 配置文件

服务可以用命令行参数或环境变量配置:

```bash
./services/gateway/chirp_gateway \
  --port 5000 \
  --ws_port 5001 \
  --auth_host auth \
  --auth_port 6000 \
  --redis_host redis \
  --redis_port 6379
```

---

## 监控

### 健康检查

下面的示例描述的是目标运维形态。当前仓库默认没有在所有服务上暴露统一的 HTTP 健康端点。

示例:

```bash
# Gateway
curl http://localhost:5000/health

# Chat
curl http://localhost:7000/health

# Social
curl http://localhost:8000/health
```

### 指标导出(Prometheus 格式)

这是目标状态的示例,并不保证当前每个服务无需额外接线就能暴露 `/metrics`。

服务在 `/metrics` 暴露指标:

```
# HELP chirp_messages_total Total messages sent
chirp_messages_total{service="chat"} 15234

# HELP chirp_connections_active Active connections
chirp_connections_total{service="gateway"} 423
```

### 日志

日志是结构化 JSON 格式:

```json
{
  "timestamp": "2024-03-01T12:00:00Z",
  "level": "INFO",
  "service": "gateway",
  "message": "User login",
  "user_id": "player1",
  "session_id": "abc123"
}
```

---

## 扩容

### 水平扩容

**Gateway:**
- 无状态设计,可无限扩容
- 前置负载均衡(HAProxy、nginx、ALB)
- 会话状态放 Redis

**Chat/Social/Voice:**
- 可各自独立扩容
- 共享状态放 Redis/MySQL
- 经 Redis 做连接亲和

### 垂直扩容

**资源配比:**
- Gateway:每 10K 连接 4 核 4GB 内存
- Chat:每 1K 并发房间 2 核 2GB 内存
- Social:每 5K 在线用户 1 核 1GB 内存
- Voice:每 100 并发房间 2 核 2GB 内存

### 容量规划

| 服务 | 并发用户 | 实例数(4C/4G) |
|---------|-----------------|---------------------|
| Gateway | 10,000 | 3 |
| Chat | 5,000 | 2 |
| Social | 20,000 | 2 |
| Voice | 1,000 | 1 |

---

## 安全

### 网络安全

1. **TLS/SSL:**
   ```nginx
   server {
       listen 443 ssl;
       ssl_certificate /path/to/cert.pem;
       ssl_certificate_key /path/to/key.pem;
   }
   ```

2. **防火墙规则:**
   ```bash
   # Allow only Gateway ports externally
   ufw allow 5000/tcp
   ufw allow 5001/tcp
   ufw deny 6000/tcp
   ufw deny 7000/tcp
   ufw deny 8000/tcp
   ```

### 密钥管理

用环境变量或 secret 管理器:

```yaml
# docker-compose.yml
environment:
  - JWT_SECRET_FILE=/run/secrets/jwt_secret
  - MYSQL_PASSWORD_FILE=/run/secrets/mysql_password
```

---

## 高可用

### Redis 部署

```bash
# Redis Sentinel for high availability
redis-server --port 6379 --sentinel
redis-server --port 6380 --sentinel
redis-server --port 6381 --sentinel
```

### MySQL 复制

```
Master (write)     Slave1 (read)    Slave2 (read)
    │                   │                │
    └───────────────────┴────────────────┘
         Asynchronous replication
```

### 服务健康检查

```yaml
livenessProbe:
  tcpSocket:
    port: 5000
  initialDelaySeconds: 30
  periodSeconds: 10
  failureThreshold: 3

readinessProbe:
  tcpSocket:
    port: 5000
  initialDelaySeconds: 5
  periodSeconds: 5
  failureThreshold: 2
```

---

## 备份与恢复

### MySQL 备份

```bash
# Daily backup
mysqldump -u chirp -p chirp > backup_$(date +%Y%m%d).sql

# Restore
mysql -u chirp -p chirp < backup_20240301.sql
```

### Redis 备份

```bash
# RDB snapshot
redis-cli BGSAVE

# AOF backup
cp appendonly.aof appendonly.aof.backup
```

---

## 故障排查

### 常见问题

**服务起不来:**
```bash
# Check logs
docker compose logs gateway

# Check port conflicts
netstat -tlnp | grep 5000
```

**连不上:**
```bash
# Verify service process and logs
docker compose logs gateway

# Check firewall
sudo ufw status
```

**内存占用高:**
```bash
# Check Redis memory
redis-cli INFO memory

# Check connections
netstat -an | grep ESTABLISHED | wc -l
```

---

## 部署检查清单

- [ ] 生成 protobuf 文件
- [ ] 构建全部服务
- [ ] 配置环境变量
- [ ] 搭建 Redis 集群
- [ ] 配置 MySQL 复制
- [ ] 前置负载均衡
- [ ] 配置 TLS 证书
- [ ] 搭好监控
- [ ] 配置备份
- [ ] 跑冒烟测试
- [ ] 配置自动扩缩容
- [ ] 配置告警
