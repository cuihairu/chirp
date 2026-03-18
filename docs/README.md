---
home: true
title: Home
heroImage: /logo.png
heroText: Chirp Documentation
tagline: Real-time Communication Platform for Gaming and Interactive Applications
actions:
  - text: Get Started
    link: /guide/introduction.html
    type: primary
  - text: API Reference
    link: /api/overview.html
    type: secondary

features:
  - title: 🚀 High Performance
    details: Ultra-low latency message delivery with support for 10k+ concurrent connections per server.
  - title: 💬 Rich Communication
    details: Private chat, groups, channels, reactions, and more - everything you need for in-game communication.
  - title: 🎤 Voice Chat
    details: WebRTC-based voice with P2P, group rooms, and channel-based communication.
  - title: 👥 Social Features
    details: Friends, presence, custom status, and real-time updates.
  - title: 🔔 Notifications
    details: Push notifications with FCM and APNs support.
  - title: 🔍 Search
    details: Full-text message search with filters and snippets.

footer: MIT Licensed | Copyright © 2024-Present Chirp Project
---

## Quick Start

```bash
# Clone the repository
git clone https://github.com/cuihairu/chirp.git
cd chirp

# Install dependencies
./gen_proto.sh

# Build
mkdir build && cd build
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release ..
cmake --build . --parallel

# Run with Docker Compose
docker-compose up -d
```

## Documentation

- **[Getting Started](/guide/getting-started.html)**
- **[API Reference](/api/overview.html)**
- **[SDK Guides](/sdk/overview.html)**

## Services

| Service | Port | Description |
|---------|------|-------------|
| Gateway | 5000 (TCP), 5001 (WS) | Connection routing and session management |
| Auth | 6000 (TCP) | Authentication and session validation |
| Chat | 7000 (TCP), 7001 (WS) | Messaging and group chat |
| Social | 8000 (TCP), 8001 (WS) | Friends and presence |
| Voice | 9000 (TCP), 9001 (WS) | WebRTC signaling |
| Notification | 5006 | Push notification delivery |
| Search | 5007 | Full-text message search |

## SDKs

| Platform | SDK | Status |
|----------|-----|--------|
| C++ | Core SDK | ✅ Stable |
| Unity | C# Wrapper | ✅ Stable |
| Unreal | UPlugin | ✅ Stable |
| Flutter | Dart/FFI | ✅ Stable |

## License

MIT License - see [LICENSE](https://github.com/cuihairu/chirp/LICENSE) for details.
