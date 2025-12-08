# 游戏聊天服务架构设计 (Game Chat System Architecture)

## 1. 整体架构概览 (Overall System Architecture)

为了支持游戏与独立 App 共用逻辑，同时满足高并发、低延迟和多端同步的需求，本架构采用微服务分层设计。

### 1.1 客户端架构 (Client SDK Architecture)

客户端采用分层设计，以支持不同平台（Unity/Unreal/Flutter/React）复用核心逻辑。

```mermaid
graph TD
    UI[UI Layer (Unity/Unreal/Flutter/React)] --> Interface[Unified Interface (IChatService)]
    Interface --> ModuleMgr[Module Manager (Loader)]

    subgraph "Core SDK (C++)"
        ModuleMgr --> Net[Network Layer (TCP/KCP/WS)]
        ModuleMgr --> DB[Local Storage (SQLite)]

        subgraph "Modules"
            ChatMod[Chat Module]
            SocialMod[Social Module]
            VoiceMod[Voice Module]
        end

        ModuleMgr -.-> ChatMod
        ModuleMgr -.-> SocialMod
    end
```

- **UI Layer**: 负责展示，不同平台使用不同技术栈。
- **Unified Interface**: 提供统一 API (`Login`, `SendMessage`, `GetFriends`)。
- **Module Manager**: 根据 `ChatConfig` 动态加载所需模块 (Feature Flags)。

### 1.2 服务端架构 (Server-Side Microservices)

```mermaid
graph TD
    Client[Client (Game/App)] -- TCP/WS --> Gateway[Unified Gateway]

    Gateway --> SessionMgr[Session Manager (Redis)]
    Gateway --> Router[Msg Router (MQ)]

    Router --> ChatSvc[Chat Service]
    Router --> SocialSvc[Social/Relation Service]
    Router --> PresenceSvc[Presence Service]

    ChatSvc --> HotDB[(Redis: Cache)]
    ChatSvc --> ColdDB[(MySQL/Mongo: History)]

    PresenceSvc --> PubSub((Redis Pub/Sub))
```

- **Unified Gateway (接入层)**:
  - 同时支持 TCP (游戏客户端) 和 WebSocket (Web/小程序/独立 App)。
  - 维护连接状态，处理心跳。
- **Session Manager (会话管理)**:
  - 维护 `UserID` -> `GatewayID` 的映射。
  - 支持多端登录 (Multi-Device Login) 策略控制。
- **Chat Service (聊天服务)**:
  - 核心消息处理，存储历史消息，离线消息队列。
- **Presence Service (状态服务)**:
  - 专门处理高频的状态变化 (Online/In-Game)。
  - 使用 Redis Pub/Sub 实现轻量级状态广播。

---

## 2. 关键数据流 (Key Data Flows)

### 2.1 消息投递流程 (Message Delivery)

1.  **发送**: Client A -> Gateway -> Chat Service (Persist to DB).
2.  **路由**: Chat Service -> Session Manager (Query B's loc) -> Gateway B.
3.  **推送**: Gateway B -> Client B.
4.  **离线**: 若 B 不在线，写入 Offline Queue (Redis List).

### 2.2 好友状态同步 (Friend Presence Sync)

1.  **心跳/事件**: Client A 进入战斗 -> Gateway -> Presence Service.
2.  **发布**: Presence Service 更新 A 的状态 -> Publish to Channel `status:user:A`.
3.  **订阅**: Gateway (代表 A 的好友们) 订阅了 `status:user:A`。
4.  **广播**: Gateway 收到变动通知 -> 推送给订阅了 A 的在线好友 (Client B/C...)。

---

## 3. 通信协议选型 (Communication Protocol Selection)

### 3.1 协议对比 (Comparison)

| 特性 (Feature)           | TCP                 | WebSocket (WSS)        | UDP (KCP/QUIC)           | WebRTC (UDP)            |
| :----------------------- | :------------------ | :--------------------- | :----------------------- | :---------------------- |
| **有序性 (Ordering)**    | 严格有序 (Strict)   | 严格有序               | 可配置 (Configurable)    | **无需有序** (RTP 流)   |
| **可靠性 (Reliability)** | 100%                | 100%                   | 可配置 (Ack/Retry)       | **允许丢包** (FEC/PLC)  |
| **延迟 (Latency)**       | 中 (受拥塞控制影响) | 中 (基于 TCP)          | **低** (抗弱网能力强)    | **极低** (P2P/ICE)      |
| **穿透性 (NAT)**         | 一般                | **极好** (80/443 端口) | 较差 (部分网络 QoS 限制) | **极好** (ICE/TURN)     |
| **开发难度 (Dev)**       | 低 (成熟)           | 低 (Web 标准)          | 高 (需调优参数)          | **高** (协议栈复杂)     |
| **电量消耗 (Battery)**   | 低 (系统栈优化)     | 低                     | **高** (应用层重传)      | **中** (DSP 硬编解优化) |

### 3.2 推荐策略：混合协议 (Recommended: Hybrid Strategy)

考虑到 **多端共用** 和 **场景差异**，推荐采用 **"接入层协议适配"** 方案：

1.  **游戏客户端 (Game Client)**: 推荐 **TCP (Protobuf)**

    - _理由_: 聊天对延迟不极其敏感（相比射击移动），TCP 足够稳定、省电，且无需处理丢包乱序逻辑。
    - _例外_: 若游戏本身用 KCP 做战斗同步，可复用 KCP 通道顺带发聊天消息（节省连接开销）。

2.  **Web / 小程序 (Web/Mini-App)**: 必须 **WebSocket (WSS)**

    - _理由_: 浏览器环境唯一选择，防火墙穿透性最好。

3.  **独立 App (Mobile Companion)**: 推荐 **WebSocket** 或 **TCP**

    - _理由_: 现代移动端开发（Flutter/React Native）对 WebSocket 支持极好，且更容易保持后台长连接（配合系统推送）。

4.  **实时语音 (Real-time Voice)**: 必须 **WebRTC (UDP)**
    - _理由_: 行业标准。内置 FEC (前向纠错)、PLC (丢包隐藏) 和 AGC (自动增益)，抗弱网能力极强。
    - _实现_: 可集成 Google WebRTC 源码或使用第三方 SDK (Agora/GME)。

### 3.3 数据负载 (Payload)

- **协议格式**: **Protobuf (Google Protocol Buffers)**
  - _优势_: 二进制流，体积极小（比 JSON 小 50%+），解析快，向后兼容性好，适合移动流量环境。
