#ifndef CHIRP_CHAT_SERVER_GATEWAY_PEER_H_
#define CHIRP_CHAT_SERVER_GATEWAY_PEER_H_

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>

#include <asio.hpp>

#include "proto/common.pb.h"
#include "proto/gateway.pb.h"
#include "proto/server_gateway.pb.h"

namespace chirp::chat {

// Long-lived client connection from chat to the server-plane hub
// (chirp_server_gateway). Chat dials out, authenticates with a service_id +
// secret, answers the hub's keepalive cadence, receives forwarded injections,
// and runs request/response RPCs (inject / event publish / event ack) over
// the same connection. Reconnects with a fixed delay until stopped.
//
// Entry points (Start/Stop/SendInject/SendEventPublish/SendEventAck) may be
// called from any thread: each hops onto the peer's strand, and every socket
// and timer handler runs on that strand too, so no member needs extra
// locking. The class keeps itself alive through enable_shared_from_this, so
// async handlers never dangle.
class ServerGatewayPeer : public std::enable_shared_from_this<ServerGatewayPeer> {
 public:
  using InjectHandler =
      std::function<void(const chirp::server_gateway::InjectMessageNotify&)>;
  using EventHandler =
      std::function<void(const chirp::server_gateway::EventDeliverNotify&)>;
  // Invoked exactly once per RPC: the hub's response code on a well-formed
  // reply, INTERNAL_ERROR on an unparseable reply body, SERVER_UNAVAILABLE
  // when the RPC was sent while disconnected or was lost to a connection
  // drop. There is deliberately no per-RPC timeout: the hub closes
  // connections silent for twice its heartbeat cadence, which fails any
  // in-flight RPC through the same path.
  using RpcCallback = std::function<void(chirp::common::ErrorCode code)>;

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
                                                   InjectHandler on_inject,
                                                   EventHandler on_event = nullptr);
  ~ServerGatewayPeer();

  void Start();
  void Stop();

  // Uplink RPCs. Callable from any thread; each posts onto the peer's strand.
  // Sending while disconnected (or stopped) fails fast with
  // SERVER_UNAVAILABLE - nothing is queued for a later connection. The
  // callback may fire on the strand thread, so it must not block.
  void SendInject(const chirp::server_gateway::MessageInjectRequest& req, RpcCallback cb);
  void SendEventPublish(const chirp::server_gateway::EventPublishRequest& req,
                        RpcCallback cb);
  void SendEventAck(const chirp::server_gateway::EventAckRequest& req, RpcCallback cb);

 private:
  struct PrivateTag { explicit PrivateTag() = default; };
  ServerGatewayPeer(asio::io_context& io, Options options, InjectHandler on_inject,
                    EventHandler on_event, PrivateTag);

  // Turns a response body into its error code; an unparseable body maps to
  // INTERNAL_ERROR.
  using BodyParser = std::function<chirp::common::ErrorCode(const std::string& body)>;
  struct PendingRpc {
    chirp::gateway::MsgID resp_id;
    BodyParser parse;
    RpcCallback callback;
  };

  void DoConnect();
  void SendAuth();
  void ReadHeader();
  void ReadBody(uint32_t size);
  void HandlePacket(const chirp::gateway::Packet& pkt);
  void DispatchRpcResponse(const chirp::gateway::Packet& pkt);
  void SendRpc(chirp::gateway::MsgID req_id, chirp::gateway::MsgID resp_id,
               const google::protobuf::Message& body, BodyParser parse, RpcCallback cb);
  void FailPending();
  void SendPacket(chirp::gateway::MsgID msg_id, int64_t seq,
                  const google::protobuf::Message& body);
  void SendHeartbeat();
  void ArmHeartbeat();
  void OnConnectionLost();
  void ScheduleReconnect();

  asio::io_context& io_;
  Options options_;
  InjectHandler on_inject_;
  EventHandler on_event_;
  // Serializes every entry point and every socket/timer handler; also keeps
  // the implicit is_open() checks inside async ops from racing a close().
  asio::strand<asio::any_io_executor> strand_;
  asio::ip::tcp::socket socket_;
  asio::steady_timer timer_;  // reconnect delay and heartbeat, one at a time
  std::array<uint8_t, 4> header_{};
  std::string body_;
  bool connected_ = false;
  bool stopping_ = false;
  int64_t heartbeat_seq_ = 0;
  int64_t rpc_seq_ = 0;  // independent of heartbeat_seq_; identifies in-flight RPCs
  std::map<int64_t, PendingRpc> pending_;  // Packet.sequence -> in-flight RPC
  int heartbeat_interval_seconds_ = 30;  // reassigned by the hub on auth
};

}  // namespace chirp::chat

#endif  // CHIRP_CHAT_SERVER_GATEWAY_PEER_H_
