> 🌐 Language: [中文](../zh/design-notes/sdk_hooks.md)) | **English** (this page)

# SDK Hook Interface Design

Last updated: 2026-09-21

This document defines the extensible hook interfaces of the game SDK. All engine SDKs implement the same interfaces, named in each language's idiomatic style.

## Design principles

1. **The SDK defines the interfaces; the game implements the behavior.** The SDK does not prescribe game-specific logic (word filtering, UI, sound effects) — it only provides insertion points.
2. **All callbacks fire on the SDK's internal io thread.** The engine adapter layer (Unity ChirpManager, Unreal AsyncTask) is responsible for dispatching back to the game thread.
3. **Hooks are optional.** When no hook is set, SDK behavior is unchanged (allow by default, no storage by default).
4. **The interface count is kept restrained.** Hooks are provided only in three dimensions: message lifecycle, connection lifecycle, and command extension.

## Message lifecycle hooks

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

Insert custom logic before a message is sent / after it is received.

| Callback | When it fires | What it can do | Return value |
|---|---|---|---|
| `OnBeforeSend(msg)` | before the message is sent | modify content, add metadata, word filtering | `false` = block the send |
| `OnAfterSend(msg)` | after the message is sent | statistics, logging, send effects | none |
| `OnBeforeReceive(msg)` | before the message is received | modify content, drop spam messages | `false` = drop the message |
| `OnAfterReceive(msg)` | just before the message is displayed | sound effects, vibration, desktop notification, unread count | none |

**C++ definition**: `sdks/core/include/chirp/message_interceptor.h`

**Game usage example**:

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

## Connection lifecycle hooks

```
Connect → Connecting → Connected
  → Login → LoggedIn
  → 运行中（心跳、收发消息）
  → 断线 → WaitingReconnect → Reconnecting → Reconnected
  → 被踢 → Kicked（终态）
```

### ChatEventListener

The SDK notifies the game at key lifecycle nodes.

| Callback | When it fires | Typical use |
|---|---|---|
| `OnConnectionStateChanged(state)` | connection state change | UI status indicator |
| `OnLoginResult(code, user_id)` | login result | on success -> show the chat UI; on failure -> show a prompt |
| `OnKicked(reason)` | kicked offline | popup showing the reason, guiding re-login |
| `OnReconnecting(attempt, delay_ms)` | reconnecting | show a "reconnecting..." prompt |
| `OnReconnected()` | reconnect succeeded | restore the UI, refresh unread |
| `OnMessageReceived(msg)` | new message received | update the chat window |
| `OnUnreadChanged(type, channel_id, count)` | unread count changed | update the badge |
| `OnPresenceChanged(user_id, status)` | friend presence changed | update the friend list |
| `OnTypingIndicator(user_id, channel_id, is_typing)` | typing | show "the other party is typing..." |
| `OnMarqueeMessage(msg, ttl_seconds)` | marquee message | scrolling across the top of the screen |
| `OnSystemAnnouncement(msg)` | system announcement | popup or highlighted display |

**C++ definition**: `sdks/core/include/chirp/chat_event_listener.h`

**Game usage example**:

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

## Auth hook

### AuthProvider

The game login system connects with the SDK.

| Callback | When it fires | Description |
|---|---|---|
| `GetToken()` | when the SDK needs a token | returns the token issued by the game login system |
| `OnTokenExpired(renew)` | when the token expires | after the game refreshes the token, call `renew(new_token)` |
| `OnAuthResult(code, user_id)` | auth result | notifies the game of auth success/failure |

**C++ definition**: `sdks/core/include/chirp/auth_provider.h`

**Game usage example**:

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

## Command extension hooks

### CommandHandler

The game registers custom chat commands.

| Method | Description |
|---|---|
| `GetName()` | command name (without the leading slash), e.g. "trade" |
| `GetDescription()` | command description, used by /help |
| `GetUsage()` | usage example, e.g. "/trade &lt;player name&gt;" |
| `Execute(args, sender_id)` | execute the command; returns `true` = handled |

**C++ definition**: `sdks/core/include/chirp/command_handler.h`

**Game usage example**:

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

**Command parsing flow**:

```
玩家输入: "/trade PlayerA 100 gold"
  → SDK 解析: command="trade", args="PlayerA 100 gold"
  → 遍历已注册 CommandHandler
  → 找到 TradeCommand.Execute("PlayerA 100 gold", sender_id)
  → 如果所有 handler 都返回 false → 提示"未知命令"
```

## Message storage hook

### MessageStore

The game customizes the local caching strategy for messages.

| Method | Description |
|---|---|
| `Save(msg)` | store one message |
| `Load(type, channel_id, limit, before_timestamp)` | load history messages |
| `MarkRead(type, channel_id, message_id)` | mark as read |
| `GetUnreadCount(type, channel_id)` | get the unread count |
| `Cleanup(older_than)` | clean up expired messages |

**C++ definition**: `sdks/core/include/chirp/message_store.h`

**Built-in implementations**: `MemoryMessageStore` (in-memory cache, for lightweight games), `FileMessageStore` (file persistence, `chirp/file_message_store.h`, header-only; append-only log + read cursor + `Compact()` atomic rewrite — the same file format as the C# SDK)

**Game usage example**:

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

## Per-language interface mapping

| Hook | C++ | C# | TypeScript | GDScript | Dart |
|---|---|---|---|---|---|
| Message interception | `MessageInterceptor` virtual base class | `IMessageInterceptor` interface | `MessageInterceptor` interface | signal + callback | `MessageInterceptor` abstract class |
| Event listening | `ChatEventListener` virtual base class | `IChatEventListener` interface | `ChatEventListener` interface | signal | `ChatEventListener` abstract class |
| Auth | `AuthProvider` virtual base class | `IAuthProvider` interface | `AuthProvider` interface | callback | `AuthProvider` abstract class |
| Command | `CommandHandler` virtual base class | `ICommandHandler` interface | `CommandHandler` interface | signal | `CommandHandler` abstract class |
| Storage | `MessageStore` virtual base class | `IMessageStore` interface | `MessageStore` interface | Resource | `MessageStore` abstract class |

> The C#/TypeScript columns are both implemented (the GDScript column is planned — Godot takes the C# reuse route, see `docs/sdk/godot.md`; the Dart/Flutter SDK was removed on 2026-09-29). Cross-language built-in implementations also include the `WordFilterInterceptor` in C++/C#/TS (send-side word-filter precheck, semantics aligned with the server; see `docs/design-notes/sdk_compatibility.md`).

## SDK registration entry point

All hooks are registered through the SDK instance and must be called before `Connect`:

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
