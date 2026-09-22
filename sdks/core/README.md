# Chirp Core SDK(C++)

C++ 客户端协议核心:`chirp::sdk::ChatClient` 直连 chat 网关的 TCP 长连接(`[u32_be len][Packet protobuf]` 帧)。桌面游戏客户端与 Unreal SDK(`sdks/unreal`)共用这一份实现;连接语义与 web/mobile/unity 三端的 `ChirpClient` 逐一对齐。

## 能力面

- **连接状态机**:`Disconnected → Connecting → Connected → LoggedIn`,外加 `WaitingReconnect`(运行中断线的退避重连中)与 `Kicked`(顶号终态)。
- **请求-响应**:通用 `Request(req_msg_id, resp_msg_id, body, cb)`,sequence 关联 + 响应 msg_id 校验,默认 10s 超时(`ChatConfig::request_timeout_ms`),迟到的响应无害丢弃。
- **notify 订阅**:`OnNotify(msg_id, cb)` 返回退订句柄,`OffNotify` 退订;便捷回调(`SetMessageCallback` 等)与之平行。
- **心跳**:默认 25s,pong 必须回显 ping 的非零 sequence 才记为已答;连续 `max_missed_pongs`(默认 2)次未答判定连接死亡,进入重连。
- **自动重连**:指数退避 500ms→15s ±20% 抖动;`max_reconnect_attempts`(-1 无限);KICK 不重连;显式 `Disconnect()`/`Logout()` 取消重连。
- **钩子接口**(设计契约见 `docs/design-notes/sdk_hooks.md`):`SetMessageInterceptor` / `SetAuthProvider` / `SetMessageStore` / `AddListener` / `RegisterCommand`,全部在 `Connect()` 前注册;配套 `LoadHistory` 等存储转发查询。
- 所有回调在内部 io 线程触发;引擎适配层负责派发到游戏线程(见 unreal 壳的 `AsyncTask` 用法)。

## 构建

in-tree(随 chirp 仓库构建,`sdks/core/CMakeLists.txt` 被根 CMake 引入):

```bash
cmake --build build --target chirp_core_sdk_static sdk_example
```

standalone(无 chirp 树也能编,自动落回 checked-in gencode):

```bash
cd sdks/core && cmake -B build && cmake --build build
```

产物:`chirp_core_sdk_static`(静态)/ `chirp_core_sdk`(共享;依赖的 chirp_network/chirp_common 会被自动以 PIC 重编)。

## 测试

- 单测:`tests/unit/sdk_core_test.cc`(73 例:状态机、loopback 登录/收发、请求关联与超时、notify 订阅退订、心跳死亡判定、踢线终态、断线重连、钩子接线),CI 自动跑。
- 进程级 E2E:`./test_services.sh --smoke-sdk`(两个 SDK 实例对真 `chirp_chat` 双向收发 + 离线队列)。

## 使用

```cpp
#include "chirp/sdk_client.h"

chirp::sdk::ChatConfig config;
config.gateway_host = "127.0.0.1";
config.gateway_port = 5000;
chirp::sdk::ChatClient client(config);

client.SetMessageCallback([](const std::string& sender, const std::string& content) {
  // io 线程:投递回你的主线程再碰 UI
});
client.Connect();
client.Login("user-42", [](const std::error_code& ec, const std::string& user_id) {
  // ...
});
client.SendMessage("peer-7", "hello");
```

完整示例见 `examples/sdk_example.cc`(`--smoke-sdk` 的被测程序)。

## 钩子接口

五个钩子全部可选、在 `Connect()` 前注册、回调都在 SDK 内部 io 线程触发(引擎适配层自行派发回游戏线程)。设计契约:`docs/design-notes/sdk_hooks.md`。

```cpp
#include "chirp/message_interceptor.h"
#include "chirp/command_handler.h"
#include "chirp/message_store.h"
#include "chirp/auth_provider.h"
#include "chirp/chat_event_listener.h"

class GameListener : public chirp::sdk::ChatEventListener {
  void OnConnectionStateChanged(int state) override { /* 状态机驱动 UI */ }
  void OnKicked(const std::string& reason) override { /* 顶号弹窗 */ }
  // 其余回调有默认空实现,按需覆写。
};

class TradeCommand : public chirp::sdk::CommandHandler {
  std::string GetName() const override { return "trade"; }
  std::string GetDescription() const override { return "发起交易"; }
  bool Execute(const std::string& args, const std::string& sender_id) override {
    return true;  // 返回 false 则继续找下一个 handler
  }
};

// 敏感词/改写:
class CleanInterceptor : public chirp::sdk::MessageInterceptor {
  bool OnBeforeSend(chirp::chat::SendMessageRequest& msg) override {
    return !IsBanned(msg.content());  // false = 拦截,消息不进网络
  }
  bool OnBeforeReceive(chirp::chat::ChatMessage& msg) override {
    msg.set_content(Filter(msg.content()));
    return true;
  }
};

client.SetMessageInterceptor(std::make_shared<CleanInterceptor>());
client.SetMessageStore(std::make_unique<chirp::sdk::MemoryMessageStore>(200));
client.SetAuthProvider(gameAuthProvider);            // Login("") 时自动取 token,
                                                     // AUTH_FAILED 时给一次续期机会
client.AddListener(std::make_shared<GameListener>());
client.RegisterCommand(std::make_unique<TradeCommand>());

// 随后照常 Connect()/Login();历史与未读经转发方法查询:
auto recent = client.LoadHistory(chirp::chat::WORLD, "world", 20);
```

语义要点:
- 命令路由:注册了至少一个 handler 后,`/cmd args` 形态的 `SendMessage` 走本地路由不再上网;全 miss 本地丢弃(Warn 日志,unknown command 提示由引擎层负责)。零注册时 `/` 消息照常发送。
- 拦截丢弃(`OnBeforeReceive` 返回 false)的消息:不存储、不触发任何回调、不进原始 notify 分发。
- 存储转发方法(`LoadHistory`/`MarkRead`/`GetUnreadCount`/`CleanupMessages`)可从任意线程调;自定义 store 的并发安全由实现方负责(`MemoryMessageStore` 内置互斥,但不跟踪已读,`GetUnreadCount` 恒 0)。

## 历史

2026-09 架构评审删除了从未编译过的 `chirp::core` 模块层(include/chirp/core、src/client_impl、src/modules,约 2000 行,头文件自带编译错误且被 Unity/Unreal 旧桥各自错误引用);现存的只有 smoke 验证过的 `sdk.cc`/`sdk_client.cc` 链,并在此之上补齐了通用 Request/notify 订阅/心跳回声/退避重连/KICK 终态。
