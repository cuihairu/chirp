> 🌐 Language: [中文](./SCALABILITY_SUMMARY.md) | **English** (this page)

# Chirp Distributed Scaling Summary

> Status note: this document is a scalability design summary including capacity estimates and an evolution plan. For the current default supported paths, experimental capabilities and unfinished boundaries, defer to [Overall Architecture](../architecture.en.md) and [Capability Matrix](../CAPABILITY_MATRIX.en.md).

## Architecture scaling capability analysis

| Component | Single-server limit | Scaling approach | Scaling type |
|------|----------|----------|----------|
| **Gateway** | ~50K connections | ✅ Redis distributed sessions already supported | Horizontal |
| **Auth** | ~20K QPS | ✅ Stateless design | Horizontal |
| **Chat** | Local-state bottleneck | ⚠️ Experimental distributed implementation exists; the default path still needs to converge | Horizontal |
| **Social** | Local-state bottleneck | ⚠️ Needs refactoring (message router) | Horizontal |
| **Voice** | Local-state bottleneck | ⚠️ Needs refactoring (message router) | Horizontal |
| **Redis** | Single point of failure | Redis Cluster / master-slave | Horizontal |
| **MySQL** | Single-writer | Master-slave replication / sharding | Vertical + horizontal |

---

## Target distributed architecture

```
                        ┌─────────────────────────────────────────────────┐
                        │                   客户端层                      │
                        │   Game Clients (TCP) │ Web/Mobile (WebSocket)  │
                        └─────────────────────┬───────────────────────────┘
                                              │
                        ┌─────────────────────┴───────────────────────────┐
                        │              负载均衡层 (HAProxy/LVS)           │
                        │   TCP:5000 WebSocket:5001 │ Multi-Protocol     │
                        └─────────────────────┬───────────────────────────┘
                                              │
        ┌─────────────────────────────────────┼─────────────────────────────────────────────┐
        │                                     │                                             │
        │  ┌──────────────────────────────────▼──────────────────────────────────────────┐ │
        │  │                        Gateway 层 (无状态)                                  │ │
        │  │  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐                   │ │
        │  │  │ GW Node1 │  │ GW Node2 │  │ GW Node3 │  │ GW NodeN │ ... 可水平扩展     │ │
        │  │  │ :5000/01 │  │ :5000/01 │  │ :5000/01 │  │ :5000/01 │                   │ │
        │  │  └────┬─────┘  └────┬─────┘  └────┬─────┘  └────┬─────┘                   │ │
        │  └───────┼────────────┼────────────┼────────────┼────────────────────────────┘ │
        │          │            │            │            │                                │
        │  ┌───────┴────────────┴────────────┴────────────┴────────────────────────────┐ │
        │  │                      服务发现 (Redis)                                    │ │
        │  │           service:gateway:gw_1 -> 192.168.1.10:5000                      │ │
        │  └──────────────────────────────────────────────────────────────────────────┘ │
        └────────────────────────────────────────────────────────────────────────────────┘
                                             │
        ┌─────────────────────────────────────┼─────────────────────────────────────────────┐
        │                                     │                                             │
        │  ┌──────────────────────────────────▼──────────────────────────────────────────┐ │
        │  │                    业务服务层 (Chat/Social/Voice)                           │ │
        │  │  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐                   │ │
        │  │  │Chat Node1│  │Chat Node2│  │Chat Node3│  │Chat NodeN│ ... 水平扩展        │ │
        │  │  │ :7000/01 │  │ :7000/01 │  │ :7000/01 │  │ :7000/01 │                   │ │
        │  │  └────┬─────┘  └────┬─────┘  └────┬─────┘  └────┬─────┘                   │ │
        │  │       │            │            │            │                              │ │
        │  │  ┌────▼────────────▼────────────▼────────────▼────┐                         │ │
        │  │  │        MessageRouter (Redis Pub/Sub)            │                         │ │
        │  │  │  chirp:chat:user:{id}  chirp:social:user:{id}  │                         │ │
        │  │  │  chirp:chat:group:{id} chirp:kick:instance{}   │                         │ │
        │  │  └──────────────────────┬───────────────────────────┘                         │ │
        │  └─────────────────────────┼──────────────────────────────────────────────────┘ │
        │                            │                                                        │
        │  ┌─────────────────────────▼──────────────────────────────────────────────────┐ │
        │  │                        分布式状态管理                                       │ │
        │  │  ┌────────────────────────────────────────────────────────────────────────┐ │ │
        │  │  │  本地会话: local_sessions[user_id] = weak_ptr<Session>                 │ │ │
        │  │  │  消息路由: router->SendChatMessage(user_id, msg, local_send_fn)        │ │ │
        │  │  │            → 先尝试本地投递，失败则通过 Redis 转发                      │ │ │
        │  │  └────────────────────────────────────────────────────────────────────────┘ │ │
        │  └─────────────────────────────────────────────────────────────────────────────┘ │
        └────────────────────────────────────────────────────────────────────────────────┘
                                             │
        ┌─────────────────────────────────────┼─────────────────────────────────────────────┐
        │                                     │                                             │
        │  ┌──────────────────────────────────▼──────────────────────────────────────────┐ │
        │  │                           数据层                                            │ │
        │  │                                                                              │ │
        │  │  ┌─────────────────────┐    ┌─────────────────────────────────────────────┐ │ │
        │  │  │   Redis Cluster      │    │   MySQL 主从复制                             │ │ │
        │  │  │  ┌───┐  ┌───┐  ┌───┐ │    │  ┌─────────┐  ┌─────────────┐              │ │ │
        │  │  │  M │◄─┤ S │◄─┤ S │ │    │  │ Primary │──│ Replica 1   │              │ │ │
        │  │  │   │  └───┘  └───┘ │    │  │ :3306   │  │ :3306       │              │ │ │
        │  │  │   │   (16379)      │    │  └─────────┘  └─────────────┘              │ │ │
        │  │  │   │                │    │              ┌─────────────┐              │ │ │
        │  │  │   │               ...│    │              │ Replica 2   │              │ │ │
        │  │  │   │   (6379)       │    │              └─────────────┘              │ │ │
        │  │  │   │                │    │                                         │ │ │
        │  │  │   │   ┌───┐  ┌───┐ │    │  功能: 持久化存储、历史消息、离线数据    │ │ │
        │  │  │   └───┤ S │◄─┤ S │ │    │                                         │ │ │
        │  │  │       └───┘  └───┘ │    │                                         │ │ │
        │  │  └─────────────────────┘    └─────────────────────────────────────────────┘ │ │
        │  │                                                                            │ │
        │  │  Redis 用途: 会话管理、Pub/Sub、在线状态、离线消息缓存                      │ │
        │  └────────────────────────────────────────────────────────────────────────────┘ │
        └────────────────────────────────────────────────────────────────────────────────┘
```

---

## Key design decisions

### 1. Gateway layer - stateless
- [Session state shared through Redis]
- [Each instance has a unique `instance_id`]
- [Cross-instance kicks via Redis Pub/Sub]
- [Horizontally scalable to N instances]

### 2. Business service layer - local state + message routing
- Maintains the `user_id -> Session` mapping locally (hot data)
- Adds the `MessageRouter` component
- Message send logic:
  1. Check whether the receiver is local → deliver directly
  2. Otherwise → forward to other instances via Redis Pub/Sub
- On login, a receiver subscribes to their own Redis channel

### 3. Load balancing strategy
- **Gateway**: use the `least_conn` algorithm (suitable for long connections)
- **Session Affinity**: keep the same client connected to the same instance (fewer Redis lookups)
- **Health Check**: actively probe instance health

### 4. Data consistency
- Redis: eventual consistency via Pub/Sub
- MySQL: write to the primary, read from replicas
- Offline messages: Redis cache + MySQL persistence

---

## Scale-out steps

### Adding a Gateway instance
```bash
# 方式1：Docker Compose 扩展
docker compose -f docker-compose.cluster.yml up -d --scale gateway=5

# 方式2：Kubernetes HPA（自动）
kubectl autoscale deployment chirp-gateway --min=3 --max=20 --cpu-percent=70

# 方式3：手动添加实例
./deploy-cluster.sh scale gateway 5
```

### Adding a Chat instance
```bash
# Docker Compose
docker compose -f docker-compose.cluster.yml up -d --scale chat=5

# Kubernetes
kubectl scale deployment chirp-chat --replicas=5
```

---

## Capacity planning reference

| Online users | Gateway instances | Chat instances | Redis nodes | MySQL nodes | Est. cost/month |
|----------|--------------|-----------|------------|------------|-------------|
| 1K online | 1 | 1 | 1 | 1 | $50 |
| 10K | 2-3 | 2-3 | 3 (1 primary, 2 replicas) | 1 (primary) + 1 (replica) | $200-500 |
| 100K | 5-10 | 5-10 | 3-6 | 1 (primary) + 2 (replicas) | $1000-2000 |
| 1M | 20-50 | 20-50 | 6-12 | 1 (primary) + 3 (replicas) + sharding | $5000-10000 |

---

## Next work

### Immediately doable
1. [x] Add the `MessageRouter` component - done (verified 2026-10-08: `libs/network/message_router.{h,cc}`)
2. [x] Refactor the Chat service for distribution - done (verified 2026-10-08: `services/shared/chat/src/main_distributed.cc`, CMake target `chirp_chat_distributed`)
3. [x] Create the HAProxy load balancing configuration - done (verified 2026-10-08: `deploy/haproxy.cfg`)
4. [x] Create the Kubernetes deployment files - done (verified 2026-10-08: `deploy/k8s/`)

### Short-term optimization
1. [Refactor the Social/Voice services]
2. [Add service health checks and automatic failover]
3. [Implement connection migration (keep connections across instance restarts)]
4. [Add monitoring metrics (Prometheus)]

### Long-term planning
1. [Redis Cluster support (automatic sharding)]
2. [MySQL sharding plan]
3. [Multi-datacenter deployment (cross-region disaster recovery)]
4. [Service mesh (Istio/Linkerd)]
5. [Automated scale-up/down policies]

---

## File inventory

| File | Description |
|------|------|
| `docs/design-notes/SCALABILITY.md` | Scalability design document |
| `docs/design-notes/SCALABILITY_CHAT_IMPLEMENTATION.md` | Chat service refactoring implementation |
| `libs/network/message_router.h/cc` | Message router component |
| `deploy/haproxy.cfg` | HAProxy load balancing configuration |
| `deploy/k8s/gateway-deployment.yaml` | Gateway K8s deployment |
| `deploy/k8s/chat-deployment.yaml` | Chat K8s deployment |
| `deploy/k8s/redis-cluster.yaml` | Redis cluster deployment |
| `deploy/deploy-cluster.sh` | Cluster deployment script |
