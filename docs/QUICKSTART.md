# Chirp Quick Start Guide

## What is Chirp?

Chirp is a lightweight, scalable chat backend designed specifically for game development. It provides:

- **Real-time messaging** with sub-millisecond latency
- **Multi-platform support** via TCP (game clients) and WebSocket (web/mobile)
- **Social features** including friends, presence, and groups
- **Voice chat** with WebRTC signaling
- **Distributed architecture** with Redis for session management

## Quick Start (5 Minutes)

### Prerequisites

**Required:**
- C++17 compiler (MSVC 2019+, GCC 9+, Clang 10+)
- CMake 3.15+
- [vcpkg](https://github.com/microsoft/vcpkg) package manager
- Git

**Optional:**
- Docker & Docker Compose (for services)

### Step 1: Install vcpkg

vcpkg is used for dependency management (protobuf, etc.)

```bash
# Clone vcpkg
git clone https://github.com/Microsoft/vcpkg.git
cd vcpkg

# Run bootstrap script
./bootstrap-vcpkg.bat  # Windows
# or
./bootstrap-vcpkg.sh   # Linux/Mac

# Install manifest dependencies from vcpkg.json
./vcpkg install
```

Set environment variable (optional, for convenience):
```bash
# Windows
set VCPKG_ROOT=C:\path\to\vcpkg

# Linux/Mac
export VCPKG_ROOT=/path/to/vcpkg
```

### Step 2: Clone and Build

```bash
# Clone repository
git clone <repository-url>
cd chirp

# Run build script (uses vcpkg automatically)
bash scripts/build_and_test.sh
```

Or manually:

```bash
# Generate protobuf files (uses vcpkg's protoc)
/vc/vcpkg/installed/x64-windows/tools/protobuf/protoc \
  --proto_path=. --cpp_out=proto/cpp \
  proto/common.proto proto/gateway.proto proto/auth.proto \
  proto/chat.proto proto/social.proto proto/voice.proto proto/notification.proto

# Configure with vcpkg toolchain
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022" -A x64 \
  -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake \
  -DENABLE_TESTS=OFF

# Build
cmake --build . --config Debug
```

### Step 3: Start Services

```bash
docker compose up -d
```

This starts:
- **Redis** (port 6379) - caching and sessions
- **MySQL** (port 3306) - persistence (optional)
- **Auth** (port 6000) - authentication
- **Gateway** (ports 5000/5001) - TCP/WebSocket gateway
- **Chat** (ports 7000/7001) - messaging service
- **Social** (ports 8000/8001) - friends and presence
- **Voice** (ports 9000/9001) - voice chat signaling

### Step 4: Test with CLI Client

```bash
cd apps/cli_client
mkdir -p build && cd build
cmake ../.. -G "Visual Studio 17 2022" -A x64 \
  -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build . --config Debug

# Run CLI client
./Debug/chirp_cli
> connect localhost 5000
> login player1
> msg player2 Hello!
```

## Core SDK Usage

```cpp
#include "chirp/core/sdk.h"

using namespace chirp::core;

int main() {
  // Configure
  Config config;
  config.app_id = "my_game";
  config.user_id = "player1";
  config.connection.chat_host = "localhost";
  config.connection.chat_port = 7000;

  // Initialize
  SDK::Initialize(config);

  // Connect and login
  auto* client = SDK::GetClient();
  client->Connect();
  client->Login("player1");

  // Use Chat module
  auto* chat = client->GetChatModule();
  chat->SetMessageCallback([](const Message& msg) {
    std::cout << "Received: " << msg.content << std::endl;
  });

  chat->SendMessage("player2", MessageType::TEXT, "Hello!",
    [](SendResult result, const std::string& id) {
      std::cout << "Sent: " << id << std::endl;
    });

  // Keep running
  std::this_thread::sleep_for(std::chrono::minutes(5));

  // Cleanup
  SDK::Shutdown();
  return 0;
}
```

## Service Architecture

```
┌─────────────────────────────────────┐
│           Clients                    │
│  ┌──────┐  ┌──────┐  ┌──────────┐   │
│  │ Game │  │  Web │  │  Mobile   │   │
│  │ (TCP)│  │(WS)  │  │  (WS)    │   │
│  └───┬──┘  └───┬──┘  └────┬─────┘   │
└──────┼─────────┼───────────┼─────────┘
       │         │           │
┌──────┴─────────┴───────────┴─────────┐
│           Gateway (5000/5001)       │
│  - Protocol routing                │
│  - Session management             │
└────┬────────┬────────┬────────────┘
     │        │        │
┌────┴──┐ ┌──┴──┐ ┌──┴─────┐
│ Auth │ │ Chat │ │ Social │
│ 6000 │ │ 7000 │ │ 8000  │
└──────┘ └──────┘ └────────┘
     │        │        │
┌────┴────────┴────────┴────┐
│  Redis + MySQL (optional) │
└────────────────────────────┘
```

## Basic Operations

| Service | Ports | Description |
|---------|-------|-------------|
| Gateway | 5000, 5001 | Protocol routing, session management |
| Auth | 6000 | JWT token verification, login/logout |
| Chat | 7000, 7001 | Real-time messaging, groups, read receipts |
| Social | 8000, 8001 | Friends, presence, blacklist |
| Voice | 9000, 9001 | Voice rooms, WebRTC signaling |

## Key Files

| Path | Description |
|------|-------------|
| `proto/*.proto` | Protocol definitions |
| `services/*/src/main.cc` | Service implementations |
| `sdks/core/` | C++ SDK |
| `apps/cli_client/` | Test client |
| `docker-compose.yml` | Docker orchestration |
| `scripts/build_and_test.sh` | Build script with vcpkg |

## Next Steps

1. Read [Architecture Overview](../docs/API.md)
2. Check [API Documentation](../docs/API.md)
3. See [Deployment Guide](../docs/DEPLOYMENT.md)
4. Run [Integration Tests](../tests/run_integration_tests.sh)

## Troubleshooting

### vcpkg not found
```bash
# Set VCPKG_ROOT environment variable
export VCPKG_ROOT=/path/to/vcpkg  # Linux/Mac
set VCPKG_ROOT=C:\path\to\vcpkg   # Windows
```

### Protobuf generation fails
```bash
# Install protobuf via vcpkg
/path/to/vcpkg/vcpkg install protobuf:x64-windows
```

### Build fails with protobuf errors
```bash
# Regenerate protobuf files using vcpkg's protoc
/vc/vcpkg/installed/x64-windows/tools/protobuf/protoc \
  --proto_path=. --cpp_out=proto/cpp \
  proto/*.proto
```

## Support

For issues and questions:
- GitHub: [repository-url]/issues
- Documentation: `docs/`
