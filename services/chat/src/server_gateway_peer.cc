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
                                                             InjectHandler on_inject,
                                                             EventHandler on_event) {
  return std::shared_ptr<ServerGatewayPeer>(new ServerGatewayPeer(
      io, std::move(options), std::move(on_inject), std::move(on_event), PrivateTag{}));
}

ServerGatewayPeer::ServerGatewayPeer(asio::io_context& io, Options options,
                                     InjectHandler on_inject, EventHandler on_event,
                                     PrivateTag)
    : io_(io),
      options_(std::move(options)),
      on_inject_(std::move(on_inject)),
      on_event_(std::move(on_event)),
      socket_(io),
      timer_(io) {}

ServerGatewayPeer::~ServerGatewayPeer() {
  // Deliberately not Stop(): the posted cleanup captures shared_from_this(),
  // which throws bad_weak_ptr while destructing. Reaching the destructor at
  // all means no async handler holds a shared_ptr anymore (they keep the
  // peer alive), so members simply tear themselves down: the timer cancels
  // and the socket closes.
  stopping_ = true;
}

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
    self->FailPending();
    asio::error_code ec;
    self->socket_.close(ec);
  });
}

void ServerGatewayPeer::SendInject(const chirp::server_gateway::MessageInjectRequest& req,
                                   RpcCallback cb) {
  SendRpc(chirp::gateway::INJECT_MESSAGE_REQ, chirp::gateway::INJECT_MESSAGE_RESP, req,
          [](const std::string& body) {
            chirp::server_gateway::MessageInjectResponse resp;
            return resp.ParseFromString(body) ? resp.code() : chirp::common::INTERNAL_ERROR;
          },
          std::move(cb));
}

void ServerGatewayPeer::SendEventPublish(
    const chirp::server_gateway::EventPublishRequest& req, RpcCallback cb) {
  SendRpc(chirp::gateway::EVENT_PUBLISH_REQ, chirp::gateway::EVENT_PUBLISH_RESP, req,
          [](const std::string& body) {
            chirp::server_gateway::EventPublishResponse resp;
            return resp.ParseFromString(body) ? resp.code() : chirp::common::INTERNAL_ERROR;
          },
          std::move(cb));
}

void ServerGatewayPeer::SendEventAck(const chirp::server_gateway::EventAckRequest& req,
                                     RpcCallback cb) {
  SendRpc(chirp::gateway::EVENT_ACK_REQ, chirp::gateway::EVENT_ACK_RESP, req,
          [](const std::string& body) {
            chirp::server_gateway::EventAckResponse resp;
            return resp.ParseFromString(body) ? resp.code() : chirp::common::INTERNAL_ERROR;
          },
          std::move(cb));
}

void ServerGatewayPeer::SendRpc(chirp::gateway::MsgID req_id, chirp::gateway::MsgID resp_id,
                                const google::protobuf::Message& body, BodyParser parse,
                                RpcCallback cb) {
  if (stopping_ || !connected_) {
    // Fail fast without queueing: this request raced with (or predates) a
    // live connection, so the caller retries on its own schedule.
    cb(chirp::common::SERVER_UNAVAILABLE);
    return;
  }
  const int64_t seq = ++rpc_seq_;
  pending_[seq] = {resp_id, std::move(parse), std::move(cb)};
  SendPacket(req_id, seq, body);
}

void ServerGatewayPeer::DispatchRpcResponse(const chirp::gateway::Packet& pkt) {
  auto it = pending_.find(pkt.sequence());
  if (it == pending_.end() || it->second.resp_id != pkt.msg_id()) {
    // Nobody is waiting on this (sequence, msg id) pair: a response to an RPC
    // that already failed, a duplicate, or the hub answering a sequence with
    // the wrong message id. Nothing to dispatch it to; log and move on.
    chirp::common::Logger::Instance().Warn(
        "server-gateway response does not match a pending rpc (seq=" +
        std::to_string(pkt.sequence()) + ")");
    return;
  }
  PendingRpc entry = std::move(it->second);
  pending_.erase(it);
  entry.callback(entry.parse(pkt.body()));
}

void ServerGatewayPeer::FailPending() {
  if (pending_.empty()) {
    return;
  }
  // The connection dropped (or the peer stopped) with RPCs in flight; no
  // response will ever arrive for them.
  auto stale = std::move(pending_);
  pending_.clear();
  for (auto& seq_entry : stale) {
    seq_entry.second.callback(chirp::common::SERVER_UNAVAILABLE);
  }
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
  case chirp::gateway::EVENT_DELIVER_NOTIFY: {
    if (!on_event_) {
      break;  // no event consumer configured; ignore
    }
    chirp::server_gateway::EventDeliverNotify notify;
    if (!notify.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
      chirp::common::Logger::Instance().Warn("failed to parse EventDeliverNotify");
      break;
    }
    on_event_(notify);
    break;
  }
  case chirp::gateway::INJECT_MESSAGE_RESP:
  case chirp::gateway::EVENT_PUBLISH_RESP:
  case chirp::gateway::EVENT_ACK_RESP:
    DispatchRpcResponse(pkt);
    break;
  case chirp::gateway::SERVER_HEARTBEAT_PONG:
    break;  // liveness is enforced by the hub; nothing to do
  default:
    break;  // unknown frames are ignored
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
  FailPending();
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
