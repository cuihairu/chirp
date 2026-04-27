---
title: Deployment
---

# Deployment Guide

This guide covers deploying Chirp to production environments.

## Deployment Options

### 1. Docker Deployment (Recommended)

Using Docker Compose for multi-service deployment:

```bash
# Build and start all services
docker-compose up -d

# View logs
docker-compose logs -f

# Stop services
docker-compose down
```

### 2. Kubernetes Deployment

For large-scale deployments:

```bash
# Apply Kubernetes manifests
kubectl apply -f k8s/

# Check deployment status
kubectl get pods -l app=chirp

# Scale services
kubectl scale deployment chirp-gateway --replicas=3
```

### 3. Manual Deployment

Deploy individual services:

```bash
# Start each service
./services/gateway/chirp_gateway &
./services/chat/chirp_chat_enhanced &
./services/social/chirp_social &
./services/voice/chirp_voice &
./services/auth/chirp_auth &
./services/notification/chirp_notification &
```

## Production Checklist

### Security

- [ ] Use strong passwords for MySQL and Redis
- [ ] Enable TLS/SSL for all services
- [ ] Configure firewall rules
- [ ] Enable rate limiting
- [ ] Set up proper CORS policies

### Monitoring

- [ ] Enable Prometheus metrics endpoint
- [ ] Configure logging with appropriate levels
- [ ] Set up alerts for critical failures
- [ ] Monitor Redis memory usage
- [ ] Monitor MySQL connections

### Scalability

- [ ] Configure Redis clustering for high availability
- [ ] Enable MySQL master-slave replication
- [ ] Use HAProxy/nginx for load balancing
- [ ] Configure auto-scaling for pods/containers
- [ ] Enable CDN for static assets

## Service Configuration

### Gateway Service

**Environment Variables:**
```bash
GATEWAY_HOST=0.0.0.0
GATEWAY_PORT=5000
GATEWAY_WS_PORT=5001
REDIS_HOST=redis
REDIS_PORT=6379
LOG_LEVEL=info
```

**Docker Compose:**
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

### Chat Service

**Environment Variables:**
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

### Auth Service

**Environment Variables:**
```bash
AUTH_HOST=0.0.0.0
AUTH_PORT=6000
JWT_SECRET=your-secret-key
JWT_EXPIRATION=86400
REDIS_HOST=redis
MYSQL_HOST=mysql
```

## Load Balancer Configuration

### HAProxy Example

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

### nginx Example

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

## Monitoring Setup

### Prometheus Configuration

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

### Grafana Dashboards

Import the provided dashboard for monitoring:
- Message throughput
- Connection count
- API latency
- Error rates
- Resource usage

## Scaling Guidelines

### Gateway Service

- **Single Instance**: ~10K concurrent connections
- **Recommended**: 3-5 instances behind load balancer
- **Scaling**: Scale based on connection count

### Chat Service

- **Single Instance**: ~5K messages/sec
- **Recommended**: 2-3 instances with Redis pub/sub
- **Scaling**: Scale based on message queue size

### Social Service

- **Single Instance**: ~10K presence updates/sec
- **Recommended**: 2 instances for HA
- **Scaling**: Scale based on friend list size

## Backup Strategy

### MySQL Backup

```bash
# Daily backup
mysqldump -u chirp -pchirp123 chirp > backup_$(date +%Y%m%d).sql

# Restore
mysql -u chirp -pchirp123 chirp < backup_20240318.sql
```

### Redis Backup

```bash
# Snapshot
redis-cli BGSAVE

# Copy dump file
cp /var/lib/redis/dump.rdb backup/
```

## Rollback Procedure

### Service Rollback

```bash
# Stop current version
docker-compose down

# Deploy previous version
docker-compose -f docker-compose.v1.yml up -d

# Verify health
./scripts/health_check.sh
```

### Database Rollback

```bash
# Stop MySQL
sudo systemctl stop mysql

# Restore from backup
mysql -u chirp -pchirp123 chirp < backup.sql

# Start MySQL
sudo systemctl start mysql
```

## Troubleshooting

### High CPU Usage

1. Check connection count per service
2. Review Redis memory usage
3. Enable query logging in MySQL
4. Profile with perf/FlameGraph

### High Memory Usage

1. Check Redis maxmemory setting
2. Review MySQL buffer pool size
3. Enable heap profiling
4. Check for memory leaks

### Connection Drops

1. Verify load balancer health checks
2. Check service logs for errors
3. Monitor network latency
4. Review rate limiting settings

## Next Steps

- [Monitoring Guide](../ops/monitoring.md)
- [Scalability Guide](../ops/scalability.md)
- [API Reference](../api/overview.md)
