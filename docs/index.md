---
home: true
title: 首页
heroImage: /logo.png
heroText: Chirp
tagline: 面向游戏开发的轻量实时通信后端骨架
actions:
  - text: 核心说明
    link: /CORE
    type: primary
  - text: 快速开始
    link: /guide/getting-started
    type: secondary

features:
  - title: 核心链路
    details: 当前建议优先验证 gateway + auth + chat。
  - title: 双协议接入
    details: TCP 和 WebSocket 使用同一套长度前缀 Protobuf Packet 协议。
  - title: 可选 Redis 增强
    details: Redis 可用于 Gateway 分布式 session、跨实例 kick、聊天历史和离线队列。
  - title: 明确能力边界
    details: 社交、语音、通知、搜索、SDK 和应用侧代码存在，但完成度不一致。

footer: MIT Licensed | Copyright © 2024-Present Chirp Project
---

## 当前定位

Chirp 目前是“可运行的核心通信骨架 + 一批实验性扩展”，不是所有目录都同等成熟的完整产品套件。

优先阅读：

- [核心说明](/CORE)：当前可用链路、协议、服务边界和本地验证命令
- [能力矩阵](/CAPABILITY_MATRIX)：各服务、SDK、应用的真实完成度
- [导言](/guide/introduction)：文档定位、当前支持路径和边界说明
- [安装](/guide/installation)：开发环境和依赖准备
- [快速开始](/guide/getting-started)：本地构建、Docker Compose 和 smoke test
- [API 概述](/api/overview)：Packet envelope、消息 ID 和核心流程
- [整体架构](/architecture)：服务拓扑、数据流和架构判断

## 文档分层

| 层级 | 代表页面 | 用途 |
| --- | --- | --- |
| 当前运行时 | `CORE`, `CAPABILITY_MATRIX`, `guide/getting-started`, `api/overview`, `architecture` | 当前可用链路、协议和边界约束 |
| 入门指南 | `guide/introduction`, `guide/installation`, `guide/getting-started`, `guide/deployment` | 环境准备、本地构建和验证 |
| 专题 / 历史 | `DEPLOYMENT`, `DISTRIBUTED_DEPLOYMENT`, `SCALABILITY*`, `game_chat_*`, `npc_dialog_system`, `INTEGRATION_TEST_FIXES` | 设计记录、演进方案和旧版说明 |
| 兼容页 | `API`, `QUICKSTART` | 兼容入口，优先跳转到维护中的页面 |

## 核心服务

| 服务 | 默认端口 | 当前状态 | 说明 |
| --- | --- | --- | --- |
| Gateway | TCP 5000 / WS 5001 | Supported | 登录、登出、心跳、会话绑定、可选 Redis 跨实例 kick |
| Auth | TCP 6000 | Supported | 基础 token flow；依赖满足时构建增强认证实现 |
| Chat | TCP 7000 / WS 7001 | Supported | 私聊、历史、离线队列；依赖满足时构建增强存储路径 |

## 关键边界

当前架构对“游戏聊天后端骨架、本地验证、协议接入和二次开发”是合理的。它还不是生产级统一通信平台：`gateway` 尚未转发聊天等业务包，`chat` 仍是独立接入口，部分高级能力缺少统一会话和验证链路。
