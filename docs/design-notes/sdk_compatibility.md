---
title: SDK 引擎兼容性
---

# SDK 引擎兼容性

最后更新：2026-09-21

## 总览

| 引擎 | 语言 | SDK 目录 | 最低版本 | 优先级 | 状态 |
|---|---|---|---|---|---|
| Unity | C# | `sdks/unity/` | 2021.2 LTS | P0 | 已有基础 |
| Unreal Engine 5 | C++ | `sdks/unreal/` | 5.1 | P0 | 已有基础 |
| LayaBox | TypeScript | `sdks/laya/` | LayaAir 3.0 | P1 | 待开发 |
| Godot | GDScript / C# | `sdks/godot/` | 4.0 | P2 | 待开发 |
| Cocos Creator | TypeScript | `sdks/cocos/` | 3.8 | P2 | 待开发 |
| Web | TypeScript | `apps/web_companion/` | — | P0 | 已有基础 |
| Flutter | Dart | `apps/mobile_companion/` | 3.0 | P1 | 已有基础 |
| C++ 桌面 | C++ | `sdks/core/` | C++17 | P0 | 已有基础 |
| Go 服务端 | Go | `sdks/go/` | 1.21 | P0 | 已有基础 |

## 各引擎详细要求

### Unity（P0 优先级）

| 项目 | 要求 |
|---|---|
| 最低版本 | Unity 2021.2 LTS（C# 9 / netstandard2.1） |
| 推荐版本 | Unity 2022.3 LTS 或 Unity 6000.x |
| .NET 兼容 | .NET Standard 2.1 |
| Protobuf | Google.Protobuf 3.27.x（Unity 发布包） |
| 传输层 | ClientWebSocket（内建） |
| 线程模型 | ChirpManager MonoBehaviour 主线程派发 |
| 包格式 | Unity Package（UPM）或直接拷贝 Assets/ |

已有：
- `ChirpClient` 协议核心（纯 C#，无 UnityEngine 依赖）
- `ChirpManager` MonoBehaviour 薄壳
- `dotnet/` 纯 .NET 测试工程
- `proto/csharp/` 生成代码

待补：
- [x] Hook 接口（MessageInterceptor/AuthProvider/MessageStore/ChatEventListener/CommandHandler 五件套）——C++ core(2026-09)与 .NET/Unity(2026-09,`ChirpHooks.cs`,interface + 默认方法)均已对齐
- [ ] 历史消息本地存储（SQLite）
- [ ] 敏感词过滤客户端预检
- [ ] Unity Package 发布配置

### Unreal Engine 5（P0 优先级）

| 项目 | 要求 |
|---|---|
| 最低版本 | UE 5.1 |
| 推荐版本 | UE 5.4+ |
| C++ 标准 | C++20（UE5 默认） |
| 传输层 | 原生 TCP（chirp core SDK） |
| 线程模型 | AsyncTask(ENamedThreads::GameThread) 派发 |
| 包格式 | UE Plugin（.uplugin） |

已有：
- `UChirpClientSubsystem`（GameInstance 子系统）
- Blueprint 事件（login result、chat message、kick、disconnect）
- 连接状态枚举
- native 核心编译脚本

待补：
- [ ] Hook 接口（C++ 虚基类 + Blueprint 可绑定事件）
- [ ] UMG 聊天 UI 组件（可选）
- [ ] 物品链接/成就分享的 Blueprint 可渲染数据结构
- [ ] UE 5.4+ 验证

### LayaBox（P1 优先级）

| 项目 | 要求 |
|---|---|
| 最低版本 | LayaAir 3.0 |
| 推荐版本 | LayaAir 3.x 最新 |
| 语言 | TypeScript |
| 传输层 | WebSocket |
| 运行时 | 浏览器 / 微信小游戏 / APP |
| Protobuf | protobuf.js（运行时）或 ts-proto（编译时） |

待开发：
- [ ] TypeScript 协议核心（ChirpClient，与 web_companion 共享传输层逻辑）
- [ ] WebSocket 传输适配
- [ ] 微信小游戏平台适配（wx.connectSocket）
- [ ] Hook 接口（TypeScript interface）
- [ ] 示例项目

### Godot（P2 优先级）

| 项目 | 要求 |
|---|---|
| 最低版本 | Godot 4.0 |
| 推荐版本 | Godot 4.3+ |
| 语言 | GDScript 或 C# |
| 传输层 | StreamPeerTCP / WebSocketPeer |
| Protobuf | GDScript protobuf 或 C# protobuf |

待开发：
- [ ] GDScript 协议核心（或 C# 复用 Unity SDK 的纯 C# 部分）
- [ ] Godot 节点封装（ChatClient node）
- [ ] Hook 接口（GDScript signal / C# event）
- [ ] 示例项目

### Cocos Creator（P2 优先级）

| 项目 | 要求 |
|---|---|
| 最低版本 | Cocos Creator 3.8 |
| 推荐版本 | 3.8.x LTS |
| 语言 | TypeScript |
| 传输层 | WebSocket（原生 / 小游戏适配层） |
| 运行时 | Web / iOS / Android / 微信小游戏 |
| Protobuf | protobuf.js 或 ts-proto |

待开发：
- [ ] TypeScript 协议核心（可与 LayaBox SDK 共享传输层）
- [ ] Cocos 组件封装
- [ ] 微信小游戏平台适配
- [ ] Hook 接口

### Web（P0，已有基础）

| 项目 | 要求 |
|---|---|
| 运行时 | 现代浏览器（Chrome 90+、Firefox 90+、Safari 15+、Edge 90+） |
| 语言 | TypeScript（strict） |
| 传输层 | WebSocket |
| 框架 | React 18（web_companion 示例） |
| 构建 | Vite |

已有：
- `ChirpClient` 协议核心（TypeScript）
- WebSocket 传输
- React 组件示例

待补：
- [ ] 独立 npm 包发布
- [ ] Hook 接口（TypeScript interface）
- [ ] 框架无关的核心包（不依赖 React）

### Flutter（P1，已有基础）

| 项目 | 要求 |
|---|---|
| 最低版本 | Flutter 3.0 / Dart 3.0 |
| 推荐版本 | Flutter 3.22+ |
| 语言 | Dart |
| 传输层 | WebSocket（dart:io） |
| 平台 | iOS / Android / Linux / Windows / macOS |

已有：
- `ChirpClient` 协议核心（纯 Dart）
- Store + API 层
- 聊天 UI

待补：
- [ ] Hook 接口（Dart abstract class）
- [ ] 独立 pub 包发布
- [ ] APNs/FCM 推送集成

### C++ 桌面（P0，已有基础）

| 项目 | 要求 |
|---|---|
| C++ 标准 | C++17（最低），C++20（推荐） |
| 传输层 | TCP（ASIO） |
| 依赖 | ASIO（standalone）、Protobuf |
| 平台 | Linux、Windows、macOS |

已有：
- `ChatClient` 完整实现
- 5 个 Hook 接口（MessageInterceptor/AuthProvider/MessageStore/ChatEventListener/CommandHandler）已接线（2026-09）
- 27 个便捷 API（发送扩展/服务端历史/已读未读/黑名单/静音/输入状态/编辑删除/表情回执/批量删除/@提及/群组全套，2026-09）
- 106 个单测（状态机/loopback/钩子接线/便捷 API 往返）
- `sdk_example` 示例

### Go 服务端（P0，已有基础）

| 项目 | 要求 |
|---|---|
| Go 版本 | 1.21+ |
| 传输层 | TCP（出站连接到 game_server_gateway） |
| 依赖 | google.golang.org/protobuf |

已有：
- 完整的 server plane 客户端
- inject / event / identity / subscription / unread RPC
- 12 个 race-enabled 单测

待补：
- [ ] 对齐新的 proto 包名（game_server_gateway）

## Hook 接口统一设计

所有 SDK 实现相同的钩子接口（按语言惯用方式）：

| 钩子 | C++ | C# | TypeScript | GDScript | Dart |
|---|---|---|---|---|---|
| 消息拦截 | `MessageInterceptor`（虚基类） | `IMessageInterceptor`（interface） | `MessageInterceptor`（interface） | signal + callback | `MessageInterceptor`（abstract class） |
| 认证提供 | `AuthProvider`（虚基类） | `IAuthProvider`（interface） | `AuthProvider`（interface） | callback | `AuthProvider`（abstract class） |
| 命令处理 | `CommandHandler`（虚基类） | `ICommandHandler`（interface） | `CommandHandler`（interface） | signal | `CommandHandler`（abstract class） |
| 消息存储 | `MessageStore`（虚基类） | `IMessageStore`（interface） | `MessageStore`（interface） | Resource | `MessageStore`（abstract class） |
| 事件监听 | `ChatEventListener`（虚基类） | `IChatEventListener`（interface） | `ChatEventListener`（interface） | signal | `ChatEventListener`（abstract class） |
| 消息渲染 | `MessageRenderer`（虚基类） | `IMessageRenderer`（interface） | `MessageRenderer`（interface） | signal | `MessageRenderer`（abstract class） |

接口语义完全一致，只是按语言习惯调整命名和调用方式。详见 `sdks/core/include/chirp/` 下的 C++ 头文件定义。

## 实现优先级

### 第一批（P0，与游戏平面同步）

1. **C++ core SDK**：Hook 接口 + 命令系统（其他 SDK 的参考实现）
2. **Unity SDK**：Hook 接口 + 历史存储
3. **Unreal SDK**：Hook 接口 + Blueprint 事件

### 第二批（P1，App 平面启动后）

4. **TypeScript 核心包**：独立于 web_companion，LayaBox/Cocos/Web 共用
5. **LayaBox SDK**：TypeScript 核心 + Laya 适配层
6. **Flutter SDK**：Hook 接口 + pub 包

### 第三批（P2，按需）

7. **Godot SDK**：GDScript 或 C# 复用
8. **Cocos Creator SDK**：TypeScript 核心 + Cocos 适配层
