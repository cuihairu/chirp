#!/bin/bash
# Chirp 分布式集群部署脚本

set -e

# 配置
COMPOSE_FILE="docker-compose.cluster.yml"
HAPROXY_CFG="deploy/haproxy.cfg"
NAMESPACE="chirp"

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# 检查 Docker
check_docker() {
    if ! command -v docker &> /dev/null; then
        log_error "Docker 未安装，请先安装 Docker"
        exit 1
    fi
    log_info "Docker 版本: $(docker --version)"
}

# 检查 Docker Compose
check_docker_compose() {
    if ! command -v docker-compose &> /dev/null && ! docker compose version &> /dev/null; then
        log_error "Docker Compose 未安装，请先安装 Docker Compose"
        exit 1
    fi
    log_info "Docker Compose 已就绪"
}

# 检查端口占用
check_ports() {
    # 与 docker-compose.cluster.yml 的服务面一致：haproxy 前端(5000-8001)、
    # app-gateway(5200/5201)、server-gateway(8100)、voice(9000/9001)、
    # party(7500/7501)、notification(5006/5016)、search(5007)、redis/mysql。
    local ports=("5000" "5001" "5200" "5201" "6000" "7000" "7001" "7500" "7501" "8000" "8001" "8100" "9000" "9001" "5006" "5016" "5007" "6379" "3306")
    for port in "${ports[@]}"; do
        if lsof -Pi :$port -sTCP:LISTEN -t >/dev/null 2>&1 || netstat -an 2>/dev/null | grep ":$port " | grep LISTEN >/dev/null; then
            log_warn "端口 $port 已被占用"
        fi
    done
}

# 创建必要的目录
setup_directories() {
    log_info "创建部署目录..."
    mkdir -p deploy/k8s
    mkdir -p data/mysql
    mkdir -p data/redis
    mkdir -p logs
}

# 构建镜像
build_images() {
    log_info "构建服务镜像..."
    docker compose -f $COMPOSE_FILE build
}

# 启动集群
start_cluster() {
    log_info "启动 Chirp 集群..."
    docker compose -f $COMPOSE_FILE up -d

    log_info "等待服务启动..."
    sleep 10

    # 检查服务状态
    log_info "服务状态:"
    docker compose -f $COMPOSE_FILE ps
}

# 停止集群
stop_cluster() {
    log_info "停止 Chirp 集群..."
    docker compose -f $COMPOSE_FILE down
}

# 扩展服务
scale_service() {
    local service=$1
    local replicas=$2

    if [ -z "$service" ] || [ -z "$replicas" ]; then
        log_error "用法: $0 scale <service> <replicas>"
        echo "示例: $0 scale gateway 5"
        exit 1
    fi

    log_info "扩展服务 $service 到 $replicas 个副本..."
    docker compose -f $COMPOSE_FILE up -d --scale $service=$replicas
}

# 查看日志
view_logs() {
    local service=$1
    if [ -z "$service" ]; then
        docker compose -f $COMPOSE_FILE logs -f
    else
        docker compose -f $COMPOSE_FILE logs -f $service
    fi
}

# 查看状态
show_status() {
    log_info "集群状态:"
    docker compose -f $COMPOSE_FILE ps

    echo ""
    log_info "连接数统计:"
    echo "Gateway 连接:"
    docker compose -f $COMPOSE_FILE exec -T gateway-1 netstat -an 2>/dev/null | grep ":5000 " | grep ESTABLISHED | wc -l
}

# 健康检查
health_check() {
    log_info "执行健康检查..."

    # 与 docker-compose.cluster.yml 的服务一一对应（检查各自 -1 实例）。
    local services=("gateway" "auth" "chat" "social" "voice" "party" "search" "notification" "server-gateway" "npc" "app-gateway")
    local healthy=0
    local total=0

    for service in "${services[@]}"; do
        total=$((total + 1))
        if docker compose -f $COMPOSE_FILE ps $service-1 | grep -q "Up"; then
            log_info "$service-1: 运行中"
            healthy=$((healthy + 1))
        else
            log_error "$service-1: 未运行"
        fi
    done

    # 检查 Redis
    if docker compose -f $COMPOSE_FILE ps redis | grep -q "Up"; then
        log_info "Redis: 运行中"
        total=$((total + 1))
        healthy=$((healthy + 1))
    fi

    echo ""
    log_info "健康检查: $healthy/$total 服务运行中"

    if [ $healthy -eq $total ]; then
        return 0
    else
        return 1
    fi
}

# Kubernetes 部署
deploy_k8s() {
    if ! command -v kubectl &> /dev/null; then
        log_error "kubectl 未安装"
        exit 1
    fi

    log_info "部署到 Kubernetes..."

    # 镜像前置条件：compose 的 build 不打 tag，k8s 需要预先构建并 tag 好的镜像
    # （每个清单头部注明构建命令，如 chirp/auth、chirp/search 等）。
    log_info "前置: 各服务镜像需预先 docker build -f docker/Dockerfile.service --build-arg SERVICE=<target> -t chirp/<name>:latest ."

    # 创建命名空间
    kubectl create namespace $NAMESPACE --dry-run=client -o yaml | kubectl apply -f -

    # 部署 Redis
    log_info "部署 Redis 集群..."
    kubectl apply -f deploy/k8s/redis-cluster.yaml

    # 等待 Redis 就绪
    kubectl wait --for=condition=ready pod -l app=redis -n $NAMESPACE --timeout=120s

    # 部署基础面: Auth -> Chat -> Search（依赖 MySQL/Redis,清单内含 initContainer 等待）
    log_info "部署 Auth..."
    kubectl apply -f deploy/k8s/auth-deployment.yaml

    log_info "部署 Chat..."
    kubectl apply -f deploy/k8s/chat-deployment.yaml

    log_info "部署 Search..."
    kubectl apply -f deploy/k8s/search-deployment.yaml

    # 部署 server 平面: hub -> npc(npc 是 hub 的出站客户端)
    log_info "部署 Server Gateway..."
    kubectl apply -f deploy/k8s/server-gateway-deployment.yaml

    log_info "部署 NPC..."
    kubectl apply -f deploy/k8s/npc-deployment.yaml

    # 部署房间面
    log_info "部署 Voice..."
    kubectl apply -f deploy/k8s/voice-deployment.yaml

    log_info "部署 Party..."
    kubectl apply -f deploy/k8s/party-deployment.yaml

    # 部署 app 平面: 推送 + companion 聚合边
    log_info "部署 Notification..."
    kubectl apply -f deploy/k8s/notification-deployment.yaml

    log_info "部署 App Gateway..."
    kubectl apply -f deploy/k8s/app-gateway-deployment.yaml

    # 部署 Gateway（game 平面边,依赖 auth/chat/search）
    log_info "部署 Gateway..."
    kubectl apply -f deploy/k8s/gateway-deployment.yaml

    log_info "Kubernetes 部署完成"
    log_info "查看状态: kubectl get pods -n $NAMESPACE"
}

# 性能测试
run_benchmark() {
    log_info "运行性能测试..."

    local gateway_host="localhost"
    local gateway_port="5000"
    local connections=1000
    local duration=60

    # 使用 wrk 或自定义客户端进行测试
    log_info "测试参数: 连接数=$connections, 持续时间=${duration}s"

    # 这里可以集成实际的测试工具
    # docker run --rm -t chirp/benchmark \
    #   --host $gateway_host \
    #   --port $gateway_port \
    #   --connections $connections \
    #   --duration $duration
}

# 清理数据
clean_data() {
    log_warn "这将删除所有数据，确认继续? (y/N)"
    read -r confirm
    if [ "$confirm" != "y" ]; then
        return
    fi

    log_info "清理数据..."
    stop_cluster
    rm -rf data/*
    log_info "数据已清理"
}

# 显示帮助
show_help() {
    cat << EOF
Chirp 分布式集群部署脚本

用法: $0 [command] [options]

命令:
  build       构建服务镜像
  start       启动集群
  stop        停止集群
  restart     重启集群
  scale       扩展服务实例数
    示例: $0 scale gateway 5
  status      查看集群状态
  logs        查看服务日志
    示例: $0 logs [service]
  health      执行健康检查
  benchmark   运行性能测试
  k8s         部署到 Kubernetes
  clean       清理所有数据
  help        显示此帮助

示例:
  $0 build           # 构建镜像
  $0 start           # 启动集群
  $0 scale chat 5    # 扩展 Chat 服务到 5 个实例
  $0 logs gateway    # 查看 Gateway 日志
  $0 health          # 健康检查
EOF
}

# 主函数
main() {
    check_docker
    check_docker_compose
    setup_directories

    local command=$1
    shift || true

    case $command in
        build)
            build_images
            ;;
        start)
            start_cluster
            ;;
        stop)
            stop_cluster
            ;;
        restart)
            stop_cluster
            start_cluster
            ;;
        scale)
            scale_service "$@"
            ;;
        status)
            show_status
            ;;
        logs)
            view_logs "$@"
            ;;
        health)
            health_check
            ;;
        benchmark)
            run_benchmark
            ;;
        k8s)
            deploy_k8s
            ;;
        clean)
            clean_data
            ;;
        help|--help|-h)
            show_help
            ;;
        *)
            log_error "未知命令: $command"
            show_help
            exit 1
            ;;
    esac
}

main "$@"
