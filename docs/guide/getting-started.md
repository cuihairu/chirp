---
title: Getting Started
icon: 📖
---

# Getting Started

This guide will help you get up and running with Chirp quickly.

## Prerequisites

### Development Environment

- **C++ Compiler**: GCC 9+ / Clang 11+ / MSVC 2022+
- **CMake**: Version 3.20 or higher
- **vcpkg**: For dependency management
- **Protocol Buffers**: `protoc` compiler
- **Redis**: For session management and pub/sub
- **MySQL**: 8.0+ for persistent storage

### Optional Dependencies

- **Docker**: For containerized deployment
- **Python 3**: For build scripts
- **Node.js 18+**: For web dashboard

## Quick Start

### 1. Clone the Repository

```bash
git clone https://github.com/cuihairu/chirp.git
cd chirp
```

### 2. Install Dependencies

#### On Linux/macOS

```bash
# Install build tools
sudo apt-get install cmake ninja-build gcc g++ libprotobuf-dev protobuf-compiler

# Install Redis (Ubuntu/Debian)
sudo apt-get install redis-server

# Install MySQL (Ubuntu/Debian)
sudo apt-get install mysql-server libmysqlclient-dev
```

#### On Windows

```bash
# Use vcpkg for dependencies
vcpkg install protobuf redis-plus libmysql asio
```

### 3. Build Protobuf Files

```bash
./gen_proto.sh
```

### 4. Build the Project

```bash
mkdir build && cd build
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release ..
cmake --build . --parallel
```

### 5. Start Services

```bash
# Start Redis
redis-server --daemonize yes

# Start MySQL (if needed)
sudo systemctl start mysql

# Start Gateway
./build/services/gateway/chirp_gateway

# Start Chat Service
./build/services/chat/chirp_chat_enhanced

# Start other services as needed...
```

## Running the CLI Client

Chirp includes a simple CLI client for testing:

```bash
./build/apps/cli_client/chirp_cli
```

Example CLI session:
```
> connect localhost 5000
Connected!
> login user123
Logged in as user123
> send Hello, Chirp!
Message sent
> quit
```

## Docker Quick Start

For a complete development environment, use Docker Compose:

```bash
docker-compose up -d
```

This will start:
- Gateway Service (ports 5000, 5001)
- Chat Service (ports 7000, 7001)
- Social Service (ports 8000, 8001)
- Voice Service (ports 9000, 9001)
- Auth Service (port 6000)
- Notification Service (port 5006)
- Redis
- MySQL

## Configuration

### Environment Variables

| Variable | Description | Default |
|----------|-------------|---------|
| `CHIRP_ENV` | Environment (dev/staging/prod) | `dev` |
| `REDIS_HOST` | Redis server host | `localhost` |
| `REDIS_PORT` | Redis server port | `6379` |
| `MYSQL_HOST` | MySQL server host | `localhost` |
| `MYSQL_PORT` | MySQL server port | `3306` |
| `MYSQL_DATABASE` | Database name | `chirp` |
| `MYSQL_USER` | Database user | `chirp` |
| `MYSQL_PASSWORD` | Database password | `chirp123` |

### Configuration Files

Services can be configured via JSON config files:

```json
{
  "host": "0.0.0.0",
  "port": 5000,
  "redis": {
    "host": "localhost",
    "port": 6379
  },
  "log_level": "info"
}
```

## Next Steps

- Learn about the [Architecture](./architecture.md)
- Explore the [API Reference](../api/overview.md)
- Check out [SDK Guides](../sdk/overview.md)
- Read about [Deployment](./deployment.md)

## Troubleshooting

### Build Issues

**Problem**: CMake can't find Protobuf
```bash
export CMAKE_PREFIX_PATH=/usr/local
cmake ..
```

**Problem**: Linker errors on Linux
```bash
sudo apt-get install libabsl-dev
```

### Runtime Issues

**Problem**: Services can't connect to Redis
```bash
# Check if Redis is running
redis-cli ping
# Should return: PONG
```

**Problem**: Database connection errors
```bash
# Check MySQL
mysql -u chirp -p chirp123
# Create database if needed
CREATE DATABASE chirp;
```

## Getting Help

- Check the [GitHub Issues](https://github.com/cuihairu/chirp/issues)
- Start a [Discussion](https://github.com/cuihairu/chirp/discussions)
- Read the [API Documentation](../api/overview.md)
