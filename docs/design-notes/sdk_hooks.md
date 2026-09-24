---
title: SDK 钩子接口设计
---

# SDK 钩子接口设计

最后更新：2026-09-21

本文档定义游戏 SDK 的可扩展钩子接口。所有引擎 SDK 实现相同的接口，按语言惯用方式命名。

## 设计原则

1. **SDK 定义接口，游戏实现行为**。SDK 不预设游戏的具体逻辑（敏感词、UI、音效），只提供插入点。
2. **所有回调在 SDK 内部 io 线程触发**。引擎适配层（Unity ChirpManager、Unreal AsyncTask）负责派发回游戏线程。
3. **钩子可选**。不设置钩子时 SDK 行为不变（默认放行、默认不存储）。
4. **接口数量克制**。只在消息生命周期、连接生命周期、命令扩展三个维度提供钩子。

## 消息生命周期钩子

```
玩家输入消息
  → OnBeforeSend（可修改/拒绝）
  → 发送到服务器
  → OnAfterSend（统计/日志）

服务器推送消息
  → OnBeforeReceive（可修改/丢弃）
  → OnAfterReceive（音效/振动/通知）
  → 渲染展示
```

### MessageInterceptor

在消息发送前/接收后插入自定义逻辑。

| 回调 | 触发时机 | 可做什么 | 返回值 |
|---|---|---|---|
| `OnBeforeSend(msg)` | 消息发送前 | 修改内容、添加 metadata、敏感词过滤 | `false` = 阻止发送 |
| `OnAfterSend(msg)` | 消息已发送 | 统计、日志、发送特效 | 无 |
| `OnBeforeReceive(msg)` | 消息接收前 | 修改内容、丢弃垃圾消息 | `false` = 丢弃消息 |
| `OnAfterReceive(msg)` | 消息即将展示 | 音效、振动、桌面通知、未读计数 | 无 |

**C++ 定义**：`sdks/core/include/chirp/message_interceptor.h`

**游戏用法示例**：

```cpp
// 敏感词过滤 + VIP 消息颜色
class GameMessageInterceptor : public chirp::sdk::MessageInterceptor {
 public:
  bool OnBeforeSend(chirp::chat::SendMessageRequest& msg) override {
    // 敏感词过滤
    if (filter_->ContainsBadWord(msg.content())) {
      return false;  // 阻止发送
    }
    // VIP 消息带颜色
    if (player_->IsVip()) {
      (*msg.mutable_metadata())["color"] = "#FFD700";
    }
    return true;
  }

  void OnAfterReceive(const chirp::chat::ChatMessage& msg) override {
    // 根据消息类型播放不同音效
    switch (msg.msg_type()) {
      case chirp::chat::TEXT:
        audio_->Play("chat_message");
        break;
      case chirp::chat::ITEM_LINK:
        audio_->Play("item_drop");
        break;
      case chirp::chat::ACHIEVEMENT:
        audio_->Play("achievement");
        break;
    }
  }
};
```

## 连接生命周期钩子

```
Connect → Connecting → Connected
  → Login → LoggedIn
  → 运行中（心跳、收发消息）
  → 断线 → WaitingReconnect → Reconnecting → Reconnected
  → 被踢 → Kicked（终态）
```

### ChatEventListener

SDK 在关键生命周期节点通知游戏。

| 回调 | 触发时机 | 典型用途 |
|---|---|---|
| `OnConnectionStateChanged(state)` | 连接状态变化 | UI 状态指示器 |
| `OnLoginResult(code, user_id)` | 登录结果 | 成功→显示聊天 UI；失败→提示 |
| `OnKicked(reason)` | 被踢下线 | 弹窗提示原因，引导重新登录 |
| `OnReconnecting(attempt, delay_ms)` | 重连中 | 显示"重连中..."提示 |
| `OnReconnected()` | 重连成功 | 恢复 UI，刷新未读 |
| `OnMessageReceived(msg)` | 收到新消息 | 更新聊天窗口 |
| `OnUnreadChanged(type, channel_id, count)` | 未读数变化 | 更新 badge |
| `OnPresenceChanged(user_id, status)` | 好友在线状态变化 | 更新好友列表 |
| `OnTypingIndicator(user_id, channel_id, is_typing)` | 正在输入 | 显示"对方正在输入..." |
| `OnMarqueeMessage(msg, ttl_seconds)` | 走马灯消息 | 屏幕顶部滚动 |
| `OnSystemAnnouncement(msg)` | 系统公告 | 弹窗或高亮展示 |

**C++ 定义**：`sdks/core/include/chirp/chat_event_listener.h`

**游戏用法示例**：

```cpp
class GameChatListener : public chirp::sdk::ChatEventListener {
 public:
  void OnLoginResult(int code, const std::string& user_id) override {
    if (code == 0) {
      ui_->ShowChatWindow();
    } else {
      ui_->ShowError("登录失败: " + std::to_string(code));
    }
  }

  void OnKicked(const std::string& reason) override {
    ui_->ShowKickDialog(reason);
  }

  void OnMessageReceived(const chirp::chat::ChatMessage& msg) override {
    ui_->AppendToChat(msg);
  }

  void OnMarqueeMessage(const chirp::chat::ChatMessage& msg,
                        int ttl_seconds) override {
    ui_->ShowMarquee(msg.content(), ttl_seconds);
  }
};
```

## 认证钩子

### AuthProvider

游戏登录系统与 SDK 对接。

| 回调 | 触发时机 | 说明 |
|---|---|---|
| `GetToken()` | SDK 需要 token 时 | 返回游戏登录系统签发的 token |
| `OnTokenExpired(renew)` | token 过期时 | 游戏刷新 token 后调用 `renew(new_token)` |
| `OnAuthResult(code, user_id)` | 认证结果 | 通知游戏认证成功/失败 |

**C++ 定义**：`sdks/core/include/chirp/auth_provider.h`

**游戏用法示例**：

```cpp
class GameAuthProvider : public chirp::sdk::AuthProvider {
 public:
  GameAuthProvider(GameLogin* login) : login_(login) {}

  std::string GetToken() override {
    return login_->GetCurrentToken();
  }

  void OnTokenExpired(
      std::function<void(const std::string&)> renew) override {
    login_->RefreshToken([renew](const std::string& new_token) {
      renew(new_token);
    });
  }

 private:
  GameLogin* login_;
};
```

## 命令扩展钩子

### CommandHandler

游戏注册自定义聊天命令。

| 方法 | 说明 |
|---|---|
| `GetName()` | 命令名（不含斜杠），如 "trade" |
| `GetDescription()` | 命令描述，用于 /help |
| `GetUsage()` | 用法示例，如 "/trade <玩家名>" |
| `Execute(args, sender_id)` | 执行命令，返回 `true` = 已处理 |

**C++ 定义**：`sdks/core/include/chirp/command_handler.h`

**游戏用法示例**：

```cpp
class TradeCommand : public chirp::sdk::CommandHandler {
 public:
  std::string GetName() const override { return "trade"; }
  std::string GetDescription() const override {
    return "发起交易";
  }
  std::string GetUsage() const override { return "/trade <玩家名>"; }

  bool Execute(const std::string& args,
               const std::string& sender_id) override {
    auto target = FindPlayer(args);
    if (target) {
      trade_->Invite(target->id());
      return true;
    }
    return false;
  }
};

class InviteCommand : public chirp::sdk::CommandHandler {
 public:
  std::string GetName() const override { return "invite"; }
  std::string GetDescription() const override {
    return "邀请组队";
  }

  bool Execute(const std::string& args,
               const std::string& sender_id) override {
    party_->Invite(FindPlayer(args)->id());
    return true;
  }
};

// 注册
sdk.RegisterCommand(std::make_unique<TradeCommand>());
sdk.RegisterCommand(std::make_unique<InviteCommand>());
```

**命令解析流程**：

```
玩家输入: "/trade PlayerA 100 gold"
  → SDK 解析: command="trade", args="PlayerA 100 gold"
  → 遍历已注册 CommandHandler
  → 找到 TradeCommand.Execute("PlayerA 100 gold", sender_id)
  → 如果所有 handler 都返回 false → 提示"未知命令"
```

## 消息存储钩子

### MessageStore

游戏自定义消息的本地缓存策略。

| 方法 | 说明 |
|---|---|
| `Save(msg)` | 存储一条消息 |
| `Load(type, channel_id, limit, before_timestamp)` | 加载历史消息 |
| `MarkRead(type, channel_id, message_id)` | 标记已读 |
| `GetUnreadCount(type, channel_id)` | 获取未读数 |
| `Cleanup(older_than)` | 清理过期消息 |

**C++ 定义**：`sdks/core/include/chirp/message_store.h`

**内置实现**：`MemoryMessageStore`（内存缓存，轻量级游戏用）、`FileMessageStore`（文件持久化，`chirp/file_message_store.h`，header-only；append-only 日志 + 已读游标 + `Compact()` 原子重写，与 C# SDK 同一文件格式）

**游戏用法示例**：

```cpp
// 轻量级：使用默认内存缓存
sdk.SetMessageStore(std::make_unique<chirp::sdk::MemoryMessageStore>(200));

// 跨会话：文件持久化存档（已读游标一并落盘）
sdk.SetMessageStore(std::make_unique<chirp::sdk::FileMessageStore>(
    chirp::sdk::FileMessageStore::Options{.path = "chat_archive.log",
                                          .max_per_channel = 200}));

// 重度游戏：SQLite 持久化
class SqliteMessageStore : public chirp::sdk::MessageStore {
 public:
  SqliteMessageStore(const std::string& db_path) {
    sqlite3_open(db_path.c_str(), &db_);
    // 建表...
  }

  void Save(const chirp::chat::ChatMessage& msg) override {
    // INSERT INTO messages ...
  }

  std::vector<chirp::chat::ChatMessage> Load(...) override {
    // SELECT * FROM messages WHERE ...
  }
};
```

## 各语言接口映射

| 钩子 | C++ | C# | TypeScript | GDScript | Dart |
|---|---|---|---|---|---|
| 消息拦截 | `MessageInterceptor` 虚基类 | `IMessageInterceptor` interface | `MessageInterceptor` interface | signal + callback | `MessageInterceptor` abstract class |
| 事件监听 | `ChatEventListener` 虚基类 | `IChatEventListener` interface | `ChatEventListener` interface | signal | `ChatEventListener` abstract class |
| 认证 | `AuthProvider` 虚基类 | `IAuthProvider` interface | `AuthProvider` interface | callback | `AuthProvider` abstract class |
| 命令 | `CommandHandler` 虚基类 | `ICommandHandler` interface | `CommandHandler` interface | signal | `CommandHandler` abstract class |
| 存储 | `MessageStore` 虚基类 | `IMessageStore` interface | `MessageStore` interface | Resource | `MessageStore` abstract class |

> C#/TypeScript/Dart 列均已落地（GDScript 列为计划形态，Godot 走 C# 复用路线，见 `docs/sdk/godot.md`）。跨语言内置实现还包括四语言的 `WordFilterInterceptor`（敏感词预检，语义对齐服务端，见 `docs/design-notes/sdk_compatibility.md`）。

## SDK 注册入口

所有钩子通过 SDK 实例注册，必须在 `Connect` 之前调用：

```cpp
chirp::sdk::ChatClient sdk(config);

// 注册钩子（均可选）
sdk.SetAuthProvider(std::make_unique<GameAuthProvider>());
sdk.SetMessageInterceptor(std::make_unique<GameMessageInterceptor>());
sdk.SetMessageStore(std::make_unique<MemoryMessageStore>());
sdk.AddListener(std::make_shared<GameChatListener>());
sdk.RegisterCommand(std::make_unique<TradeCommand>());
sdk.RegisterCommand(std::make_unique<InviteCommand>());

// 连接并登录
sdk.Connect();
sdk.Login(token, [](auto ec, const auto& user_id) { ... });
```
