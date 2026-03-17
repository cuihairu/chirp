# Chirp 分布式扩展方案

## 当前架构分析

### 单机架构问题

```
┌─────────────────────────────────────────────────────┐
│              单台服务器 (瓶颈)                        │
│  ┌─────────┐  ┌──────────┐  ┌──────────┐          │
│  │Gateway  │  │  Auth    │  │  Chat    │          │
│  │ (5000)  │  │  (6000)  │  │  (7000)  │          │
│  └─────────┘  └──────────┘  └──────────┘          │
│       │             │              │               │
│  ┌────┴────┐  ┌────┴────┐  ┌──────┴──────┐        │
│  │ Local   │  │  无状态  │  │ Local State │        │
│  │ Session │  │  可扩展  │  │ (瓶颈!)     │        │
│  └─────────┘  └─────────┘  └─────────────┘        │
└─────────────────────────────────────────────────────┘
```

**瓶颈：**
1. Gateway 连接数受限于单机端口和文件描述符
2. Chat/Social 服务的内存状态无法共享
3. 单点故障风险

---

## 分布式架构设计

### 整体架构

```
                                    ┌─────────────────┐
                                    │   客户端层       │
                                    │ Game/Web/Mobile │
                                    └────────┬────────┘
                                             │
                        ┌────────────────────┼────────────────────┐
                        │                    │                    │
                ┌───────▼──────┐    ┌───────▼──────┐    ┌───────▼──────┐
                │   LB/Proxy   │    │   LB/Proxy   │    │   LB/Proxy   │
                │  (HAProxy/   │    │  (HAProxy/   │    │  (HAProxy/   │
                │   Nginx)     │    │   Nginx)     │    │   Nginx)     │
                └───────┬──────┘    └───────┬──────┘    └───────┬──────┘
                        │                    │                    │
┌───────────────────────┼────────────────────┼────────────────────┼──────────────────┐
│                   ┌───▼────┐          ┌───▼────┐          ┌───▼────┐              │
│   Gateway 层       │Gateway │          │Gateway │          │Gateway │   水平扩展    │
│   (无状态)          │ GW-1   │   ...    │ GW-2   │   ...    │ GW-N   │   ✓ 已支持    │
│   TCP:5000         │ 5000   │          │ 5000   │          │ 5000   │              │
│   WebSocket:5001   └───┬────┘          └───┬────┘          └───┬────┘              │
│                        │                    │                    │                  │
├───────────────────────┼────────────────────┼────────────────────┼──────────────────┤
│                       │                    │                    │                  │
│   Service 层       ┌───▼────┐          ┌───▼────┐          ┌───▼────┐              │
│   (需改造)          │ Auth   │          │ Auth   │          │ Auth   │   无状态     │
│                    │ AUTH-1 │   ...    │ AUTH-2 │   ...    │ AUTH-N │   ✓ 已支持    │
│                    └────────┘          └────────┘          └────────┘              │
│                                                                                   │
│                    ┌───▼────┐          ┌───▼────┐          ┌───▼────┐              │
│                    │ Chat   │          │ Chat   │          │ Chat   │   有状态     │
│                    │CHAT-1  │   ...    │CHAT-2  │   ...    │CHAT-N  │   ⚠️ 需改造  │
│                    └───┬────┘          └───┬────┘          └───┬────┘              │
│                        │                    │                    │                  │
│                    ┌───▼────┐          ┌───▼────┐          ┌───▼────┐              │
│                    │ Social │          │ Social │          │ Social │   有状态     │
│                    │SOC-1   │   ...    │SOC-2   │   ...    │SOC-N   │   ⚠️ 需改造  │
│                    └───┬────┘          └───┬────┘          └───┬────┘              │
│                        │                    │                    │                  │
│                    ┌───▼────┐          ┌───▼────┐          ┌───▼────┐              │
│                    │ Voice  │          │ Voice  │          │ Voice  │   有状态     │
│                    │VOICE-1 │   ...    │VOICE-2 │   ...    │VOICE-N │   ⚠️ 需改造  │
│                    └────────┘          └────────┘          └────────┘              │
└───────────────────────┼────────────────────┼────────────────────┼──────────────────┘
                        │                    │                    │
┌───────────────────────┼────────────────────┼────────────────────┼──────────────────┐
│   Data 层         ┌───▼────┐          ┌───▼────┐          ┌───▼────┐              │
│                    │Redis   │          │Redis   │          │Redis   │   Cluster    │
│                    │ Master │  ◄──►   │ Slave  │  ◄──►   │ Slave  │   主从复制    │
│                    └───┬────┘          └───┬────┘          └───┬────┘              │
│                        │                    │                    │                  │
│                    ┌───▼────┐          ┌───▼────┐          ┌───▼────┐              │
│                    │ MySQL  │          │ MySQL  │          │ MySQL  │   主从复制    │
│                    │ Master │  ◄──►   │ Slave  │  ◄──►   │ Slave  │              │
│                    └────────┘          └────────┘          └────────┘              │
└──────────────────────────────────────────────────────────────────────────────────┘
```

---

## 扩展性改造方案

### 1. Gateway 层 (✅ 已支持扩展)

**当前状态：** 已实现 Redis 分布式会话管理

```cpp
// gateway/src/redis_session_manager.cc
// - 每个实例有唯一 instance_id
// - Redis 存储 user_id -> instance_id 映射
// - Pub/Sub 机制实现跨实例踢人
// - 新实例登录时，通知旧实例踢人
```

**部署方式：**
```bash
# 多实例部署
docker compose up --scale gateway=3
# 或手动指定实例ID
./chirp_gateway --instance_id gw_1 --port 5000 &
./chirp_gateway --instance_id gw_2 --port 5001 &
./chirp_gateway --instance_id gw_3 --port 5002 &
```

**负载均衡：**
```nginx
# nginx.conf
upstream gateway_tcp {
    least_conn;
    server gateway-1:5000;
    server gateway-2:5000;
    server gateway-3:5000;
}

upstream gateway_ws {
    least_conn;
    server gateway-1:5001;
    server gateway-2:5001;
    server gateway-3:5001;
}
```

---

### 2. Chat/Social/Voice 服务改造 (⚠️ 需要改造)

**问题：** 当前使用本地内存存储用户会话映射，多实例时无法互通

**解决方案：** 添加 Redis Pub/Sub 消息转发机制

#### 2.1 消息路由设计

```
┌────────────────────────────────────────────────────────────────┐
│                    Redis Pub/Sub 通道设计                        │
├────────────────────────────────────────────────────────────────┤
│  Channel                    │ Payload                          │
├─────────────────────────────┼──────────────────────────────────┤
│  chirp:chat:user:{user_id} │ ChatMessage (私聊)                │
│  chirp:chat:group:{group_id}│ ChatMessage (群聊)                │
│  chirp:social:user:{user_id}│ PresenceNotify, FriendNotify      │
│  chirp:kick:instance:{id}   │ user_id (踢人通知)                │
└─────────────────────────────┴──────────────────────────────────┘
```

#### 2.2 改造架构

```cpp
// 消息路由器接口
class MessageRouter {
public:
    // 发布消息到 Redis
    virtual void Publish(const std::string& user_id,
                        const std::string& message) = 0;

    // 订阅用户消息
    virtual void Subscribe(const std::string& user_id,
                          std::function<void(const std::string&)> cb) = 0;
};

// Chat 服务改造
class ChatService {
    std::shared_ptr<MessageRouter> router_;
    std::unordered_map<std::string, std::weak_ptr<Session>> local_sessions_;

    void OnMessage(const Message& msg) {
        // 1. 检查接收者是否在本地
        auto local_session = local_sessions_[msg.receiver_id].lock();
        if (local_session) {
            // 本地投递
            SendToSession(local_session, msg);
            return;
        }

        // 2. 通过 Redis Pub/Sub 转发到其他实例
        router_->Publish("chirp:chat:user:" + msg.receiver_id, msg.Serialize());
    }

    void OnRedisMessage(const std::string& user_id, const std::string& msg) {
        // 收到来自其他实例的消息
        auto session = local_sessions_[user_id].lock();
        if (session) {
            SendToSession(session, msg);
        }
    }
};
```

#### 2.3 连接迁移策略

**选项 A：Gateway 保持长连接，服务层无状态化**

```
Client --TCP--> Gateway --(short-lived HTTP/gRPC)--> Service
                 │                                  │
                 └── WebSocket 长连接 ───────────────┘
```

**选项 B：Gateway 直连后端服务，使用一致性哈希**

```cpp
// 一致性哈希路由
class ConsistentHashRouter {
    std::map<size_t, std::string> ring_;

    std::string GetInstance(const std::string& user_id) {
        size_t hash = std::hash<std::string>{}(user_id);
        auto it = ring_.lower_bound(hash);
        if (it == ring_.end()) it = ring_.begin();
        return it->second;
    }
};
```

**推荐：选项 B** - 保持长连接优势，减少连接建立开销

---

### 3. 服务发现

#### 3.1 基于 Redis 的服务发现

```cpp
class ServiceDiscovery {
public:
    // 服务注册
    void Register(const std::string& service_name,
                  const std::string& instance_id,
                  const std::string& host,
                  uint16_t port) {
        std::string key = "chirp:service:" + service_name + ":" + instance_id;
        std::string value = host + ":" + std::to_string(port);
        redis_.SetEx(key, value, 30);  // 30秒 TTL

        // 定期续约
        RenewTimer(service_name, instance_id, host, port);
    }

    // 服务发现
    std::vector<std::string> Discover(const std::string& service_name) {
        std::string pattern = "chirp:service:" + service_name + ":*";
        auto keys = redis_.Keys(pattern);
        std::vector<std::string> instances;
        for (const auto& key : keys) {
            instances.push_back(redis_.Get(key).value());
        }
        return instances;
    }
};
```

---

### 4. 数据层扩展

#### 4.1 Redis Cluster

```bash
# docker-compose.yml
redis-cluster:
  image: redis:7-alpine
  command: redis-cli --cluster create \
    redis-node-1:7000 \
    redis-node-2:7001 \
    redis-node-3:7002 \
    redis-node-4:7003 \
    redis-node-5:7004 \
    redis-node-6:7005 \
    --cluster-replicas 1
```

#### 4.2 MySQL 主从复制

```yaml
# docker-compose.yml
mysql-master:
  image: mysql:8.0
  environment:
    MYSQL_REPLICATION_MODE: master
    MYSQL_REPLICATION_USER: repl
    MYSQL_REPLICATION_PASSWORD: repl_pass

mysql-slave:
  image: mysql:8.0
  environment:
    MYSQL_REPLICATION_MODE: slave
    MYSQL_REPLICATION_HOST: mysql-master
    MYSQL_REPLICATION_USER: repl
    MYSQL_REPLICATION_PASSWORD: repl_pass
```

---

## 部署配置

### Docker Compose (多实例)

```yaml
# docker-compose.cluster.yml
services:
  # 负载均衡
  haproxy:
    image: haproxy:2.8
    ports:
      - "5000:5000"   # Gateway TCP
      - "5001:5001"   # Gateway WebSocket
      - "6000:6000"   # Auth
    volumes:
      - ./haproxy.cfg:/usr/local/etc/haproxy/haproxy.cfg

  # Gateway 多实例
  gateway:
    image: chirp-gateway
    deploy:
      replicas: 3
    environment:
      - INSTANCE_ID=gateway_${HOSTNAME}
      - REDIS_HOST=redis-cluster

  # Chat 多实例
  chat:
    image: chirp-chat
    deploy:
      replicas: 3
    environment:
      - INSTANCE_ID=chat_${HOSTNAME}
      - REDIS_HOST=redis-cluster

  # Redis Cluster
  redis-cluster:
    image: redis:7-alpine
    command: redis-server --cluster-enabled yes

  # MySQL 主从
  mysql-master:
    image: mysql:8.0
  mysql-slave:
    image: mysql:8.0
```

### Kubernetes 部署

```yaml
# k8s/gateway-deployment.yaml
apiVersion: apps/v1
kind: Deployment
metadata:
  name: chirp-gateway
spec:
  replicas: 3
  selector:
    matchLabels:
      app: chirp-gateway
  template:
    metadata:
      labels:
        app: chirp-gateway
    spec:
      containers:
      - name: gateway
        image: chirp/gateway:latest
        ports:
        - containerPort: 5000
        - containerPort: 5001
        env:
        - name: INSTANCE_ID
          valueFrom:
            fieldRef:
              fieldPath: metadata.name
        - name: REDIS_HOST
          value: "redis-cluster"
---
apiVersion: v1
kind: Service
metadata:
  name: chirp-gateway
spec:
  selector:
    app: chirp-gateway
  ports:
  - name: tcp
    port: 5000
    targetPort: 5000
  - name: ws
    port: 5001
    targetPort: 5001
  type: LoadBalancer
```

---

## 容量规划

### 单机性能参考

| 组件 | 连接数 | QPS | 内存 |
|------|--------|-----|------|
| Gateway | 10K-50K | 100K | 2GB |
| Chat | 5K-20K | 50K | 4GB |
| Auth | 10K | 20K | 1GB |
| Social | 5K | 10K | 2GB |
| Redis | - | 100K | 4GB |

### 规模建议

| 在线用户 | Gateway | Chat | Redis | MySQL |
|----------|---------|------|-------|-------|
| 1K | 1 | 1 | 1 | 1 |
| 10K | 2 | 2 | 1 (主) + 1 (从) | 1 (主) |
| 100K | 5-10 | 5-10 | 3 (Cluster) | 1 (主) + 2 (从) |
| 1M | 20-50 | 20-50 | 6 (Cluster) | 1 (主) + 3 (从) + 分库分表 |

---

## 下一步工作

1. **立即实施**
   - [ ] 添加 MessageRouter 组件
   - [ ] Chat 服务改造支持 Redis Pub/Sub
   - [ ] 添加服务发现模块

2. **短期优化**
   - [ ] Social/Voice 服务改造
   - [ ] HAProxy 负载均衡配置
   - [ ] 健康检查和自动故障转移

3. **长期规划**
   - [ ] Redis Cluster 支持
   - [ ] MySQL 分库分表
   - [ ] Kubernetes 部署方案
   - [ ] 监控和告警系统
