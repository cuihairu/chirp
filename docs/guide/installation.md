---
title: 安装指南
---

# 安装指南

> 状态说明:本页是环境搭建指引,但当前受支持的运行时目标仍是核心 `gateway + auth + chat` 路径。最基本的本地构建可以不要 Redis 和 MySQL;增强版 auth/chat 路径需要额外的本地依赖。

本指南覆盖从源码安装 Chirp 和搭建开发环境。

## 系统要求

### Linux 系统(Ubuntu 22.04+)

**必需:**
- GCC 13+ 或 Clang 17+
- CMake 3.21 或更新
- Ninja 构建系统
- Protocol Buffers 编译器

**扩展运行时路径可选:**
- Redis(缓存与会话存储)
- MySQL(数据库服务)

**安装:**
```bash
sudo apt-get update
sudo apt-get install -y \
    cmake \
    ninja-build \
    gcc-13 g++-13 \
    libprotobuf-dev \
    protobuf-compiler \
    libabsl-dev \
    libssl-dev \
    redis-server \
    mysql-server \
    libmariadb-dev \
    pkg-config
```

### macOS 系统(12+)

**必需:**
- Xcode Command Line Tools(Apple 命令行工具)
- Homebrew(macOS 包管理器)

**安装:**
```bash
# Install dependencies
brew install cmake protobuf abseil openssl mysql redis
brew install pkg-config

# Start services
brew services start redis
brew services start mysql
```

### Windows 系统(11+)

**必需:**
- Visual Studio 2022 17.10 或更新
- vcpkg(C++ 依赖管理器)
- CMake(构建工具)

**安装:**
```powershell
# Install vcpkg
git clone https://github.com/Microsoft/vcpkg.git C:\vcpkg
.\vcpkg\bootstrap-vcpkg.bat
.\vcpkg\integrate install

# Install dependencies
vcpkg install protobuf absl openssl libmariadb redis-plus asio
```

## 从源码构建

### 1. 克隆仓库

```bash
git clone https://github.com/cuihairu/chirp.git
cd chirp
```

### 2. 生成 Protocol Buffer 文件

```bash
chmod +x gen_proto.sh
./gen_proto.sh
```

这会从 `proto/` 下的 `.proto` 定义生成 C++ 文件到 `proto/cpp/proto/` 目录。

### 3. 用 CMake 配置

```bash
mkdir build && cd build

# Debug build
cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug \
    -DENABLE_TESTS=ON \
    -DCMAKE_TOOLCHAIN_FILE=../vcpkg/scripts/buildsystems/vcpkg.cmake ..

# Release build
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DENABLE_TESTS=OFF \
    -DCMAKE_TOOLCHAIN_FILE=../vcpkg/scripts/buildsystems/vcpkg.cmake ..
```

### 4. 构建

```bash
cmake --build . --parallel
```

macOS 上用 vcpkg 时,还要加:

```bash
-DCMAKE_OSX_ARCHITECTURES="$(uname -m)"
```

### 5. 跑测试(可选)

```bash
ctest --output-on-failure
```

## Docker 构建

### 用 Docker Compose(推荐)

```bash
docker-compose up -d
```

### 手动 Docker 构建

```bash
# Build Gateway image
docker build -t chirp/gateway:latest -f services/gateway/Dockerfile .

# Build Chat image
docker build -t chirp/chat:latest -f services/chat/Dockerfile .

# Build all services
docker-compose build
```

## 配置

### 环境变量

在项目根目录建一个 `.env` 文件:

```bash
# Environment
CHIRP_ENV=development

# Redis
REDIS_HOST=localhost
REDIS_PORT=6379
REDIS_PASSWORD=

# MySQL
MYSQL_HOST=localhost
MYSQL_PORT=3306
MYSQL_DATABASE=chirp
MYSQL_USER=chirp
MYSQL_PASSWORD=chirp123
```

### 服务配置

每个服务都可以用放在 `config/` 下的 JSON 文件配置:

```json
{
  "host": "0.0.0.0",
  "port": 5000,
  "workers": 4,
  "log": {
    "level": "info",
    "file": "logs/gateway.log"
  },
  "redis": {
    "host": "localhost",
    "port": 6379
  }
}
```

## 数据库准备

### MySQL schema(表结构)

```bash
# Create database
mysql -u root -p -e "CREATE DATABASE chirp;"

# Create user
mysql -u root -p -e "CREATE USER 'chirp'@'localhost' IDENTIFIED BY 'chirp123';"
mysql -u root -p -e "GRANT ALL PRIVILEGES ON chirp.* TO 'chirp'@'localhost';"

# Import schema
mysql -u chirp -pchirp123 chirp < scripts/init_db.sql
```

### Redis 准备

```bash
# Start Redis
redis-server --daemonize yes

# Test connection
redis-cli ping
# Should return: PONG
```

## 验证

### 测试 Gateway

```bash
./build/services/gateway/chirp_gateway
# Output: Gateway service listening on 0.0.0.0:5000
```

### 测试 CLI 客户端

```bash
./build/apps/cli_client/chirp_cli
```

### 测试服务

```bash
# Test all services at once
./build/apps/load_tester/chirp_load_tester \
    --connections 100 \
    --messages 1000 \
    --duration 60
```

## 故障排查

### Protobuf 问题

**问题**:找不到 Protobuf
```bash
export CMAKE_PREFIX_PATH=/usr/local
cmake ..
```

### MySQL 链接错误

**问题**:找不到 MySQL 客户端库(libmariadb)
```bash
# Linux
export MYSQL_DIR=/usr
cmake -DMYSQL_INCLUDE_DIR=/usr/include/mysql \
      -DMYSQL_LIBRARY=/usr/lib/x86_64-linux-gnu/libmariadb.so ..

# macOS
cmake -DMYSQL_DIR=$(brew --prefix mysql) ..
```

### Redis 连接

**问题**:连不上 Redis
```bash
# Check Redis status
redis-cli ping

# Check if port is open
netstat -an | grep 6379
```

## 下一步

- [快速上手](./getting-started.md)
- [架构总览](../architecture.md)
- [部署指南](./deployment.md)
