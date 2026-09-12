#ifndef CHIRP_CHAT_SERVER_GATEWAY_PEER_H_
#define CHIRP_CHAT_SERVER_GATEWAY_PEER_H_

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include <asio.hpp>

#include "proto/gateway.pb.h"
#include "proto/server_gateway.pb.h"

namespace chirp::chat {

// Long-lived client connection from chat to the server-plane hub
// (chirp_server_gateway). Chat dials out, authenticates with a service_id +
// secret, answers the hub's keepalive cadence, and receives forwarded
// injections. Reconnects with a fixed delay until stopped.
//
// All handlers run on the io_context given at construction (chat's single io
// thread); nothing here needs locking. The class keeps itself alive through
// enable_shared_from_this, so async handlers never dangle.
class ServerGatewayPeer : public std::enable_shared_from_this<ServerGatewayPeer> {
 public:
  using InjectHandler =
      std::function<void(const chirp::server_gateway::InjectMessageNotify&)>;

  struct Options {
    std::string host;
    uint16_t port = 8100;
    std::string service_id = "chat";
    std::string secret;
    int reconnect_delay_seconds = 3;
    // Fallback cadence until the hub assigns one in SERVER_AUTH_RESP.
    int heartbeat_interval_seconds = 30;
  };

  static std::shared_ptr<ServerGatewayPeer> Create(asio::io_context& io,
                                                   Options options,
                                                   InjectHandler on_inject);
  ~ServerGatewayPeer();

  void Start();
  void Stop();

 private:
  struct PrivateTag { explicit PrivateTag() = default; };
  ServerGatewayPeer(asio::io_context& io, Options options, InjectHandler on_inject,
                    PrivateTag);

  void DoConnect();
  void SendAuth();
  void ReadHeader();
  void ReadBody(uint32_t size);
  void HandlePacket(const chirp::gateway::Packet& pkt);
  void SendPacket(chirp::gateway::MsgID msg_id, int64_t seq,
                  const google::protobuf::Message& body);
  void SendHeartbeat();
  void ArmHeartbeat();
  void OnConnectionLost();
  void ScheduleReconnect();

  asio::io_context& io_;
  Options options_;
  InjectHandler on_inject_;
  asio::ip::tcp::socket socket_;
  asio::steady_timer timer_;  // reconnect delay and heartbeat, one at a time
  std::array<uint8_t, 4> header_{};
  std::string body_;
  bool connected_ = false;
  bool stopping_ = false;
  int64_t heartbeat_seq_ = 0;
  int heartbeat_interval_seconds_ = 30;  // reassigned by the hub on auth
};

}  // namespace chirp::chat

#endif  // CHIRP_CHAT_SERVER_GATEWAY_PEER_H_
