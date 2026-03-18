---
title: Installation
icon: 📦
---

# Installation Guide

This guide covers installing Chirp from source and setting up the development environment.

## System Requirements

### Linux (Ubuntu 22.04+)

**Required:**
- GCC 9+ or Clang 11+
- CMake 3.20+
- Ninja build system
- Protocol Buffers compiler
- Redis 7.0+
- MySQL 8.0+

**Install:**
```bash
sudo apt-get update
sudo apt-get install -y \
    cmake \
    ninja-build \
    gcc g++ \
    libprotobuf-dev \
    protobuf-compiler \
    libabsl-dev \
    libssl-dev \
    redis-server \
    mysql-server \
    libmysqlclient-dev \
    pkg-config
```

### macOS (12+)

**Required:**
- Xcode Command Line Tools
- Homebrew

**Install:**
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
- Visual Studio 2022
- vcpkg
- CMake

**Install:**
```powershell
# Install vcpkg
git clone https://github.com/Microsoft/vcpkg.git C:\vcpkg
.\vcpkg\bootstrap-vcpkg.bat
.\vcpkg\integrate install

# Install dependencies
vcpkg install protobuf absl openssl mysql-connector-cpp redis-plus
```

## Building from Source

### 1. Clone Repository

```bash
git clone https://github.com/cuihairu/chirp.git
cd chirp
```

### 2. Generate Protocol Buffer Files

```bash
chmod +x gen_proto.sh
./gen_proto.sh
```

This generates C++ files from `.proto` definitions in the `proto/cpp/proto/` directory.

### 3. Configure with CMake

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

### 4. Build

```bash
cmake --build . --parallel
```

### 5. Run Tests (Optional)

```bash
ctest --output-on-failure
```

## Docker Build

### Using Docker Compose (Recommended)

```bash
docker-compose up -d
```

### Manual Docker Build

```bash
# Build Gateway image
docker build -t chirp/gateway:latest -f services/gateway/Dockerfile .

# Build Chat image
docker build -t chirp/chat:latest -f services/chat/Dockerfile .

# Build all services
docker-compose build
```

## Configuration

### Environment Variables

Create a `.env` file in the project root:

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

### Service Configuration

Each service can be configured via JSON files placed in `config/`:

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

## Database Setup

### MySQL Schema

```bash
# Create database
mysql -u root -p -e "CREATE DATABASE chirp;"

# Create user
mysql -u root -p -e "CREATE USER 'chirp'@'localhost' IDENTIFIED BY 'chirp123';"
mysql -u root -p -e "GRANT ALL PRIVILEGES ON chirp.* TO 'chirp'@'localhost';"

# Import schema
mysql -u chirp -pchirp123 chirp < scripts/init_db.sql
```

### Redis Setup

```bash
# Start Redis
redis-server --daemonize yes

# Test connection
redis-cli ping
# Should return: PONG
```

## Verification

### Test Gateway

```bash
./build/services/gateway/chirp_gateway
# Output: Gateway service listening on 0.0.0.0:5000
```

### Test CLI Client

```bash
./build/apps/cli_client/chirp_cli
```

### Test Services

```bash
# Test all services at once
./build/apps/load_tester/chirp_load_tester \
    --connections 100 \
    --messages 1000 \
    --duration 60
```

## Troubleshooting

### Protobuf Issues

**Problem**: Protobuf not found
```bash
export CMAKE_PREFIX_PATH=/usr/local
cmake ..
```

### MySQL Linker Errors

**Problem**: libmysqlclient not found
```bash
# Linux
export MYSQL_DIR=/usr
cmake -DMYSQL_INCLUDE_DIR=/usr/include/mysql \
      -DMYSQL_LIBRARY=/usr/lib/x86_64-linux-gnu/libmysqlclient.so ..

# macOS
cmake -DMYSQL_DIR=$(brew --prefix mysql) ..
```

### Redis Connection

**Problem**: Cannot connect to Redis
```bash
# Check Redis status
redis-cli ping

# Check if port is open
netstat -an | grep 6379
```

## Next Steps

- [Quick Start Guide](./quickstart.md)
- [Architecture Overview](./architecture.md)
- [Deployment Guide](./deployment.md)
