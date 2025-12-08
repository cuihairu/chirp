# 游戏聊天服务组件化架构 (Modular Game Chat Architecture)

## 1. 设计理念 (Design Philosophy)

为了适应不同终端（PC/Mobile/Web/Watch）和不同场景（激烈战斗/休闲大厅/独立 App）的需求，聊天服务不再是一个单体功能块，而是 **可配置、可组合的组件集合 (Composable Feature Modules)**。

客户端通过 **Feature Flags (特性开关)** 或 **Module Loader (模块加载器)** 按需初始化功能。

---

## 2. 功能组件拆解 (Feature Modules)

### 2.1 核心通信组件 (Core Network Module) - **[REQUIRED]**

_基础连接与消息收发，所有场景必须包含。_

- **Connection Manager**: 长连接维护 (TCP/WS/KCP)，断线重连，心跳保活。
- **Protocol Encoder**: Protobuf 序列化/反序列化。
- **Auth Service**: 登录鉴权，Token 刷新。
- **Basic Messaging**: 文本发送、接收，频道订阅 (Join/Leave)。

### 2.2 基础聊天组件 (Basic Chat Module) - **[RECOMMENDED]**

_标准的聊天室功能。_

- **Channel Manager**: 频道切换 (World, Guild, Team)。
- **Message History**: 历史消息拉取 (Local DB / Server Fetch)。
- **Local Storage**: 离线消息缓存 (SQLite/Realm)。
- **System Notice**: 跑马灯，系统公告展示。

### 2.3 社交关系组件 (Social Relation Module)

_好友与玩家关系管理。_

- **Friend List**: 好友增删查。
- **Presence Sync**:
  - _Lite_: 仅在线/离线 (适合战斗界面)。
  - _Full_: 详细状态 (地图/模式/英雄)，支持独立 App 订阅。
- **Blocklist**: 黑名单管理。
- **UserProfile**: 查看玩家卡片信息。

### 2.4 互动与增强组件 (Interaction Module)

_增强社交互动的可选功能。_

- **Typing Status**: "对方正在输入..." (仅 1v1 私聊需要，战斗不需要)。
- **Read Receipts**: 已读/送达回执 (消耗流量，适合独立社交 App)。
- **Gifting**: 礼物赠送逻辑与特效触发。
- **Red Packet**: 红包逻辑。

### 2.5 富媒体组件 (Rich Media Module)

_复杂的展示层功能，通常依赖较大的 UI 库。_

- **Rich Text Parser**: 解析颜色、超链接、图文混排。
- **Emoji/Sticker**: 表情包管理与渲染。
- **Voice Service**: 语音录制 (PTT)、实时语音 (WebRTC RTC)、语音转文字 (STT)。
- **Translation**: 实时多语言翻译。

---

## 3. 场景化组合策略 (Scenario Composition)

通过组合上述组件，满足特定场景需求，优化性能与体验。

### 场景 A：战斗局内 (In-Game Combat)

_目标：极简，低延迟，不遮挡视线，零干扰。_

- **包含组件**:
  - `Core Network`
  - `Basic Chat` (仅 Team 频道, 限制历史消息数量)
  - `Voice Service` (仅 RTC 实时语音, 无转文字)
- **剔除组件**:
  - `Social Relation` (战斗中一般不加好友)
  - `Interaction` (无需输入状态/回执)
  - `Rich Media` (仅基础文本，无大图/超链)

### 场景 B：游戏大厅 / 休闲区 (Lobby / Social Zone)

_目标：社交最大化，展示丰富。_

- **包含组件**:
  - `All Modules` (全功能开启)
- **特性配置**:
  - 开启 `Presence Sync (Full)`
  - 开启 `Gifting`, `Red Packet`
  - 开启 `Rich Text`, `Sticker`

### 场景 C：独立伴侣 App (Companion App / Standalone)

_目标：信息同步，轻量级，节能。_

- **包含组件**:
  - `Core Network` (使用 WebSocket, 适应弱网)
  - `Basic Chat` (全频道同步)
  - `Social Relation` (重点功能)
  - `Interaction` (支持输入状态/回执，体验接近 IM 软件)
- **剔除组件**:
  - `Voice Service` (通常不需要实时语音，或者仅保留 PTT)
  - `Red Packet` (视平台合规性而定)

---

## 4. 客户端架构示意 (Client Architecture Idea)

```cpp
// 伪代码示例：基于组合的初始化

struct ChatConfig {
    bool enableVoice = false;
    bool enableRichText = false;
    bool enableSocialPresence = false;
    // ...
};

class GameChatClient {
public:
    void Init(ChatConfig config) {
        // 核心模块总是加载
        loadModule(new CoreNetworkModule());

        // 按需加载
        if (config.enableSocialPresence) {
            loadModule(new SocialModule(SocialLevel::Full));
        }

        if (config.enableVoice) {
            loadModule(new VoiceModule()); // 可能会加载庞大的音频库
        }

        // ...
    }
};
```

## 5. 整体架构设计 (Overall System Architecture)

详情请见独立文档：[game_chat_architecture.md](./game_chat_architecture.md)
