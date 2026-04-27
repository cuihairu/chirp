---
home: true
title: 首页
heroImage: /logo.png
heroText: Chirp
tagline: 面向游戏开发的轻量聊天后端骨架
actions:
  - text: 快速开始 →
    link: /guide/getting-started
    type: primary
  - text: NPC 对话系统
    link: /npc_dialog_system
    type: secondary

features:
  - title: 高性能
    details: 超低延迟消息传递，单服务器支持 10k+ 并发连接。
  - title: 丰富通信
    details: 私聊、群组、频道、表情反应等游戏内通信所需的一切。
  - title: 游戏优先
    details: 专为游戏服务器集成设计，支持 TCP/WebSocket 双协议。
  - title: 多端登录
    details: 支持同一账号多设备登录，支持踢出（最后登录优先）。
  - title: 分布式会话
    details: 可选 Redis 实现分布式会话管理。
  - title: NPC 对话
    details: 支持 AI NPC 对话系统，包含人设、任务、指令响应。

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
