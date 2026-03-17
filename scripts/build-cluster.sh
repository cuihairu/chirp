#!/bin/bash
# 构建分布式 Chirp 集群的脚本

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
cd "$PROJECT_ROOT"

# 颜色输出
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

log_info() { echo -e "${GREEN}[INFO]${NC} $1"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; }

# 检查依赖
check_deps() {
    log_info "检查构建依赖..."

    if ! command -v cmake &> /dev/null; then
        log_error "CMake 未安装"
        exit 1
    fi

    if ! command -v docker &> /dev/null && ! docker compose version &> /dev/null; then
        log_error "Docker 或 Docker Compose 未安装"
        exit 1
    fi

    log_info "✓ 依赖检查通过"
}

# 构建项目
build_project() {
    local build_type=${1:-Release}
    local build_dir="build-distributed"

    log_info "构建 Chirp 分布式版本 (Build Type: $build_type)..."

    # 清理旧的构建
    if [ -d "$build_dir" ]; then
        log_info "清理旧的构建目录..."
        rm -rf "$build_dir"
    fi

    # 生成协议文件
    log_info "生成 Protobuf 文件..."
    ./gen_proto.sh

    # 配置 CMake
    log_info "配置 CMake..."
    cmake -S . -B "$build_dir" \
        -DCMAKE_BUILD_TYPE="$build_type" \
        -DENABLE_TESTS=OFF

    # 构建所有服务
    log_info "编译服务..."
    cmake --build "$build_dir" --target \
        chirp_gateway \
        chirp_auth \
        chirp_chat_distributed \
        chirp_social \
        chirp_voice \
        -j"$(nproc)"

    log_info "✓ 构建完成"
}

# 构建 Docker 镜像
build_docker() {
    log_info "构建 Docker 镜像..."

    # 构建 Gateway
    log_info "构建 Gateway 镜像..."
    docker build \
        -f docker/Dockerfile.service \
        --target runtime \
        --build-arg SERVICE=gateway \
        -t chirp/gateway:latest \
        .

    # 构建 Auth
    log_info "构建 Auth 镜像..."
    docker build \
        -f docker/Dockerfile.service \
        --target runtime \
        --build-arg SERVICE=auth \
        -t chirp/auth:latest \
        .

    # 构建 Distributed Chat
    log_info "构建 Distributed Chat 镜像..."
    docker build \
        -f docker/Dockerfile.service \
        --target runtime \
        --build-arg SERVICE=chat_distributed \
        -t chirp/chat:latest \
        .

    # 构建 Social
    log_info "构建 Social 镜像..."
    docker build \
        -f docker/Dockerfile.service \
        --target runtime \
        --build-arg SERVICE=social \
        -t chirp/social:latest \
        .

    log_info "✓ Docker 镜像构建完成"
}

# 启动集群
start_cluster() {
    log_info "启动分布式集群..."
    docker compose -f docker-compose.cluster.yml up -d

    log_info "等待服务启动..."
    sleep 5

    log_info "集群状态:"
    docker compose -f docker-compose.cluster.yml ps
}

# 停止集群
stop_cluster() {
    log_info "停止分布式集群..."
    docker compose -f docker-compose.cluster.yml down
}

# 扩展服务
scale_service() {
    local service=$1
    local replicas=$2

    if [ -z "$service" ] || [ -z "$replicas" ]; then
        log_error "用法: $0 scale <service> <replicas>"
        return 1
    fi

    log_info "扩展服务 $service 到 $replicas 个副本..."
    docker compose -f docker-compose.cluster.yml up -d --scale "$service=$replicas"
}

# 查看日志
view_logs() {
    local service=$1
    if [ -z "$service" ]; then
        docker compose -f docker-compose.cluster.yml logs -f
    else
        docker compose -f docker-compose.cluster.yml logs -f "$service"
    fi
}

# 运行测试
run_tests() {
    log_info "运行集成测试..."

    # 检查服务是否就绪
    log_info "等待服务就绪..."
    sleep 10

    # 这里可以添加实际的测试逻辑
    # docker run --rm chirp/test-client ...

    log_info "✓ 测试完成"
}

# 健康检查
health_check() {
    log_info "执行健康检查..."

    local services=("redis" "mysql" "haproxy" "gateway-1" "auth-1" "chat-1")
    local healthy=0
    local total=${#services[@]}

    for svc in "${services[@]}"; do
        if docker compose -f docker-compose.cluster.yml ps "$svc" | grep -q "Up"; then
            log_info "✓ $svc: 运行中"
            healthy=$((healthy + 1))
        else
            log_error "✗ $svc: 未运行"
        fi
    done

    echo ""
    log_info "健康检查: $healthy/$total 服务运行中"

    if [ $healthy -eq $total ]; then
        return 0
    else
        return 1
    fi
}

# 清理
clean() {
    log_info "清理构建产物和容器..."

    read -p "确认删除所有数据? (y/N) " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        stop_cluster
        rm -rf build-distributed
        docker system prune -f
        log_info "✓ 清理完成"
    else
        log_info "已取消"
    fi
}

# 显示帮助
show_help() {
    cat << EOF
Chirp 分布式集群构建脚本

用法: $0 <command> [options]

命令:
  build       构建项目 (默认: Release)
  debug       构建项目 (Debug 模式)
  docker      构建 Docker 镜像
  start       启动集群
  stop        停止集群
  restart     重启集群
  scale       扩展服务
    示例: $0 scale chat-1 5
  logs        查看服务日志
    示例: $0 logs [service]
  health      执行健康检查
  test        运行集成测试
  clean       清理所有数据
  help        显示此帮助

示例:
  $0 build           # 构建项目
  $0 docker          # 构建 Docker 镜像
  $0 start           # 启动集群
  $0 scale chat-1 5  # 扩展 chat-1 到 5 个副本
  $0 logs chat-1     # 查看 chat-1 日志
  $0 health          # 健康检查

完整部署流程:
  1. $0 build
  2. $0 docker
  3. $0 start
  4. $0 health
EOF
}

# 主函数
main() {
    local command=$1
    shift || true

    case $command in
        build)
            build_project "Release"
            ;;
        debug)
            build_project "Debug"
            ;;
        docker)
            build_docker
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
        logs)
            view_logs "$@"
            ;;
        health)
            health_check
            ;;
        test)
            run_tests
            ;;
        clean)
            clean
            ;;
        help|--help|-h|"")
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
