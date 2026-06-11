---
home: true
title: 首页
heroImage: /logo.png
heroText: Chirp
tagline: 面向游戏开发的轻量实时通信后端骨架
actions:
  - text: 整体架构
    link: /architecture
    type: primary
  - text: 快速开始
    link: /guide/getting-started
    type: secondary

features:
  - title: 核心链路
    details: 当前建议优先验证 gateway + auth + chat 三个核心服务。
  - title: 双协议接入
    details: TCP 和 WebSocket 使用同一套长度前缀 Protobuf Packet 协议。
  - title: Redis 扩展
    details: Redis 可用于 Gateway 分布式 session、跨实例 kick、聊天历史和离线队列。
  - title: 渐进演进
    details: 社交、语音、通知、搜索、SDK 和应用侧代码已在仓库中，但完成度不一致。

footer: MIT Licensed | Copyright © 2024-Present Chirp Project
---

## 当前定位

Chirp 目前应被理解为一个“可运行的核心通信骨架 + 一批实验性扩展”，而不是所有目录都同等成熟的完整产品套件。

优先阅读：

- [整体架构](/architecture)：当前服务边界、协议、数据流和架构合理性判断
- [能力矩阵](/CAPABILITY_MATRIX)：各服务、SDK、应用的真实完成度
- [快速开始](/guide/getting-started)：本地构建和 smoke test
- [API 概述](/api/overview)：当前协议 envelope 和核心消息

## 核心服务

| 服务 | 默认端口 | 当前状态 | 说明 |
| --- | --- | --- | --- |
| Gateway | TCP 5000 / WS 5001 | Supported | 登录、登出、心跳、会话绑定、可选 Redis 跨实例 kick |
| Auth | TCP 6000 | Supported | 基础 token flow；依赖满足时构建增强认证实现 |
| Chat | TCP 7000 / WS 7001 | Supported | 私聊、历史、离线队列；依赖满足时构建增强存储路径 |

实验性模块：

| 模块 | 默认端口 | 状态 |
| --- | --- | --- |
| Social | TCP 8000 / WS 8001 | Experimental |
| Voice | TCP 9000 / WS 9001 | Experimental |
| Notification | 5006 | Experimental |
| Search | 5007 | Experimental |
| SDKs / mobile / admin | varies | Experimental / Demo / Stub |

## 架构判断

当前架构对“游戏聊天后端骨架、本地验证、协议接入和二次开发”是合理的。它还不应该被描述为生产级统一通信平台，因为 `gateway` 尚未转发聊天等业务包，`chat` 仍是独立接入口，部分高级能力缺少统一会话和验证链路。
