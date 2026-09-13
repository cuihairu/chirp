#include "chirp/sdk_client.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>

using namespace chirp::sdk;

namespace {

struct Args {
  std::string host = "localhost";
  int port = 7000;
  std::string user = "user123";
  std::string peer;        // 非空:登录后向该用户发消息
  std::string message;     // 发送内容(默认 "hello from <user>")
  std::string expect;      // 非空:等待收到内容包含该子串的消息
  std::string expect_from; // 可选:要求消息来自该发送者
  int timeout_ms = 10000;  // 连接/登录/等待消息的统一超时
  int settle_ms = 500;     // 无 expect 时发送后停留时间(等待包 flush)
};

void PrintUsage(const char* argv0) {
  std::cout << "用法: " << argv0 << " [选项]\n"
            << "  --host H           服务器地址 (默认 localhost)\n"
            << "  --port P           端口 (默认 7000, chat TCP)\n"
            << "  --user U           登录用户/token (默认 user123)\n"
            << "  --peer R           登录后向 R 发送消息\n"
            << "  --message M        发送内容 (默认 \"hello from <user>\")\n"
            << "  --expect S         等待收到内容包含 S 的消息, 命中打印 SMOKE_OK 并退出 0\n"
            << "  --expect-from U    配合 --expect, 要求发送者必须是 U\n"
            << "  --timeout-ms N     超时毫秒 (默认 10000)\n"
            << "  --settle-ms N      无 --expect 时发送后停留毫秒 (默认 500)\n"
            << "无参数运行时等同 demo: --user user123 --peer user_2 --settle-ms 5000\n"
            << "退出码: 0 成功 / 1 连接失败 / 2 登录失败 / 3 等待消息超时" << std::endl;
}

bool ParseArgs(int argc, char** argv, Args& args) {
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto next = [&](const char* name) -> std::string {
      if (i + 1 >= argc) {
        std::cerr << "缺少参数值: " << name << std::endl;
        throw false;
      }
      return argv[++i];
    };
    try {
      if (a == "--host") args.host = next("--host");
      else if (a == "--port") args.port = std::stoi(next("--port"));
      else if (a == "--user") args.user = next("--user");
      else if (a == "--peer") args.peer = next("--peer");
      else if (a == "--message") args.message = next("--message");
      else if (a == "--expect") args.expect = next("--expect");
      else if (a == "--expect-from") args.expect_from = next("--expect-from");
      else if (a == "--timeout-ms") args.timeout_ms = std::stoi(next("--timeout-ms"));
      else if (a == "--settle-ms") args.settle_ms = std::stoi(next("--settle-ms"));
      else if (a == "--help" || a == "-h") { PrintUsage(argv[0]); std::exit(0); }
      else { std::cerr << "未知参数: " << a << std::endl; PrintUsage(argv[0]); return false; }
    } catch (bool) {
      return false;
    } catch (const std::exception& e) {
      std::cerr << "参数解析失败: " << a << " (" << e.what() << ")" << std::endl;
      return false;
    }
  }
  return true;
}

} // namespace

int main(int argc, char** argv) {
  Args args;
  if (argc == 1) {
    // 保持原有 demo 行为
    args.peer = "user_2";
    args.settle_ms = 5000;
  } else if (!ParseArgs(argc, argv, args)) {
    return 1;
  }
  if (args.message.empty()) {
    args.message = "hello from " + args.user;
  }

  std::cout << "=== Chirp SDK 客户端 ===" << std::endl;

  ChatConfig config;
  config.gateway_host = args.host;
  config.gateway_port = args.port;
  config.enable_websocket = false;
  config.heartbeat_interval_seconds = 30;

  ChatClient client(config);

  std::atomic<bool> got_expected{false};
  std::atomic<bool> kicked{false};

  client.SetDisconnectCallback([](const std::error_code& ec) {
    std::cout << "[断开连接] 错误: " << ec.message() << std::endl;
  });

  client.SetMessageCallback([&](const std::string& sender, const std::string& content) {
    std::cout << "[收到消息] " << sender << ": " << content << std::endl;
    if (!args.expect.empty() &&
        content.find(args.expect) != std::string::npos &&
        (args.expect_from.empty() || sender == args.expect_from)) {
      got_expected = true;
      std::cout << "SMOKE_OK " << sender << " -> " << args.user << ": " << content << std::endl;
    }
  });

  client.SetKickCallback([&](const std::string& reason) {
    kicked = true;
    std::cout << "[被踢出] 原因: " << reason << std::endl;
  });

  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(args.timeout_ms);

  std::cout << "正在连接到 " << args.host << ":" << args.port << "..." << std::endl;
  client.Connect();
  while (client.GetState() != ConnectionState::Connected &&
         std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  if (client.GetState() != ConnectionState::Connected) {
    std::cerr << "连接失败!" << std::endl;
    return 1;
  }
  std::cout << "连接成功!" << std::endl;

  std::atomic<bool> login_done{false};
  std::atomic<bool> login_ok{false};
  std::cout << "正在登录 (user_id: " << args.user << ")..." << std::endl;
  client.Login(args.user, [&](const std::error_code& ec, const std::string& uid) {
    if (ec) {
      std::cerr << "登录失败: " << ec.message() << std::endl;
      login_ok = false;
    } else {
      std::cout << "登录成功! user_id: " << uid << std::endl;
      login_ok = true;
    }
    login_done = true;
  });
  while (!login_done && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  if (!login_done || !login_ok) {
    std::cerr << "登录超时或失败!" << std::endl;
    return 2;
  }

  if (!args.peer.empty()) {
    std::cout << "发送消息 -> " << args.peer << ": " << args.message << std::endl;
    client.SendMessage(args.peer, args.message);
  }

  int rc = 0;
  if (!args.expect.empty()) {
    while (!got_expected && !kicked && std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (!got_expected) {
      std::cerr << "等待消息超时: expect=\"" << args.expect << "\"" << std::endl;
      rc = 3;
    }
  } else {
    std::this_thread::sleep_for(std::chrono::milliseconds(args.settle_ms));
  }

  client.Disconnect();
  std::cout << "客户端结束 (rc=" << rc << ")" << std::endl;
  return rc;
}
