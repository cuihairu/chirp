#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include <asio.hpp>

#include "network/session.h"
#include "proto/gateway.pb.h"

namespace chirp {
namespace chat {

// Spoke 配置
struct PeerSpokeConfig {
  std::string hub_host;           // app_chat 地址
  uint16_t hub_port = 7000;
  std::string service_id;         // e.g. "game_42"
  std::string service_secret;
  std::string game_id;            // 命名空间前缀
  int protocol_version = 1;
};

// Spoke 模式：game_chat 作为 spoke 注册到 app_chat hub。
// 部署为 game_chat 时启用。
class PeerSpoke {
 public:
  using ConnectedCallback = std::function<void()>;
  using DisconnectedCallback = std::function<void()>;
  using InjectCallback = std::function<void(const gateway::PeerInjectMessageNotify&)>;

  PeerSpoke(asio::io_context& io, PeerSpokeConfig config);
  ~PeerSpoke();

  // 连接到 hub 并注册
  void Connect();

  // 断开连接
  void Disconnect();

  // 是否已连接并注册
  bool IsConnected() const;

  // 发送频道消息到 hub
  void SendChannelMessage(const std::string& channel_id,
                          const chat::ChatMessage& msg);

  // 设置回调
  void SetConnectedCallback(ConnectedCallback cb);
  void SetDisconnectedCallback(DisconnectedCallback cb);
  void SetInjectCallback(InjectCallback cb);

 private:
  void HandlePacket(const gateway::Packet& pkt);
  void OnConnected();
  void OnDisconnected();

  asio::io_context& io_;
  PeerSpokeConfig config_;
  std::unique_ptr<asio::ip::tcp::socket> socket_;
  bool connected_ = false;
  bool registered_ = false;

  ConnectedCallback on_connected_;
  DisconnectedCallback on_disconnected_;
  InjectCallback on_inject_;

  // 读缓冲区
  std::string read_buf_;
};

}  // namespace chat
}  // namespace chirp
