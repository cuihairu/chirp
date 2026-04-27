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

## 文档导航

- **[指南](/guide/)** - 快速开始、安装、部署
- **[架构文档](/game_chat_architecture)** - 系统设计与组件化架构
- **[功能特性](/game_chat_features)** - 游戏聊天功能详解
- **[NPC 对话系统](/npc_dialog_system)** - AI NPC 对话完整设计
- **[API 参考](/api/overview)** - 协议与接口说明

## 快速开始

```bash
# 克隆仓库
git clone https://github.com/cuihairu/chirp.git
cd chirp

# 生成 Protobuf 文件
./gen_proto.sh

# 构建
cmake -S . -B build
cmake --build build -j

# Docker Compose 一键启动
docker compose up -d
```

## 核心服务

| 服务 | TCP 端口 | WebSocket 端口 | 说明 |
|-----|---------|---------------|------|
| Gateway | 5000 | 5001 | 连接路由与会话管理 |
| Auth | 6000 | - | 认证与会话验证 |
| Chat | 7000 | 7001 | 消息与群聊 |

## SDK 支持

| 平台 | SDK | 状态 |
|------|-----|------|
| C++ | Core SDK | ✅ 稳定 |
| Unity | C# Wrapper | 规划中 |
| Unreal | UPlugin | 规划中 |
| Flutter | Dart/FFI | 规划中 |

## 技术栈

- **语言**: C++20
- **网络**: ASIO (Boost.Asio standalone)
- **序列化**: Protocol Buffers
- **存储**: Redis (缓存)、MySQL (持久化)
- **构建**: CMake
- **AI**: LLM 驱动的 NPC 对话系统

## 协议

传输层支持 TCP 和 WebSocket，统一使用 Protobuf 编码：

```
┌───────────────┬───────────────┬───────────────────────┐
│   Length     │   MsgID      │   Protobuf Body       │
│  (4 bytes)   │  (2 bytes)   │   (variable)          │
└───────────────┴───────────────┴───────────────────────┘
```

## 许可证

MIT License - see [LICENSE](https://github.com/cuihairu/chirp/LICENSE) for details.
