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

# 依赖走仓库根的 vcpkg.json manifest 模式(protobuf、abseil、libsodium、
# libmariadb、openssl、asio、sqlite3[fts5 内建]),CMake 配置时自动安装,
# 不需要手工 vcpkg install 清单。
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

# Debug build(VCPKG_ROOT 指向 vcpkg 检出目录,如 Windows 上的 C:\vcpkg)
cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug \
    -DENABLE_TESTS=ON \
    -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake ..

# Release build
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DENABLE_TESTS=OFF \
    -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake ..
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
docker compose up -d
```

### 手动 Docker 构建

单服务镜像走 `docker/Dockerfile.service`,`SERVICE` 构建参数选目标(可用值见 `docker-compose.yml`,如 `game_sdk_gateway`、`chat_enhanced`、`app_auth`):

```bash
docker build --build-arg SERVICE=game_sdk_gateway -t chirp/gateway:latest -f docker/Dockerfile.service .

# Build Chat image
docker build --build-arg SERVICE=chat_enhanced -t chirp/chat:latest -f docker/Dockerfile.service .

# Build all services
docker compose build
```

## 配置

### 环境变量

只有 chat 的存储层读环境变量,`CHIRP_` 前缀一族(`CHIRP_REDIS_HOST` / `CHIRP_REDIS_PORT` / `CHIRP_MYSQL_HOST` / `CHIRP_MYSQL_PORT` / `CHIRP_MYSQL_DATABASE` / `CHIRP_MYSQL_USER` / `CHIRP_MYSQL_PASSWORD` / `CHIRP_MIGRATION_*` / `CHIRP_DELIVERY_TRACKING_ENABLED`),读取点在 `services/shared/chat/src/message_store_config.cc`。其余服务不读环境变量。

### 服务配置

所有服务用命令行参数配置,完整样例见 `docker-compose.yml` 各 `command:`;参数清单以各服务 `main.cc` 的解析代码为准。仓库没有 JSON/配置文件机制。

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

> 存量库升级：`init_db.sql` 只在首次建表时生效；已有库补消息引用/撤回墓碑/
> 发送者类型三列（`reply_to` / `is_recalled` / `sender_kind`，缺失时写入
> `StoreMessage` 会失败）执行：
> `mysql -u chirp -pchirp123 chirp < scripts/upgrade_db_messages.sql`
> （脚本非幂等，重复执行会报 duplicate column；执行前先核对表结构。）

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
./build/services/game/sdk_gateway/chirp_game_sdk_gateway --port 5000 --ws_port 5001
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
    --msg-per-sec 20 \
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
