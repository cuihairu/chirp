> 🌐 Language: [中文](./installation.md) | **English** (this page)

# Installation Guide

> Status note: this page is an environment setup guide, but the currently supported runtime target is still the core `gateway + auth + chat` path. The most basic local build needs neither Redis nor MySQL; the enhanced auth/chat path needs extra local dependencies.

This guide covers installing Chirp from source and setting up the development environment.

## System requirements

### Linux (Ubuntu 22.04+)

**Required:**
- GCC 13+ or Clang 17+
- CMake 3.21 or newer
- The Ninja build system
- The Protocol Buffers compiler

**Optional, for the extended runtime path:**
- Redis (cache and session storage)
- MySQL (database service)

**Installation:**
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

### macOS (12+)

**Required:**
- Xcode Command Line Tools
- Homebrew (macOS package manager)

**Installation:**
```bash
# Install dependencies
brew install cmake protobuf abseil openssl mysql redis
brew install pkg-config

# Start services
brew services start redis
brew services start mysql
```

### Windows (11+)

**Required:**
- Visual Studio 2022 17.10 or newer
- vcpkg (C++ dependency manager)
- CMake (build tool)

**Installation:**
```powershell
# Install vcpkg
git clone https://github.com/Microsoft/vcpkg.git C:\vcpkg
.\vcpkg\bootstrap-vcpkg.bat
.\vcpkg\integrate install

# 依赖走仓库根的 vcpkg.json manifest 模式(protobuf、abseil、libsodium、
# libmariadb、openssl、pkgconf、zlib、asio、sqlite3[fts5 内建]),CMake 配置时自动安装,
# 不需要手工 vcpkg install 清单。
```

## Building from source

### 1. Clone the repository

```bash
git clone https://github.com/cuihairu/chirp.git
cd chirp
```

### 2. Generate the Protocol Buffer files

```bash
chmod +x gen_proto.sh
./gen_proto.sh
```

This generates C++ files from the `.proto` definitions under `proto/` into the `proto/cpp/proto/` directory.

### 3. Configure with CMake

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

### 4. Build

```bash
cmake --build . --parallel
```

On macOS with vcpkg, also add:

```bash
-DCMAKE_OSX_ARCHITECTURES="$(uname -m)"
```

### 5. Run the tests (optional)

```bash
ctest --output-on-failure
```

## Docker build

### Using Docker Compose (recommended)

```bash
docker compose up -d
```

### Manual Docker build

Single-service images go through `docker/Dockerfile.service`; pick the target with the `SERVICE` build argument (valid values are listed in `docker-compose.yml`, e.g. `game_sdk_gateway`, `chat_enhanced`, `app_auth`):

```bash
docker build --build-arg SERVICE=game_sdk_gateway -t chirp/gateway:latest -f docker/Dockerfile.service .

# Build Chat image
docker build --build-arg SERVICE=chat_enhanced -t chirp/chat:latest -f docker/Dockerfile.service .

# Build all services
docker compose build
```

## Configuration

### Environment variables

Only the chat storage layer reads environment variables, the `CHIRP_`-prefixed family (`CHIRP_REDIS_HOST` / `CHIRP_REDIS_PORT` / `CHIRP_MYSQL_HOST` / `CHIRP_MYSQL_PORT` / `CHIRP_MYSQL_DATABASE` / `CHIRP_MYSQL_USER` / `CHIRP_MYSQL_PASSWORD` / `CHIRP_MIGRATION_*` / `CHIRP_DELIVERY_TRACKING_ENABLED`); the read points are in `services/shared/chat/src/message_store_config.cc`. Another family is read by the crash handler — `CHIRP_CRASH_DIR` / `CHIRP_CRASH_UPLOAD_URL` / `CHIRP_CRASH_HANDLER` (`libs/common/crash_handler.cc`, wired into every service main; a process-level behavior, not a storage-layer one). Outside these two families, the other services read no environment variables.

### Service configuration

All services are configured with command-line arguments; complete examples are in the `command:` entries of `docker-compose.yml`; the authoritative list of arguments is each service's `main.cc` parsing code. There is no JSON/config-file mechanism in the repository.

## Database preparation

### MySQL schema

```bash
# Create database
mysql -u root -p -e "CREATE DATABASE chirp;"

# Create user
mysql -u root -p -e "CREATE USER 'chirp'@'localhost' IDENTIFIED BY 'chirp123';"
mysql -u root -p -e "GRANT ALL PRIVILEGES ON chirp.* TO 'chirp'@'localhost';"

# Import schema
mysql -u chirp -pchirp123 chirp < scripts/init_db.sql
```

> Upgrading an existing database: `init_db.sql` only takes effect when it first creates the tables; to add the three columns for message references / recall tombstones / sender kind (`reply_to` / `is_recalled` / `sender_kind`, without which `StoreMessage` fails) in an existing database, run:
> `mysql -u chirp -pchirp123 chirp < scripts/upgrade_db_messages.sql`
> (The script is not idempotent; re-running it reports duplicate column — check the table structure first.)

### Redis preparation

```bash
# Start Redis
redis-server --daemonize yes

# Test connection
redis-cli ping
# Should return: PONG
```

## Verification

### Testing the Gateway

```bash
./build/services/game/sdk_gateway/chirp_game_sdk_gateway --port 5000 --ws_port 5001
```

### Testing the CLI client

```bash
./build/apps/cli_client/chirp_cli
```

### Testing the services

```bash
# Test all services at once
./build/apps/load_tester/chirp_load_tester \
    --connections 100 \
    --msg-per-sec 20 \
    --duration 60
```

## Troubleshooting

### Protobuf problems

**Problem**: Protobuf cannot be found
```bash
export CMAKE_PREFIX_PATH=/usr/local
cmake ..
```

### MySQL link errors

**Problem**: The MySQL client library (libmariadb) cannot be found
```bash
# Linux
export MYSQL_DIR=/usr
cmake -DMYSQL_INCLUDE_DIR=/usr/include/mysql \
      -DMYSQL_LIBRARY=/usr/lib/x86_64-linux-gnu/libmariadb.so ..

# macOS
cmake -DMYSQL_DIR=$(brew --prefix mysql) ..
```

### Redis connectivity

**Problem**: Cannot connect to Redis
```bash
# Check Redis status
redis-cli ping

# Check if the port is open
netstat -an | grep 6379
```

## Next steps

- [Getting Started](./getting-started.en.md)
- [Overall Architecture](../architecture.en.md)
- [Deployment Guide](./deployment.en.md)
