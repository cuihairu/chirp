#include "chat_peer_link.h"

#include <chrono>
#include <utility>

#include "common/logger.h"
#include "network/byte_order.h"
#include "network/protobuf_framing.h"

namespace chirp::network {

namespace {

constexpr uint32_t kMaxFrameBytes = 4u * 1024u * 1024u;  // same cap as the hub plane

}  // namespace

std::shared_ptr<ChatPeerLink> ChatPeerLink::Create(asio::io_context& io, Options options,
                                                   RegisteredHandler on_registered,
                                                   ChannelMessageHandler on_channel_message,
                                                   InjectHandler on_inject,
                                                   LostHandler on_lost) {
  return std::shared_ptr<ChatPeerLink>(new ChatPeerLink(
      io, std::move(options), std::move(on_registered), std::move(on_channel_message),
      std::move(on_inject), std::move(on_lost), PrivateTag{}));
}

ChatPeerLink::ChatPeerLink(asio::io_context& io, Options options, RegisteredHandler on_registered,
                           ChannelMessageHandler on_channel_message, InjectHandler on_inject,
                           LostHandler on_lost, PrivateTag)
    : io_(io),
      options_(std::move(options)),
      on_registered_(std::move(on_registered)),
      on_channel_message_(std::move(on_channel_message)),
      on_inject_(std::move(on_inject)),
      on_lost_(std::move(on_lost)),
      strand_(asio::make_strand(io)),
      socket_(io),
      timer_(io) {}

ChatPeerLink::~ChatPeerLink() {
  // Deliberately not Stop(): the posted cleanup captures shared_from_this(),
  // which throws bad_weak_ptr while destructing. Reaching the destructor at
  // all means no async handler holds a shared_ptr anymore (they keep the
  // link alive), so members simply tear themselves down.
  stopping_ = true;
}

void ChatPeerLink::Start() {
  auto self = shared_from_this();
  asio::post(strand_, [self] {
    if (self->stopping_) {
      return;
    }
    self->DoConnect();
  });
}

void ChatPeerLink::Stop() {
  auto self = shared_from_this();
  asio::post(strand_, [self] {
    if (self->stopping_) {
      return;
    }
    self->stopping_ = true;
    self->registered_ = false;  // sends after Stop must refuse
    self->timer_.cancel();
    asio::error_code ec;
    self->socket_.close(ec);
  });
}

bool ChatPeerLink::SendChannelMessage(const chirp::gateway::ChannelMessageNotify& notify) {
  auto self = shared_from_this();
  // Serialize before posting: notify may be a stack temporary that dies
  // before the strand lambda runs.
  const std::string body = notify.SerializeAsString();
  asio::post(strand_, [self, body] {
    if (!self->registered_ || self->stopping_) {
      return;
    }
    self->SendRawPacket(chirp::gateway::CHANNEL_MESSAGE_NOTIFY, ++self->uplink_seq_, body);
  });
  return registered_;
}

bool ChatPeerLink::SendInject(const chirp::gateway::PeerInjectMessageNotify& notify) {
  auto self = shared_from_this();
  const std::string body = notify.SerializeAsString();
  asio::post(strand_, [self, body] {
    if (!self->registered_ || self->stopping_) {
      return;
    }
    self->SendRawPacket(chirp::gateway::PEER_INJECT_MESSAGE_NOTIFY, ++self->uplink_seq_, body);
  });
  return registered_;
}

void ChatPeerLink::DoConnect() {
  auto self = shared_from_this();
  auto resolver = std::make_shared<asio::ip::tcp::resolver>(io_);
  resolver->async_resolve(
      options_.host, std::to_string(options_.port),
      asio::bind_executor(strand_, [self, resolver](const std::error_code& ec,
                                                    asio::ip::tcp::resolver::results_type results) {
        if (ec) {
          chirp::common::Logger::Instance().Warn("chat peer resolve failed: " + ec.message());
          self->ScheduleReconnect();
          return;
        }
        asio::async_connect(
            self->socket_, results,
            asio::bind_executor(self->strand_, [self](const std::error_code& ec,
                                                      const asio::ip::tcp::endpoint&) {
              if (self->stopping_) return;
              if (ec) {
                chirp::common::Logger::Instance().Warn(
                    "chat peer connect failed: " + ec.message());
                self->ScheduleReconnect();
                return;
              }
              self->SendRegister();
              self->ReadHeader();
            }));
      }));
}

void ChatPeerLink::SendRegister() {
  chirp::gateway::PeerRegisterReq req;
  req.set_service_id(options_.service_id);
  req.set_service_secret(options_.secret);
  req.set_protocol_version(options_.protocol_version);
  req.set_game_id(options_.game_id);
  for (auto feature : options_.supported_features) {
    req.add_supported_features(feature);
  }
  SendPacket(chirp::gateway::PEER_REGISTER_REQ, 0, req);
}

void ChatPeerLink::ReadHeader() {
  auto self = shared_from_this();
  asio::async_read(
      socket_, asio::buffer(header_),
      asio::bind_executor(strand_, [self](const std::error_code& ec, std::size_t) {
        if (self->stopping_ || ec) {
          self->OnConnectionLost();
          return;
        }
        const uint32_t size = chirp::network::ReadU32BE(self->header_.data());
        if (size == 0 || size > kMaxFrameBytes) {
          chirp::common::Logger::Instance().Warn("chat peer sent an invalid frame size");
          self->OnConnectionLost();
          return;
        }
        self->ReadBody(size);
      }));
}

void ChatPeerLink::ReadBody(uint32_t size) {
  auto self = shared_from_this();
  body_.resize(size);
  asio::async_read(
      socket_, asio::buffer(body_.data(), body_.size()),
      asio::bind_executor(strand_, [self](const std::error_code& ec, std::size_t) {
        if (self->stopping_ || ec) {
          self->OnConnectionLost();
          return;
        }
        chirp::gateway::Packet pkt;
        if (!pkt.ParseFromArray(self->body_.data(), static_cast<int>(self->body_.size()))) {
          chirp::common::Logger::Instance().Warn("failed to parse Packet from chat peer hub");
          self->OnConnectionLost();
          return;
        }
        self->HandlePacket(pkt);
        if (!self->stopping_) {
          self->ReadHeader();
        }
      }));
}

void ChatPeerLink::HandlePacket(const chirp::gateway::Packet& pkt) {
  switch (pkt.msg_id()) {
  case chirp::gateway::PEER_REGISTER_RESP: {
    chirp::gateway::PeerRegisterResp resp;
    if (!resp.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
      chirp::common::Logger::Instance().Warn("failed to parse PeerRegisterResp");
      OnConnectionLost();
      return;
    }
    if (resp.code() != chirp::common::OK) {
      chirp::common::Logger::Instance().Warn(
          "chat peer registration rejected for service " + options_.service_id + " code=" +
          std::to_string(resp.code()) + " min_version=" + std::to_string(resp.min_version()));
      OnConnectionLost();
      return;
    }
    registered_ = true;
    heartbeat_interval_seconds_ =
        resp.heartbeat_interval_seconds() > 0 ? resp.heartbeat_interval_seconds()
                                              : options_.heartbeat_interval_seconds;
    // RepeatedField stores proto enums as int: convert explicitly.
    std::vector<chirp::gateway::PeerCapability> features;
    features.reserve(resp.supported_features_size());
    for (auto feature : resp.supported_features()) {
      features.push_back(static_cast<chirp::gateway::PeerCapability>(feature));
    }
    chirp::common::Logger::Instance().Info(
        "registered with chat peer hub as service " + options_.service_id + " game=" +
        options_.game_id + " version=" + std::to_string(resp.protocol_version()) +
        " features=" + std::to_string(features.size()) + " heartbeat=" +
        std::to_string(heartbeat_interval_seconds_) + "s");
    if (on_registered_) {
      on_registered_(resp.protocol_version(), features);
    }
    ArmHeartbeat();
    break;
  }
  case chirp::gateway::CHANNEL_MESSAGE_NOTIFY: {
    chirp::gateway::ChannelMessageNotify notify;
    if (!notify.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
      chirp::common::Logger::Instance().Warn("failed to parse ChannelMessageNotify");
      break;
    }
    if (on_channel_message_) {
      on_channel_message_(notify);
    }
    break;
  }
  case chirp::gateway::PEER_INJECT_MESSAGE_NOTIFY: {
    chirp::gateway::PeerInjectMessageNotify notify;
    if (!notify.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
      chirp::common::Logger::Instance().Warn("failed to parse PeerInjectMessageNotify");
      break;
    }
    if (on_inject_) {
      on_inject_(notify);
    }
    break;
  }
  case chirp::gateway::HEARTBEAT_PONG:
    break;  // liveness is enforced by the hub; nothing to do
  default:
    // The peer protocol carries no RPC responses: anything else is a frame
    // this link does not understand (future version skew). Drop it loudly.
    chirp::common::Logger::Instance().Warn(
        "chat peer hub sent an unexpected msg_id=" + std::to_string(pkt.msg_id()));
    break;
  }
}

void ChatPeerLink::SendPacket(chirp::gateway::MsgID msg_id, int64_t seq,
                              const google::protobuf::Message& body) {
  SendRawPacket(msg_id, seq, body.SerializeAsString());
}

void ChatPeerLink::SendRawPacket(chirp::gateway::MsgID msg_id, int64_t seq,
                                 const std::string& body) {
  chirp::gateway::Packet pkt;
  pkt.set_msg_id(msg_id);
  pkt.set_sequence(seq);
  pkt.set_body(body);
  auto framed = chirp::network::ProtobufFraming::Encode(pkt);
  asio::error_code ec;
  // Best-effort write: control frames are tiny, and the async read loop is
  // the liveness detector - a dead connection fails its next read and funnels
  // into OnConnectionLost from there.
  asio::write(socket_, asio::buffer(framed), ec);
}

void ChatPeerLink::SendHeartbeat() {
  chirp::gateway::HeartbeatPing ping;
  ping.set_timestamp(0);
  SendPacket(chirp::gateway::HEARTBEAT_PING, ++heartbeat_seq_, ping);
}

void ChatPeerLink::ArmHeartbeat() {
  auto self = shared_from_this();
  timer_.cancel();
  timer_.expires_after(std::chrono::seconds(heartbeat_interval_seconds_));
  timer_.async_wait(
      asio::bind_executor(strand_, [self](const std::error_code& ec) {
        if (ec || self->stopping_ || !self->registered_) return;  // re-armed or shutting down
        self->SendHeartbeat();
        self->ArmHeartbeat();
      }));
}

void ChatPeerLink::OnConnectionLost() {
  if (stopping_) return;
  const bool was_registered = registered_;
  registered_ = false;
  timer_.cancel();
  asio::error_code ec;
  socket_.close(ec);
  if (was_registered && on_lost_) {
    on_lost_();
  }
  ScheduleReconnect();
}

void ChatPeerLink::ScheduleReconnect() {
  if (stopping_) return;
  auto self = shared_from_this();
  timer_.expires_after(std::chrono::seconds(options_.reconnect_delay_seconds));
  timer_.async_wait([self](const std::error_code& ec) {
    if (ec || self->stopping_) return;
    self->DoConnect();
  });
}

}  // namespace chirp::network
