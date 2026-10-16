> 🌐 Language: [中文](./DISTRIBUTED_DEPLOYMENT.md) | **English** (this page)

# Chirp distributed extension - complete guide

> Status note: this document describes the distributed extension plan and deployment examples. For the currently supported default path and the experimental boundary, use [Overall Architecture](../architecture.en.md) and the [Capability Matrix](../CAPABILITY_MATRIX.en.md); do not read the capacity numbers or cluster examples directly as load-tested production commitments.

## Overview

This document describes how to scale Chirp from a single-machine deployment to a distributed cluster deployment, with horizontal scaling and high availability.

---

## Architecture changes

### 1. New components

| Component | File | Description |
|------|------|------|
| **MessageRouter** | `libs/network/message_router.h/cc` | A Redis Pub/Sub message router implementing cross-instance message forwarding |
| **DistributedChatService** | `services/shared/chat/src/main_distributed.cc` | The distributed variant of the Chat service |

---

## Core design

### MessageRouter

```cpp
// message routing
router->SendChatMessage(user_id, message,
    [](const std::string& uid) -> bool {
        // 1. try local delivery first
        if (auto session = local_sessions.Get(uid)) {
            SendToSession(session, message);
            return true;  // local delivery succeeded
        }
        return false;  // fall back to Redis Pub/Sub forwarding
    });
```

**Workflow:**
1. On send, first check whether the recipient is on the local instance
2. If the recipient is online locally, deliver directly (avoiding the Redis overhead)
3. If the recipient is offline locally, forward through Redis Pub/Sub to another instance
4. Each recipient's instance subscribes to its own channel and delivers on receipt

### Redis channel design

| Channel pattern | Purpose |
|----------|------|
| `chirp:chat:user:{user_id}` | User private-chat messages |
| `chirp:chat:group:{group_id}` | Group messages |
| `chirp:social:user:{user_id}` | Social notifications |
| `chirp:kick:instance:{id}` | Cross-instance kick notifications |

---

## Build and deploy

### Option 1: Docker Compose cluster deployment

```bash
# 1. build the project
./scripts/build-cluster.sh build

# 2. build the Docker images
./scripts/build-cluster.sh docker

# 3. start the cluster
./scripts/build-cluster.sh start

# 4. check status
docker compose -f docker-compose.cluster.yml ps

# 5. view logs
docker compose -f docker-compose.cluster.yml logs -f chat-1

# 6. scale the service
docker compose -f docker-compose.cluster.yml up -d --scale chat-1=5
```

### Option 2: manual deployment

```bash
# build the distributed Chat service
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --target chirp_chat_distributed

# start multiple instances (ports must not collide to run two on one machine)
./services/shared/chat/chirp_chat_distributed \
    --port 7000 \
    --ws_port 7001 \
    --redis_host 192.168.1.10 \
    --redis_port 6379 \
    --instance_id chat_1 &

./services/shared/chat/chirp_chat_distributed \
    --port 7100 \
    --ws_port 7101 \
    --redis_host 192.168.1.10 \
    --redis_port 6379 \
    --instance_id chat_2 &
```

### Option 3: Kubernetes deployment

```bash
# deploy to Kubernetes
kubectl apply -f deploy/k8s/redis-cluster.yaml
kubectl apply -f deploy/k8s/gateway-deployment.yaml
kubectl apply -f deploy/k8s/chat-deployment.yaml

# check status
kubectl get pods -n chirp

# scale
kubectl scale deployment chirp-chat -n chirp --replicas=5
```

---

## Configuration

### Chat service parameters

| Flag | Description | Default |
|------|------|--------|
| `--port` | TCP port | 7000 |
| `--ws_port` | WebSocket port | 7001 |
| `--redis_host` | Redis host | 127.0.0.1 |
| `--redis_port` | Redis port | 6379 |
| `--offline_ttl` | Offline message TTL (seconds) | 604800 (7 days) |
| `--instance_id` | Instance ID | randomly generated |

### Environment variables

```bash
# Redis configuration
export CHIRP_REDIS_HOST=redis-cluster
export CHIRP_REDIS_PORT=6379

# message storage (MySQL) configuration (see services/shared/chat/src/message_store_config.cc)
export CHIRP_MYSQL_HOST=mysql
export CHIRP_MYSQL_PORT=3306
export CHIRP_MYSQL_DATABASE=chirp
export CHIRP_MYSQL_USER=chirp
export CHIRP_MYSQL_PASSWORD=changeme

# history message migration
export CHIRP_MIGRATION_ENABLED=1
export CHIRP_MIGRATION_BATCH_SIZE=100

# delivery tracking (ACK ledger)
export CHIRP_DELIVERY_TRACKING_ENABLED=1
```

> Note: the instance ID is specified by the Chat service startup flag `--instance_id` (randomly generated as `chat_<hex>` when omitted); see `services/shared/chat/src/main_distributed.cc`. Environment variables such as `CHIRP_INSTANCE_ID`/`CHIRP_LOG_LEVEL` do not exist in the source (ghost configuration) — do not use them, and they will not start anything via environment variables either.

---

## Capacity planning

### Single-instance performance reference

| Metric | Gateway | Chat | Auth |
|------|---------|------|------|
| Max connections | 50K | 20K | 10K |
| Message QPS | 100K | 50K | 20K |
| Memory usage | 1GB | 2GB | 512MB |
| CPU usage | 2 cores | 2 cores | 1 core |

### Suggested cluster sizing

| Online users | Gateway instances | Chat instances | Redis | MySQL |
|----------|-------------|----------|-------|-------|
| 1K online | 1 | 1 | 1 | 1 |
| 10K | 2-3 | 2-3 | 1 master, 2 replicas | 1 master, 1 replica |
| 100K | 5-10 | 5-10 | Cluster (3 masters, 3 replicas) | 1 master, 2 replicas |
| 1M | 20-50 | 20-50 | Cluster (6+ nodes) | sharded tables |

---

## Test verification

### Basic connection test

```bash
# connect to HAProxy
nc localhost 5000

# send a login request (Protobuf)
# a client tool or test script is needed here
```

### Using the test client

```bash
# build the test client (built with the main project's CMake, target name prefixed with chirp_)
cmake -S . -B build-rel -DCMAKE_BUILD_TYPE=Release
cmake --build build-rel --target chirp_chat_send_client

# test multiple user connections
for i in {1..100}; do
    ./build-rel/tools/benchmark/chirp_chat_send_client --host localhost --port 7000 --user user_$i &
done
```

### Distributed scenario tests

1. **Multi-instance message routing test**
   ```bash
   # start two Chat instances
   ./chirp_chat_distributed --instance_id chat_1 --port 7000 &
   ./chirp_chat_distributed --instance_id chat_2 --port 7001 &

   # connect to chat_1 and send a message to a user connected on chat_2
   # verify that the message is routed correctly through Redis
   ```

2. **Failover test**
   ```bash
   # kill one Chat instance
   kill -9 <chat_1_pid>

   # verify that messages can still be routed to chat_2
   ```
---

## Troubleshooting

### Common problems

**1. Messages not delivered**
```bash
# check the Redis connection
docker compose -f docker-compose.cluster.yml exec redis redis-cli ping

# check the subscription state
docker compose -f docker-compose.cluster.yml exec redis redis-cli PUBSUB CHANNELS
```

**2. Offline messages lost**
```bash
# check the offline message queue
docker compose -f docker-compose.cluster.yml exec redis redis-cli LLEN chirp:chat:offline:user_1
```

**3. Performance problems**
```bash
# look at Redis slow queries
docker compose -f docker-compose.cluster.yml exec redis redis-cli SLOWLOG GET 10
```

---

## Monitoring metrics

### Key metrics

| Metric | Description | Alert threshold |
|------|------|----------|
| Connection count | Current active connection count | > 80% of capacity |
| Message QPS | Messages per second | > 80% of capacity |
| Message latency | End-to-end message latency | > 100ms |
| Redis connections | Redis connection pool state | exhausted |
| CPU usage | Instance CPU usage | > 80% |
| Memory usage | Instance memory usage | > 80% |

### Logs

```bash
# view service logs
docker compose -f docker-compose.cluster.yml logs -f chat-1

# view all service logs
docker compose -f docker-compose.cluster.yml logs -f
```

---

## Next steps

1. **Performance optimization**
   - add message batching
   - implement connection pool reuse
   - enable Redis Pipeline

2. **High availability**
   - Redis Sentinel automatic failover
   - MySQL master/slave automatic switchover
   - service health checks and automatic restart

3. **Monitoring**
   - Prometheus metrics export
   - Grafana dashboards
   - alert rule configuration

4. **Security**
   - TLS/SSL encrypted connections
   - authentication and authorization
   - firewall and network isolation
