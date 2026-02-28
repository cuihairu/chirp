# Chirp Deployment Guide

## Table of Contents

- [Deployment Overview](#deployment-overview)
- [Development Environment](#development-environment)
- [Production Deployment](#production-deployment)
- [Docker Deployment](#docker-deployment)
- [Configuration](#configuration)
- [Monitoring](#monitoring)
- [Scaling](#scaling)

---

## Deployment Overview

Chirp services can be deployed in several ways:

1. **Docker Compose** (recommended for development/testing)
2. **Kubernetes** (recommended for production)
3. **Manual deployment** (traditional servers)

### Service Dependencies

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

## Development Environment

### Quick Start with Docker Compose

1. **Clone repository:**
   ```bash
   git clone <repository-url>
   cd chirp
   ```

2. **Start services:**
   ```bash
   docker compose up -d
   ```

3. **View logs:**
   ```bash
   docker compose logs -f
   docker compose logs -f gateway
   ```

4. **Stop services:**
   ```bash
   docker compose down
   ```

### Build from Source

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

## Production Deployment

### Requirements

**Hardware (per service instance):**
- CPU: 2 cores minimum, 4 cores recommended
- Memory: 2GB minimum, 4GB recommended
- Network: 1 Gbps recommended

**Software:**
- OS: Linux (Ubuntu 20.04+, CentOS 8+)
- Docker: 20.10+
- Docker Compose: 2.0+ (for local testing)

### Port Requirements

| Service | Port | Protocol | Notes |
|---------|------|----------|-------|
| Gateway | 5000, 5001 | TCP, WS | External |
| Auth | 6000 | TCP | Internal |
| Chat | 7000, 7001 | TCP, WS | Internal |
| Social | 8000, 8001 | TCP, WS | Internal |
| Voice | 9000, 9001 | TCP, WS | Internal |
| Redis | 6379 | TCP | Internal |
| MySQL | 3306 | TCP | Internal |

---

## Docker Deployment

### Production Dockerfile

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

### Docker Compose (Production)

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

## Kubernetes Deployment

### Namespace and ConfigMap

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

## Configuration

### Environment Variables

**Gateway:**
| Variable | Description | Default |
|----------|-------------|---------|
| `AUTH_HOST` | Auth service host | `localhost` |
| `AUTH_PORT` | Auth service port | `6000` |
| `REDIS_HOST` | Redis host | empty |
| `REDIS_PORT` | Redis port | `6379` |
| `REDIS_TTL` | Session TTL (sec) | `3600` |
| `INSTANCE_ID` | Instance ID | random |

**Chat:**
| Variable | Description | Default |
|----------|-------------|---------|
| `REDIS_HOST` | Redis host | empty |
| `REDIS_PORT` | Redis port | `6379` |
| `OFFLINE_TTL` | Offline message TTL | `604800` |
| `MYSQL_HOST` | MySQL host | empty |
| `MYSQL_PORT` | MySQL port | `3306` |
| `MYSQL_DB` | Database name | `chirp` |

**Auth:**
| Variable | Description | Default |
|----------|-------------|---------|
| `JWT_SECRET` | JWT signing secret | required |

### Configuration Files

Services can be configured via command-line arguments or environment variables:

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

## Monitoring

### Health Checks

All services expose health check endpoints:

```bash
# Gateway
curl http://localhost:5000/health

# Chat
curl http://localhost:7000/health

# Social
curl http://localhost:8000/health
```

### Metrics Export (Prometheus format)

Services expose metrics at `/metrics`:

```
# HELP chirp_messages_total Total messages sent
chirp_messages_total{service="chat"} 15234

# HELP chirp_connections_active Active connections
chirp_connections_total{service="gateway"} 423
```

### Logging

Logs are structured JSON format:

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

## Scaling

### Horizontal Scaling

**Gateway:**
- Stateless design allows unlimited scaling
- Use load balancer (HAProxy, nginx, ALB)
- Session state in Redis

**Chat/Social/Voice:**
- Can scale independently
- Shared state in Redis/MySQL
- Connection affinity via Redis

### Vertical Scaling

**Resource allocation:**
- Gateway: 4 cores, 4GB RAM per 10K connections
- Chat: 2 cores, 2GB RAM per 1K concurrent rooms
- Social: 1 core, 1GB RAM per 5K online users
- Voice: 2 cores, 2GB RAM per 100 concurrent rooms

### Capacity Planning

| Service | Concurrent Users | Instances (4C/4G) |
|---------|-----------------|---------------------|
| Gateway | 10,000 | 3 |
| Chat | 5,000 | 2 |
| Social | 20,000 | 2 |
| Voice | 1,000 | 1 |

---

## Security

### Network Security

1. **TLS/SSL:**
   ```nginx
   server {
       listen 443 ssl;
       ssl_certificate /path/to/cert.pem;
       ssl_certificate_key /path/to/key.pem;
   }
   ```

2. **Firewall rules:**
   ```bash
   # Allow only Gateway ports externally
   ufw allow 5000/tcp
   ufw allow 5001/tcp
   ufw deny 6000/tcp
   ufw deny 7000/tcp
   ufw deny 8000/tcp
   ```

### Secrets Management

Use environment variables or secret managers:

```yaml
# docker-compose.yml
environment:
  - JWT_SECRET_FILE=/run/secrets/jwt_secret
  - MYSQL_PASSWORD_FILE=/run/secrets/mysql_password
```

---

## High Availability

### Redis Setup

```bash
# Redis Sentinel for high availability
redis-server --port 6379 --sentinel
redis-server --port 6380 --sentinel
redis-server --port 6381 --sentinel
```

### MySQL Replication

```
Master (write)     Slave1 (read)    Slave2 (read)
    │                   │                │
    └───────────────────┴────────────────┘
         Asynchronous replication
```

### Service Health Checks

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

## Backup and Recovery

### MySQL Backup

```bash
# Daily backup
mysqldump -u chirp -p chirp > backup_$(date +%Y%m%d).sql

# Restore
mysql -u chirp -p chirp < backup_20240301.sql
```

### Redis Backup

```bash
# RDB snapshot
redis-cli BGSAVE

# AOF backup
cp appendonly.aof appendonly.aof.backup
```

---

## Troubleshooting

### Common Issues

**Service won't start:**
```bash
# Check logs
docker compose logs gateway

# Check port conflicts
netstat -tlnp | grep 5000
```

**Can't connect:**
```bash
# Verify service is running
curl http://localhost:5000/health

# Check firewall
sudo ufw status
```

**High memory usage:**
```bash
# Check Redis memory
redis-cli INFO memory

# Check connections
netstat -an | grep ESTABLISHED | wc -l
```

---

## Deployment Checklist

- [ ] Generate protobuf files
- [ ] Build all services
- [ ] Configure environment variables
- [ ] Set up Redis cluster
- [ ] Configure MySQL replication
- [ ] Set up load balancer
- [ ] Configure TLS certificates
- [ ] Set up monitoring
- [ ] Configure backups
- [ ] Run smoke tests
- [ ] Configure autoscaling
- [ ] Set up alerting
