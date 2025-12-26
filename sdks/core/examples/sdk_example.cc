#include "chirp/sdk_client.h"

#include <iostream>
#include <thread>
#include <chrono>

using namespace chirp::sdk;

int main(int argc, char** argv) {
  std::cout << "=== Chirp SDK 示例程序 ===" << std::endl;

  // 配置 SDK
  ChatConfig config;
  config.gateway_host = "localhost";
  // 当前示例直连 chat service（默认端口 7000）。
  config.gateway_port = 7000;
  config.enable_websocket = false;
  config.heartbeat_interval_seconds = 30;

  // 创建客户端
  ChatClient client(config);

  // 设置回调
  client.SetDisconnectCallback([](const std::error_code& ec) {
    std::cout << "[断开连接] 错误: " << ec.message() << std::endl;
  });

  client.SetMessageCallback([](const std::string& sender, const std::string& content) {
    std::cout << "[收到消息] " << sender << ": " << content << std::endl;
  });

  client.SetKickCallback([](const std::string& reason) {
    std::cout << "[被踢出] 原因: " << reason << std::endl;
  });

  // 连接服务器
  std::cout << "正在连接到 " << config.gateway_host << ":" << config.gateway_port << "..." << std::endl;
  client.Connect();

  // 等待连接建立
  for (int i = 0; i < 50; ++i) {
    if (client.GetState() == ConnectionState::Connected) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  if (client.GetState() != ConnectionState::Connected) {
    std::cerr << "连接失败!" << std::endl;
    return 1;
  }

  std::cout << "连接成功!" << std::endl;

  // 登录
  std::string user_id = "user123";
  std::cout << "正在登录 (user_id: " << user_id << ")..." << std::endl;

  client.Login(user_id, [&client](const std::error_code& ec, const std::string& uid) {
    if (ec) {
      std::cerr << "登录失败: " << ec.message() << std::endl;
      return;
    }
    std::cout << "登录成功! user_id: " << uid << std::endl;

    // 登录成功后发送测试消息
    client.SendMessage("user_2", "Hello from Chirp SDK!");
  });

  // 运行一段时间
  std::cout << "\n程序运行中，按 Ctrl+C 退出..." << std::endl;
  std::this_thread::sleep_for(std::chrono::seconds(5));

  // 断开连接
  std::cout << "\n正在断开连接..." << std::endl;
  client.Disconnect();

  std::cout << "SDK 示例程序结束" << std::endl;
  return 0;
}
