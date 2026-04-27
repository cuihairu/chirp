---
title: Introduction
---

# Introduction to Chirp

**Chirp** is a high-performance, real-time communication platform designed for gaming and interactive applications. It provides a complete solution for in-game chat, voice communication, social features, and more.

## Features

### 🎮 Gaming-First Design
- Ultra-low latency message delivery (< 50ms)
- Support for 10k+ concurrent connections per server
- Efficient binary protocol with protobuf
- Built with game server integration in mind

### 💬 Rich Communication
- **1v1 Private Chat**: Direct messaging between users
- **Group Chat**: Create and manage groups with up to 10,000 members
- **Channel System**: Discord-like text/voice channels with categories
- **Message Types**: Text, emoji, voice clips, images, system messages

### 🎤 Voice Communication
- **WebRTC-based Voice**: High-quality voice with adaptive bitrate
- **Voice Rooms**: P2P, group, and channel-based voice
- **Audio Features**: Mute, deafen, speaker control
- **Platform Support**: Cross-platform voice on desktop, mobile, web

### 👥 Social Features
- **Friends System**: Add, remove, block users
- **Presence**: Real-time online/offline/away status
- **Custom Status**: Set custom status messages and emoji
- **Typing Indicators**: Real-time typing feedback

### 🔔 Notifications
- **Push Notifications**: FCM (Android/Web) and APNs (iOS)
- **Smart Delivery**: Batched and rate-limited notifications
- **Mention Alerts**: @username, @role, @everyone support
- **Rich Notifications**: Image, action buttons, deep linking

### 🔍 Search
- **Full-Text Search**: Search across all messages
- **Filters**: By channel, user, date range, type
- **Snippets**: Highlighted search results with context
- **Autocomplete**: Query suggestions for faster searching

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                         Clients                             │
├─────────────────────────────────────────────────────────────┤
│  Unity  │  Unreal  │  Flutter  │  Web  │  Native (C++)     │
└─────────────────────────────────────────────────────────────┘
                              │
┌─────────────────────────────────────────────────────────────┐
│                      Gateway Service                         │
│  (TCP + WebSocket | Session Management | Rate Limiting)     │
└─────────────────────────────────────────────────────────────┘
                              │
        ┌─────────────────────┼─────────────────────┐
        │                     │                     │
┌──────────────┐    ┌──────────────┐    ┌──────────────┐
│ Auth Service │    │ Chat Service │    │ Social       │
│ (JWT/Redis)  │    │ (Messaging)  │    │ Service      │
└──────────────┘    └──────────────┘    │ (Friends)    │
        │                    │        └──────────────┘
        │             ┌──────┴──────┐
┌──────────────┐    ┌──────────────┐    ┌──────────────┐
│   Notif.     │    │   Message    │    │   Voice      │
│   Service    │    │   Store      │    │   Service    │
│  (FCM/APNs)  │    │ (Redis+MySQL)│    │  (WebRTC)    │
└──────────────┘    └──────────────┘    └──────────────┘
```

## Technology Stack

### Backend
- **Language**: C++20
- **Networking**: ASIO (Boost.Asio standalone)
- **Serialization**: Protocol Buffers
- **Storage**: Redis (caching), MySQL (persistence)
- **Build System**: CMake

### SDKs
- **Core SDK**: C++ with cross-platform support
- **Unity SDK**: C# wrapper with P/Invoke
- **Unreal SDK**: UPlugin with Blueprint support
- **Flutter SDK**: Dart with FFI to C++

### Infrastructure
- **Deployment**: Docker, Kubernetes
- **Monitoring**: Prometheus metrics
- **Load Balancing**: HAProxy/Nginx
- **Message Queue**: Redis Pub/Sub

## Use Cases

### 🎮 Gaming
- In-game chat with channels (team, guild, world)
- Voice chat for team coordination
- Friend system and social features
- Cross-platform play communication

### 🎪 Interactive Apps
- Real-time event chat
- Live Q&A sessions
- Audience engagement features
- Notification system

### 💼 Enterprise
- Secure team communication
- Custom presence status
- Message search and archiving
- Admin dashboard

## Getting Started

The quickest way to get started is to check out our [Quick Start Guide](./quickstart.md) or jump directly to [Installation](./installation.md).

## Documentation Index

| Section | Description |
|---------|-------------|
| [Getting Started](./getting-started.md) | Setup and basic usage |
| [Architecture](./architecture.md) | System design and components |
| [API Reference](../api/overview.md) | Protocol and service APIs |
| [SDK Guides](../sdk/overview.md) | Platform-specific SDK docs |
| [Deployment](./deployment.md) | Production deployment guide |

## License

Chirp is licensed under the MIT License. See [LICENSE](https://github.com/cuihairu/chirp/LICENSE) for details.

## Community

- **GitHub**: [https://github.com/cuihairu/chirp](https://github.com/cuihairu/chirp)
- **Issues**: [https://github.com/cuihairu/chirp/issues](https://github.com/cuihairu/chirp/issues)
- **Discussions**: [https://github.com/cuihairu/chirp/discussions](https://github.com/cuihairu/chirp/discussions)
