# Chirp 分布式扩展 - 完整指南

## 概述

本文档描述如何将 Chirp 从单机部署扩展到分布式集群部署，支持水平扩展和高可用性。

---

## 架构变更

### 1. 新增组件

| 组件 | 文件 | 说明 |
|------|------|------|
| **MessageRouter** | `libs/network/message_router.h/cc` | Redis Pub/Sub 消息路由器，实现跨实例消息转发 |
| **DistributedChatService** | `services/chat/src/main_distributed.cc` | 分布式版本的 Chat 服务 |

### 2. 架构对比

**单机架构：**
```
Client → Gateway → Chat (本地状态) → Redis/MySQL
                  ↑
             所有连接在同一实例
```

**分布式架构：**
```
Client → LB → Gateway-1 ──┐
         │   Gateway-2 ───┼→ Redis Pub/Sub ─→ Chat-1/2/3
         │   Gateway-N ──┘                         ↓
                                                 Redis/MySQL
```

---

## 核心设计

### MessageRouter 消息路由器

```cpp
// 智能消息路由
router->SendChatMessage(user_id, message,
    [](const std::string& uid) -> bool {
        // 1. 优先尝试本地投递
        if (auto session = local_sessions.Get(uid)) {
            SendToSession(session, message);
            return true;  // 本地投递成功
        }
        return false;  // 触发 Redis Pub/Sub 转发
    });
```

**工作流程：**
1. 发送消息时，先检查接收者是否在本地实例
2. 如果本地在线，直接投递（避免 Redis 开销）
3. 如果本地离线，通过 Redis Pub/Sub 转发到其他实例
4. 接收者实例订阅自己的频道，收到消息后投递

### Redis 频道设计

| 频道模式 | 用途 |
|----------|------|
| `chirp:chat:user:{user_id}` | 用户私聊消息 |
| `chirp:chat:group:{group_id}` | 群组消息 |
| `chirp:social:user:{user_id}` | 社交通知 |
| `chirp:kick:instance:{id}` | 跨实例踢人通知 |

---

## 构建和部署

### 方式一：Docker Compose 集群部署

```bash
# 1. 构建项目
./scripts/build-cluster.sh build

# 2. 构建 Docker 镜像
./scripts/build-cluster.sh docker

# 3. 启动集群
./scripts/build-cluster.sh start

# 4. 查看状态
docker compose -f docker-compose.cluster.yml ps

# 5. 查看日志
docker compose -f docker-compose.cluster.yml logs -f chat-1

# 6. 扩展服务
docker compose -f docker-compose.cluster.yml up -d --scale chat-1=5
```

### 方式二：手动部署

```bash
# 构建分布式 Chat 服务
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --target chirp_chat_distributed

# 启动多个实例
./services/chat/chirp_chat_distributed \
    --port 7000 \
    --ws_port 7001 \
    --redis_host 192.168.1.10 \
    --redis_port 6379 \
    --instance_id chat_1 &

./services/chat/chirp_chat_distributed \
    --port 7000 \
    --ws_port 7001 \
    --redis_host 192.168.1.10 \
    --redis_port 6379 \
    --instance_id chat_2 &
```

### 方式三：Kubernetes 部署

```bash
# 部署到 Kubernetes
kubectl apply -f deploy/k8s/redis-cluster.yaml
kubectl apply -f deploy/k8s/gateway-deployment.yaml
kubectl apply -f deploy/k8s/chat-deployment.yaml

# 查看状态
kubectl get pods -n chirp

# 扩展
kubectl scale deployment chirp-chat -n chirp --replicas=5
```

---

## 配置说明

### Chat 服务参数

| 参数 | 说明 | 默认值 |
|------|------|--------|
| `--port` | TCP 端口 | 7000 |
| `--ws_port` | WebSocket 端口 | 7001 |
| `--redis_host` | Redis 主机 | 127.0.0.1 |
| `--redis_port` | Redis 端口 | 6379 |
| `--offline_ttl` | 离线消息TTL(秒) | 604800 (7天) |
| `--instance_id` | 实例ID | 随机生成 |

### 环境变量

```bash
# Redis 配置
export CHIRP_REDIS_HOST=redis-cluster
export CHIRP_REDIS_PORT=6379

# 实例配置
export CHIRP_INSTANCE_ID=chat_$(hostname)

# 日志级别
export CHIRP_LOG_LEVEL=info
```

---

## 容量规划

### 单实例性能参考

| 指标 | Gateway | Chat | Auth |
|------|---------|------|------|
| 最大连接数 | 50K | 20K | 10K |
| 消息QPS | 100K | 50K | 20K |
| 内存使用 | 1GB | 2GB | 512MB |
| CPU使用 | 2核 | 2核 | 1核 |

### 集群规模建议

| 在线用户 | Gateway实例 | Chat实例 | Redis | MySQL |
|----------|-------------|----------|-------|-------|
| 1K | 1 | 1 | 1 | 1 |
| 10K | 2-3 | 2-3 | 1主2从 | 1主1从 |
| 100K | 5-10 | 5-10 | Cluster (3主3从) | 1主2从 |
| 1M | 20-50 | 20-50 | Cluster (6+节点) | 分库分表 |

---

## 测试验证

### 基本连接测试

```bash
# 连接到 HAProxy
nc localhost 5000

# 发送登录请求 (Protobuf)
# 这里需要使用客户端工具或测试脚本
```

### 使用测试客户端

```bash
# 编译测试客户端
cd tools/benchmark
make

# 测试多个用户连接
for i in {1..100}; do
    ./chat_send_client --host localhost --port 7000 --user user_$i &
done
```

### 分布式场景测试

1. **多实例消息路由测试**
   ```bash
   # 启动两个 Chat 实例
   ./chirp_chat_distributed --instance_id chat_1 --port 7000 &
   ./chirp_chat_distributed --instance_id chat_2 --port 7001 &

   # 连接到 chat_1，发送消息给连接在 chat_2 的用户
   # 验证消息通过 Redis 正确路由
   ```

2. **故障转移测试**
   ```bash
   # 杀掉一个 Chat 实例
   kill -9 <chat_1_pid>

   # 验证消息仍可路由到 chat_2
   ```

---

## 故障排查

### 常见问题

**1. 消息未送达**
```bash
# 检查 Redis 连接
docker compose -f docker-compose.cluster.yml exec redis redis-cli ping

# 检查订阅状态
docker compose -f docker-compose.cluster.yml exec redis redis-cli PUBSUB CHANNELS
```

**2. 离线消息丢失**
```bash
# 检查离线消息队列
docker compose -f docker-compose.cluster.yml exec redis redis-cli LLEN chirp:chat:offline:user_1
```

**3. 性能问题**
```bash
# 查看 Redis 慢查询
docker compose -f docker-compose.cluster.yml exec redis redis-cli SLOWLOG GET 10
```

---

## 监控指标

### 关键指标

| 指标 | 说明 | 告警阈值 |
|------|------|----------|
| 连接数 | 当前活跃连接数 | > 容量的 80% |
| 消息QPS | 每秒消息数 | > 容量的 80% |
| 消息延迟 | 消息端到端延迟 | > 100ms |
| Redis 连接 | Redis 连接池状态 | 耗尽 |
| CPU 使用率 | 实例 CPU 使用率 | > 80% |
| 内存使用率 | 实例内存使用率 | > 80% |

### 日志

```bash
# 查看服务日志
docker compose -f docker-compose.cluster.yml logs -f chat-1

# 查看所有服务日志
docker compose -f docker-compose.cluster.yml logs -f
```

---

## 下一步

1. **性能优化**
   - 添加消息批处理
   - 实现连接池复用
   - 启用 Redis Pipeline

2. **高可用**
   - Redis Sentinel 自动故障转移
   - MySQL 主从自动切换
   - 服务健康检查和自动重启

3. **监控**
   - Prometheus 指标导出
   - Grafana 可视化面板
   - 告警规则配置

4. **安全**
   - TLS/SSL 加密连接
   - 认证和授权
   - 防火墙和网络隔离
