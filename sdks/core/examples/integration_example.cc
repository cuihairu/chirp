// Chirp 接入示例:完整接入模式一条龙(非冒烟最小路径,那是 sdk_example.cc)。
//
// 本例演示接入方真正要处理的六件事:
//   1. 状态机监听(AddListener):连接/登录/重连/KICK 全生命周期
//   2. AuthProvider:token 由游戏登录系统供给 + AUTH_FAILED 续期一次
//   3. 类型化发送 + 业务码全处理(ec == OK 不代表业务成功)
//   4. 服务端防线体验:世界节奏(RATE_LIMITED)/超长(CONTENT_TOO_LONG)
//   5. 历史与已读:FetchHistory(服务端权威) + MarkChannelRead
//   6. 本地存档:MemoryMessageStore + LoadHistory(离线可读)
//
// 对着一个默认配置的 game_chat(basic 形态)运行:
//   ./integration_example --host 127.0.0.1 --port 7000 --user user_1
//
// 回调线程注意:所有回调在 SDK 内部 io 线程触发。本例用原子变量 +
// 主线程轮询桥接(引擎适配层换成 AsyncTask/Dispatcher 即是同一模式)。

#include "chirp/auth_provider.h"
#include "chirp/chat_event_listener.h"
#include "chirp/message_store.h"
#include "chirp/sdk_client.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

using chirp::sdk::ChatClient;
using chirp::sdk::ChatConfig;
using chirp::sdk::ConnectionState;

namespace {

// ---- 主线程等待谓词:引擎适配层里换成主线程 dispatch,语义相同 ----
bool WaitUntil(const std::function<bool()>& done, int timeout_ms) {
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    if (done()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
  }
  return done();
}

const char* StateName(ConnectionState s) {
  switch (s) {
    case ConnectionState::Disconnected: return "Disconnected";
    case ConnectionState::Connecting: return "Connecting";
    case ConnectionState::Connected: return "Connected";
    case ConnectionState::LoggedIn: return "LoggedIn";
    case ConnectionState::WaitingReconnect: return "WaitingReconnect";
    case ConnectionState::Kicked: return "Kicked";
  }
  return "?";
}

const char* CodeName(int code) {
  switch (code) {
    case 0: return "OK";
    case 1: return "INTERNAL_ERROR";
    case 2: return "INVALID_PARAM";
    case 3: return "AUTH_FAILED";
    case 4: return "SESSION_EXPIRED";
    case 5: return "USER_NOT_FOUND";
    case 6: return "TARGET_OFFLINE";
    case 7: return "SERVER_UNAVAILABLE";
    case 8: return "RATE_LIMITED";
    case 9: return "VERSION_MISMATCH";
    case 10: return "WORD_FILTERED";
    case 11: return "CONTENT_TOO_LONG";
  }
  return "UNKNOWN";
}

// ---- 场景 1/5:生命周期监听。覆写需要的回调,其余走默认空实现 ----
class GameListener : public chirp::sdk::ChatEventListener {
 public:
  std::atomic<ConnectionState> last_state{ConnectionState::Disconnected};
  std::atomic<int> reconnect_attempts{0};
  std::atomic<bool> kicked{false};

  void OnConnectionStateChanged(int state) override {
    // io 线程:这里只写原子/投递事件,不碰 UI。
    last_state.store(static_cast<ConnectionState>(state));
    std::cout << "[状态] " << StateName(static_cast<ConnectionState>(state))
              << std::endl;
  }
  void OnReconnecting(int attempt, int delay_ms) override {
    reconnect_attempts.store(attempt);
    std::cout << "[重连] 第 " << attempt << " 次," << delay_ms << "ms 后重试"
              << std::endl;
  }
  void OnKicked(const std::string& reason) override {
    kicked.store(true);
    std::cout << "[顶号] reason=" << reason << "(终态,不要再自动重连)"
              << std::endl;
  }
};

// ---- 场景 2:认证提供方。token 来自游戏自己的登录系统 ----
class GameAuthProvider : public chirp::sdk::AuthProvider {
 public:
  explicit GameAuthProvider(std::string initial_token)
      : token_(std::move(initial_token)) {}

  std::string GetToken() override { return token_; }

  // AUTH_FAILED 时 SDK 给一次续期机会:调用 renew(new_token) 自动重登,
  // 不调用则停在 Disconnected(游戏应回登录页)。
  void OnTokenExpired(std::function<void(const std::string&)> renew) override {
    std::cout << "[认证] token 被拒,续期一次(真实游戏里这里刷新登录态)"
              << std::endl;
    // 演示:原 token 重试。真实场景:renew(RefreshFromLoginServer());
    renew(token_);
  }

 private:
  std::string token_;
};

struct Args {
  std::string host = "127.0.0.1";
  int port = 7000;
  std::string user = "user_1";
  std::string peer = "user_2";
  int timeout_ms = 10000;
};

Args ParseArgs(int argc, char** argv) {
  Args a;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    auto next = [&]() -> std::string {
      return (i + 1 < argc) ? argv[++i] : "";
    };
    if (arg == "--host") a.host = next();
    else if (arg == "--port") a.port = std::stoi(next());
    else if (arg == "--user") a.user = next();
    else if (arg == "--peer") a.peer = next();
    else if (arg == "--timeout-ms") a.timeout_ms = std::stoi(next());
    else if (arg == "--help" || arg == "-h") {
      std::cout
          << "用法: integration_example [--host H] [--port P] [--user U]\n"
             "                        [--peer R] [--timeout-ms N]\n"
             "默认: --host 127.0.0.1 --port 7000 --user user_1 --peer user_2\n"
             "对着 basic 形态的 chirp_game_chat 运行。\n";
      std::exit(0);
    }
  }
  return a;
}

} // namespace

int main(int argc, char** argv) {
  const Args args = ParseArgs(argc, argv);
  int failures = 0;
  auto check = [&](bool ok, const std::string& what) {
    std::cout << (ok ? "  [通过] " : "  [失败] ") << what << std::endl;
    if (!ok) ++failures;
  };

  ChatConfig config;
  config.gateway_host = args.host;
  config.gateway_port = static_cast<uint16_t>(args.port);
  // 心跳/重连/超时全部吃默认值(25s 心跳、2 次未答判死、500ms→15s 退避、
  // 10s 请求超时)——接入方通常不需要改它们,改之前先读避坑指南。

  ChatClient client(config);

  auto listener = std::make_shared<GameListener>();
  client.AddListener(listener);

  // 本地存档:收到的消息会 Save;断网期间 LoadHistory 仍可读(离线 UI)。
  client.SetMessageStore(std::make_unique<chirp::sdk::MemoryMessageStore>(500));

  auto auth = std::make_shared<GameAuthProvider>(args.user);
  client.SetAuthProvider(auth);

  // ---- 场景 1:连接 + 登录(Login("") 经 AuthProvider 取 token) ----
  std::cout << "== 场景 1:连接与登录 ==" << std::endl;
  client.Connect();
  check(WaitUntil([&] { return client.GetState() == ConnectionState::Connected; },
                  args.timeout_ms),
        "连接建立 (Connected)");

  std::atomic<bool> login_done{false};
  std::error_code login_ec;
  client.Login("", [&](const std::error_code& ec, const std::string& uid) {
    login_ec = ec;
    std::cout << (ec ? std::string("[登录失败] ") + ec.message()
                     : "[登录成功] user_id=" + uid)
              << std::endl;
    login_done = true;
  });
  check(WaitUntil([&] { return login_done.load() && !login_ec; },
                  args.timeout_ms),
        "登录完成 (LoggedIn)");
  if (failures) {
    std::cerr << "无法建立连接/登录,后续场景跳过(先确认 chat 在 "
              << args.host << ":" << args.port << " 上)" << std::endl;
    client.Disconnect();
    return 1;
  }

  // ---- 场景 3:类型化发送 + 业务码全处理 ----
  std::cout << "== 场景 3:私聊发送 + 业务码处理 ==" << std::endl;
  {
    ChatClient::SendOptions opts;  // 默认 PRIVATE
    opts.receiver_id = args.peer;
    std::atomic<bool> sent{false};
    client.SendMessage(opts, "hello from integration_example",
                       [&](const std::error_code& ec,
                           const chirp::chat::SendMessageResponse& resp) {
                         // ec 只覆盖传输层;业务结果一律读 resp.code()。
                         if (ec) {
                           std::cout << "  [传输错误] " << ec.message()
                                     << "(重连后由业务层补发)" << std::endl;
                         } else if (resp.code() != chirp::common::OK) {
                           std::cout << "  [业务拒绝] code=" << resp.code()
                                     << " (" << CodeName(resp.code()) << ")"
                                     << std::endl;
                         } else {
                           std::cout << "  [已受理] message_id="
                                     << resp.message_id() << std::endl;
                         }
                         sent = true;
                       });
    check(WaitUntil([&] { return sent.load(); }, args.timeout_ms),
          "发送往返完成");
  }

  // ---- 场景 4:服务端防线(世界节奏 / 内容长度) ----
  std::cout << "== 场景 4:防线体验(RATE_LIMITED / CONTENT_TOO_LONG) =="
            << std::endl;
  {
    ChatClient::SendOptions world;
    world.channel_type = chirp::chat::WORLD;
    world.channel_id = "world";

    std::atomic<int> replies{0};
    std::atomic<int> second_code{-1};
    auto on_reply = [&](const chirp::chat::SendMessageResponse& resp) {
      if (replies.fetch_add(1) == 1) second_code = resp.code();
    };
    auto send_world = [&](const std::string& text) {
      client.SendMessage(world, text,
                         [&](const std::error_code& ec,
                             const chirp::chat::SendMessageResponse& resp) {
                           if (ec) {
                             std::cout << "  [传输错误] " << ec.message()
                                       << std::endl;
                           }
                           on_reply(resp);
                         });
    };
    // 世界频道最小间隔 5s:两条背靠背,第二条应回 RATE_LIMITED。
    send_world("first world message");
    send_world("second world message (too fast)");
    check(WaitUntil([&] { return replies.load() >= 2; }, args.timeout_ms),
          "世界频道两连发完成往返");
    std::cout << "  第二条 code=" << second_code.load() << " ("
              << CodeName(second_code.load()) << ")"
              << "(basic 默认世界节奏 5s;非 RATE_LIMITED 说明部署改过阈值)"
              << std::endl;

    // 私聊上限 200 码点:210 个汉字必然超长,应回 CONTENT_TOO_LONG。
    ChatClient::SendOptions priv;
    priv.receiver_id = args.peer;
    std::atomic<int> long_code{-1};
    std::atomic<bool> long_done{false};
    std::string overlong;
    for (int i = 0; i < 210; ++i) {
      overlong += "超";  // 逐字拼接:单引号 '超' 是多字节字面量,不可移植
    }
    client.SendMessage(priv, overlong,
                       [&](const std::error_code& ec,
                           const chirp::chat::SendMessageResponse& resp) {
                         if (!ec) long_code = resp.code();
                         long_done = true;
                       });
    check(WaitUntil([&] { return long_done.load(); }, args.timeout_ms),
          "超长消息完成往返");
    std::cout << "  超长 code=" << long_code.load() << " ("
              << CodeName(long_code.load()) << ")"
              << "(私聊上限 200 码点,按字符数不是字节数)" << std::endl;
  }

  // ---- 场景 5:历史与已读 ----
  std::cout << "== 场景 5:服务端历史 + 标已读 ==" << std::endl;
  {
    std::atomic<bool> hist_done{false};
    int hist_count = -1;
    client.FetchHistory(
        chirp::chat::PRIVATE, args.peer, 20, 0,
        [&](const std::error_code& ec,
            const chirp::chat::GetHistoryResponse& resp) {
          if (!ec && resp.code() == chirp::common::OK) {
            hist_count = resp.messages_size();
          }
          hist_done = true;
        });
    check(WaitUntil([&] { return hist_done.load(); }, args.timeout_ms),
          "FetchHistory 完成");
    std::cout << "  与 " << args.peer << " 的服务端历史 " << hist_count
              << " 条" << std::endl;

    std::atomic<bool> read_done{false};
    bool read_ok = false;
    client.MarkChannelRead(
        chirp::chat::PRIVATE, args.peer, "latest",
        [&](const std::error_code& ec,
            const chirp::chat::MarkReadResponse& resp) {
          read_ok = !ec && resp.code() == chirp::common::OK;
          read_done = true;
        });
    check(WaitUntil([&] { return read_done.load() && read_ok; },
                    args.timeout_ms),
          "MarkChannelRead 完成");
  }

  // ---- 场景 6:本地存档(离线可读) ----
  std::cout << "== 场景 6:本地存档 LoadHistory ==" << std::endl;
  {
    auto local = client.LoadHistory(chirp::chat::PRIVATE, args.peer, 10);
    std::cout << "  本地存档 " << local.size() << " 条(断网也能读)"
              << std::endl;
    check(true, "LoadHistory 返回(条数取决于本会话收发)");
  }

  client.Disconnect();
  std::cout << (failures == 0 ? "全部场景通过"
                              : std::to_string(failures) + " 项失败")
            << std::endl;
  return failures == 0 ? 0 : 1;
}
