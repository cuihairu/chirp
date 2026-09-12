#include "server_gateway_peer.h"

#include <chrono>
#include <utility>

#include "logger.h"
#include "network/byte_order.h"
#include "network/protobuf_framing.h"
#include "proto/common.pb.h"

namespace chirp::chat {

namespace {

int64_t NowMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

constexpr uint32_t kMaxFrameBytes = 4u * 1024u * 1024u;  // same cap as the hub plane

}  // namespace

std::shared_ptr<ServerGatewayPeer> ServerGatewayPeer::Create(asio::io_context& io,
                                                             Options options,
                                                             InjectHandler on_inject) {
  return std::shared_ptr<ServerGatewayPeer>(
      new ServerGatewayPeer(io, std::move(options), std::move(on_inject), PrivateTag{}));
}

ServerGatewayPeer::ServerGatewayPeer(asio::io_context& io, Options options,
                                     InjectHandler on_inject, PrivateTag)
    : io_(io),
      options_(std::move(options)),
      on_inject_(std::move(on_inject)),
      socket_(io),
      timer_(io) {}

ServerGatewayPeer::~ServerGatewayPeer() { Stop(); }

void ServerGatewayPeer::Start() {
  if (stopping_) {
    return;
  }
  DoConnect();
}

void ServerGatewayPeer::Stop() {
  if (stopping_) {
    return;
  }
  stopping_ = true;
  asio::post(io_, [self = shared_from_this()] {
    self->timer_.cancel();
    asio::error_code ec;
    self->socket_.close(ec);
  });
}

void ServerGatewayPeer::DoConnect() {
  auto self = shared_from_this();
  auto resolver = std::make_shared<asio::ip::tcp::resolver>(io_);
  resolver->async_resolve(
      options_.host, std::to_string(options_.port),
      [self, resolver](const std::error_code& ec,
                       asio::ip::tcp::resolver::results_type results) {
        // No stopping_ guard here: if stopped, the connect below completes on
        // a closed socket with an error and its handler returns early.
        if (ec) {
          chirp::common::Logger::Instance().Warn("server-gateway resolve failed: " + ec.message());
          self->ScheduleReconnect();
          return;
        }
        asio::async_connect(
            self->socket_, results,
            [self](const std::error_code& ec, const asio::ip::tcp::endpoint&) {
              if (self->stopping_) return;
              if (ec) {
                chirp::common::Logger::Instance().Warn(
                    "server-gateway connect failed: " + ec.message());
                self->ScheduleReconnect();
                return;
              }
              self->SendAuth();
              self->ReadHeader();
            });
      });
}

void ServerGatewayPeer::SendAuth() {
  chirp::server_gateway::ServerAuthRequest req;
  req.set_service_id(options_.service_id);
  req.set_secret(options_.secret);
  req.set_protocol_version(1);
  SendPacket(chirp::gateway::SERVER_AUTH_REQ, 0, req);
}

void ServerGatewayPeer::ReadHeader() {
  auto self = shared_from_this();
  asio::async_read(
      socket_, asio::buffer(header_),
      [self](const std::error_code& ec, std::size_t) {
        if (self->stopping_ || ec) {
          self->OnConnectionLost();
          return;
        }
        const uint32_t size = chirp::network::ReadU32BE(self->header_.data());
        if (size == 0 || size > kMaxFrameBytes) {
          chirp::common::Logger::Instance().Warn("server-gateway sent an invalid frame size");
          self->OnConnectionLost();
          return;
        }
        self->ReadBody(size);
      });
}

void ServerGatewayPeer::ReadBody(uint32_t size) {
  auto self = shared_from_this();
  body_.resize(size);
  asio::async_read(
      socket_, asio::buffer(body_.data(), body_.size()),
      [self](const std::error_code& ec, std::size_t) {
        if (self->stopping_ || ec) {
          self->OnConnectionLost();
          return;
        }
        chirp::gateway::Packet pkt;
        if (!pkt.ParseFromArray(self->body_.data(), static_cast<int>(self->body_.size()))) {
          chirp::common::Logger::Instance().Warn("failed to parse Packet from server-gateway");
          self->OnConnectionLost();
          return;
        }
        self->HandlePacket(pkt);
        if (!self->stopping_) {
          self->ReadHeader();
        }
      });
}

void ServerGatewayPeer::HandlePacket(const chirp::gateway::Packet& pkt) {
  switch (pkt.msg_id()) {
  case chirp::gateway::SERVER_AUTH_RESP: {
    chirp::server_gateway::ServerAuthResponse resp;
    if (!resp.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
      chirp::common::Logger::Instance().Warn("failed to parse ServerAuthResponse");
      OnConnectionLost();
      return;
    }
    if (resp.code() != chirp::common::OK) {
      chirp::common::Logger::Instance().Warn(
          "server-gateway auth rejected for service " + options_.service_id);
      OnConnectionLost();
      return;
    }
    connected_ = true;
    heartbeat_interval_seconds_ =
        resp.heartbeat_interval_seconds() > 0
            ? resp.heartbeat_interval_seconds()
            : options_.heartbeat_interval_seconds;
    chirp::common::Logger::Instance().Info(
        "connected to server-gateway as service " + options_.service_id +
        " heartbeat=" + std::to_string(heartbeat_interval_seconds_) + "s");
    ArmHeartbeat();
    break;
  }
  case chirp::gateway::INJECT_MESSAGE_NOTIFY: {
    chirp::server_gateway::InjectMessageNotify notify;
    if (!notify.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
      chirp::common::Logger::Instance().Warn("failed to parse InjectMessageNotify");
      break;
    }
    if (on_inject_) {
      on_inject_(notify);
    }
    break;
  }
  case chirp::gateway::SERVER_HEARTBEAT_PONG:
    break;  // liveness is enforced by the hub; nothing to do
  default:
    break;  // events and unknown frames are not consumed by chat yet
  }
}

void ServerGatewayPeer::SendPacket(chirp::gateway::MsgID msg_id, int64_t seq,
                                   const google::protobuf::Message& body) {
  chirp::gateway::Packet pkt;
  pkt.set_msg_id(msg_id);
  pkt.set_sequence(seq);
  pkt.set_body(body.SerializeAsString());
  auto framed = chirp::network::ProtobufFraming::Encode(pkt);
  asio::error_code ec;
  // Best-effort write: control frames are tiny, and the async read loop is
  // the liveness detector - a dead connection fails its next read and funnels
  // into OnConnectionLost from there.
  asio::write(socket_, asio::buffer(framed), ec);
}

void ServerGatewayPeer::SendHeartbeat() {
  chirp::server_gateway::ServerHeartbeatPing ping;
  ping.set_client_time_ms(NowMs());
  SendPacket(chirp::gateway::SERVER_HEARTBEAT_PING, ++heartbeat_seq_, ping);
}

void ServerGatewayPeer::ArmHeartbeat() {
  auto self = shared_from_this();
  timer_.cancel();
  timer_.expires_after(std::chrono::seconds(heartbeat_interval_seconds_));
  timer_.async_wait([self](const std::error_code& ec) {
    if (ec || self->stopping_ || !self->connected_) return;  // re-armed or shutting down
    self->SendHeartbeat();
    self->ArmHeartbeat();
  });
}

void ServerGatewayPeer::OnConnectionLost() {
  if (stopping_) return;
  connected_ = false;
  timer_.cancel();
  asio::error_code ec;
  socket_.close(ec);
  ScheduleReconnect();
}

void ServerGatewayPeer::ScheduleReconnect() {
  if (stopping_) return;
  auto self = shared_from_this();
  timer_.expires_after(std::chrono::seconds(options_.reconnect_delay_seconds));
  timer_.async_wait([self](const std::error_code& ec) {
    if (ec || self->stopping_) return;
    self->DoConnect();
  });
}

}  // namespace chirp::chat
