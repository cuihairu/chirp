#include "chat_peer_hub.h"

#include <algorithm>
#include <chrono>
#include <utility>

#include "common/logger.h"
#include "network/byte_order.h"
#include "network/protobuf_framing.h"

namespace chirp::network {

namespace {

constexpr uint32_t kMaxFrameBytes = 4u * 1024u * 1024u;  // same cap as the peer plane

}  // namespace

std::shared_ptr<ChatPeerHub> ChatPeerHub::Create(asio::io_context& io, Options options,
                                                 PeerRegisteredHandler on_registered,
                                                 PeerDroppedHandler on_dropped,
                                                 ChannelMessageHandler on_channel_message) {
  return std::shared_ptr<ChatPeerHub>(new ChatPeerHub(
      io, std::move(options), std::move(on_registered), std::move(on_dropped),
      std::move(on_channel_message), PrivateTag{}));
}

ChatPeerHub::ChatPeerHub(asio::io_context& io, Options options,
                         PeerRegisteredHandler on_registered, PeerDroppedHandler on_dropped,
                         ChannelMessageHandler on_channel_message, PrivateTag)
    : options_(std::move(options)),
      io_(io),
      on_registered_(std::move(on_registered)),
      on_dropped_(std::move(on_dropped)),
      on_channel_message_(std::move(on_channel_message)),
      acceptor_(io) {}

ChatPeerHub::~ChatPeerHub() {
  // Deliberately not Stop(): reaching the destructor means no async handler
  // holds a shared_ptr anymore (every conn handler captures the hub's
  // shared_ptr, which keeps it - and itself - alive), so members simply tear
  // themselves down.
}

uint16_t ChatPeerHub::port() const {
  asio::error_code ec;
  const auto endpoint = acceptor_.local_endpoint(ec);
  return ec ? 0 : endpoint.port();
}

void ChatPeerHub::Stop() {
  // Everything this touches (acceptor, peer tables, stopping_) lives on the
  // hub's io thread, so the teardown must run there. Posting also keeps the
  // hub object alive until the cleanup handler itself completes.
  auto self = shared_from_this();
  asio::post(io_, [self] { self->DoStop(); });
}

void ChatPeerHub::DoStop() {
  stopping_ = true;
  asio::error_code ec;
  acceptor_.close(ec);
  // Copy first: Close() erases from peers_ while we iterate.
  std::vector<std::shared_ptr<PeerConn>> dropped;
  for (auto& [id, conn] : peers_) {
    dropped.push_back(conn);
  }
  for (auto& conn : peers_unregistered_) {
    dropped.push_back(conn);
  }
  for (auto& conn : dropped) {
    conn->Close(shared_from_this(), "hub stopped");
  }
  peers_.clear();
  peers_unregistered_.clear();
}

void ChatPeerHub::Start() {
  const asio::ip::tcp::endpoint endpoint(asio::ip::tcp::v4(), options_.port);
  asio::error_code ec;
  acceptor_.open(endpoint.protocol(), ec);
  acceptor_.set_option(asio::socket_base::reuse_address(true), ec);
  acceptor_.bind(endpoint, ec);
  if (ec) {
    chirp::common::Logger::Instance().Error(
        "chat peer hub cannot bind port " + std::to_string(options_.port) + ": " + ec.message());
    return;
  }
  acceptor_.listen(asio::socket_base::max_listen_connections, ec);
  if (ec) {
    chirp::common::Logger::Instance().Error("chat peer hub cannot listen: " + ec.message());
    return;
  }
  chirp::common::Logger::Instance().Info(
      "chat peer hub listening on port " + std::to_string(options_.port) + " peers=" +
      std::to_string(options_.allowed_peers.size()) +
      (options_.allow_unknown_peers ? " open-registration" : " whitelist-only"));
  DoAccept();
}

bool ChatPeerHub::SendInject(const std::string& service_id,
                             const chirp::gateway::PeerInjectMessageNotify& notify) {
  auto it = peers_.find(service_id);
  if (it == peers_.end()) {
    return false;
  }
  it->second->SendRawPacket(chirp::gateway::PEER_INJECT_MESSAGE_NOTIFY,
                            ++it->second->uplink_seq, notify.SerializeAsString());
  return true;
}

std::string ChatPeerHub::game_id_for(const std::string& service_id) const {
  auto it = peers_.find(service_id);
  return it == peers_.end() ? std::string() : it->second->game_id;
}

std::string ChatPeerHub::service_id_for_game(const std::string& game_id) const {
  // The peer table is service-count sized; a linear scan is fine.
  for (const auto& [service_id, conn] : peers_) {
    if (conn && conn->registered && conn->game_id == game_id) {
      return service_id;
    }
  }
  return "";
}

void ChatPeerHub::DoAccept() {
  auto self = shared_from_this();
  acceptor_.async_accept([self](const std::error_code& ec, asio::ip::tcp::socket socket) {
    if (self->stopping_) {
      return;
    }
    if (!ec) {
      auto conn = std::make_shared<PeerConn>(std::move(socket));
      self->peers_unregistered_.push_back(conn);
      self->DoAccept();
      // Unregistered connections live under the same idle window: a peer
      // that connects but never registers is dropped like a silent one.
      conn->ArmIdleTimer(self);
      conn->ReadHeader(self);
      return;
    }
    if (ec != asio::error::operation_aborted) {
      chirp::common::Logger::Instance().Warn("chat peer hub accept failed: " + ec.message());
      self->DoAccept();
    }
  });
}

void ChatPeerHub::HandleRegister(const std::shared_ptr<PeerConn>& conn,
                                 const chirp::gateway::PeerRegisterReq& req) {
  chirp::gateway::PeerRegisterResp resp;

  // Whitelist: the map maps service_id -> secret. Unknown peers only pass in
  // open-registration mode.
  auto allowed = options_.allowed_peers.find(req.service_id());
  if (allowed == options_.allowed_peers.end()) {
    if (!options_.allow_unknown_peers) {
      chirp::common::Logger::Instance().Warn(
          "chat peer registration rejected (not whitelisted): " + req.service_id());
      resp.set_code(chirp::common::AUTH_FAILED);
      resp.set_min_version(options_.min_peer_version);
      conn->SendRawPacket(chirp::gateway::PEER_REGISTER_RESP, 0, resp.SerializeAsString());
      conn->Close(shared_from_this(), "rejected");
      return;
    }
  } else if (allowed->second != req.service_secret()) {
    chirp::common::Logger::Instance().Warn(
        "chat peer registration rejected (bad secret): " + req.service_id());
    resp.set_code(chirp::common::AUTH_FAILED);
    resp.set_min_version(options_.min_peer_version);
    conn->SendRawPacket(chirp::gateway::PEER_REGISTER_RESP, 0, resp.SerializeAsString());
    conn->Close(shared_from_this(), "rejected");
    return;
  }

  // Version gate: below the minimum gets VERSION_MISMATCH plus the hub's
  // minimum so the spoke can decide to upgrade.
  if (req.protocol_version() < options_.min_peer_version) {
    chirp::common::Logger::Instance().Warn(
        "chat peer registration rejected (version " + std::to_string(req.protocol_version()) +
        " < min " + std::to_string(options_.min_peer_version) + "): " + req.service_id());
    resp.set_code(chirp::common::VERSION_MISMATCH);
    resp.set_protocol_version(kPeerProtocolVersion);
    resp.set_min_version(options_.min_peer_version);
    conn->SendRawPacket(chirp::gateway::PEER_REGISTER_RESP, 0, resp.SerializeAsString());
    conn->Close(shared_from_this(), "rejected");
    return;
  }

  // Game id must be namespace-safe: the hub builds "<game_id>:<channel_id>"
  // keys, and a ':' inside game_id would make that routing ambiguous.
  if (req.game_id().empty() || req.game_id().find(':') != std::string::npos) {
    chirp::common::Logger::Instance().Warn(
        "chat peer registration rejected (bad game_id): " + req.service_id());
    resp.set_code(chirp::common::INVALID_PARAM);
    conn->SendRawPacket(chirp::gateway::PEER_REGISTER_RESP, 0, resp.SerializeAsString());
    conn->Close(shared_from_this(), "rejected");
    return;
  }

  // Same service_id registering again displaces the old connection. Close()
  // performs the erase + on_dropped_ report while the entry still belongs to
  // the old conn.
  auto existing = peers_.find(req.service_id());
  if (existing != peers_.end()) {
    existing->second->Close(shared_from_this(), "displaced");
  }

  const int32_t negotiated = std::min(kPeerProtocolVersion, req.protocol_version());
  conn->service_id = req.service_id();
  conn->game_id = req.game_id();
  // RepeatedField stores proto enums as int: convert explicitly, the range
  // constructor would need a narrowing no compiler will do for us.
  conn->features.reserve(req.supported_features_size());
  for (auto feature : req.supported_features()) {
    conn->features.push_back(static_cast<chirp::gateway::PeerCapability>(feature));
  }
  conn->registered = true;
  peers_[conn->service_id] = conn;

  resp.set_code(chirp::common::OK);
  resp.set_protocol_version(negotiated);
  resp.set_min_version(options_.min_peer_version);
  resp.set_heartbeat_interval_seconds(options_.heartbeat_interval_seconds);
  for (auto feature : conn->features) {
    resp.add_supported_features(feature);
  }
  conn->SendRawPacket(chirp::gateway::PEER_REGISTER_RESP, 0, resp.SerializeAsString());
  conn->ArmIdleTimer(shared_from_this());

  chirp::common::Logger::Instance().Info(
      "chat peer registered: " + conn->service_id + " game=" + conn->game_id + " version=" +
      std::to_string(negotiated));
  if (on_registered_) {
    on_registered_(conn->service_id, conn->game_id, negotiated, conn->features);
  }
}

ChatPeerHub::PeerConn::PeerConn(asio::ip::tcp::socket s)
    : socket(std::move(s)), idle_timer(socket.get_executor()) {}

ChatPeerHub::PeerConn::~PeerConn() {
  asio::error_code ec;
  socket.close(ec);
}

void ChatPeerHub::PeerConn::ReadHeader(const std::shared_ptr<ChatPeerHub>& hub) {
  auto self = shared_from_this();
  asio::async_read(
      socket, asio::buffer(header),
      [self, hub](const std::error_code& ec, std::size_t) {
        if (self->closing) return;
        if (ec) {
          self->Close(hub, "lost");
          return;
        }
        const uint32_t size = chirp::network::ReadU32BE(self->header.data());
        if (size == 0 || size > kMaxFrameBytes) {
          chirp::common::Logger::Instance().Warn("chat peer sent an invalid frame size");
          self->Close(hub, "bad frame");
          return;
        }
        self->ReadBody(size, hub);
      });
}

void ChatPeerHub::PeerConn::ReadBody(uint32_t size, const std::shared_ptr<ChatPeerHub>& hub) {
  auto self = shared_from_this();
  body.resize(size);
  asio::async_read(
      socket, asio::buffer(body.data(), body.size()),
      [self, hub](const std::error_code& ec, std::size_t) {
        if (self->closing) return;
        if (ec) {
          self->Close(hub, "lost");
          return;
        }
        chirp::gateway::Packet pkt;
        if (!pkt.ParseFromArray(self->body.data(), static_cast<int>(self->body.size()))) {
          chirp::common::Logger::Instance().Warn("failed to parse Packet from chat peer");
          self->Close(hub, "bad frame");
          return;
        }
        self->HandlePacket(pkt, hub);
        if (!self->closing) {
          self->ReadHeader(hub);
        }
      });
}

void ChatPeerHub::PeerConn::HandlePacket(const chirp::gateway::Packet& pkt,
                                         const std::shared_ptr<ChatPeerHub>& hub) {
  switch (pkt.msg_id()) {
  case chirp::gateway::PEER_REGISTER_REQ: {
    if (registered) {
      // A second registration on a live connection is a protocol violation.
      chirp::common::Logger::Instance().Warn(
          "chat peer " + service_id + " re-registered on a live connection");
      Close(hub, "protocol error");
      return;
    }
    chirp::gateway::PeerRegisterReq req;
    if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
      chirp::common::Logger::Instance().Warn("failed to parse PeerRegisterReq");
      Close(hub, "bad frame");
      return;
    }
    hub->HandleRegister(shared_from_this(), req);
    break;
  }
  case chirp::gateway::CHANNEL_MESSAGE_NOTIFY: {
    if (!registered) {
      Close(hub, "protocol error");
      return;
    }
    chirp::gateway::ChannelMessageNotify notify;
    if (!notify.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
      chirp::common::Logger::Instance().Warn("failed to parse ChannelMessageNotify");
      break;
    }
    ArmIdleTimer(hub);  // any traffic proves liveness
    if (hub->on_channel_message_) {
      hub->on_channel_message_(service_id, notify);
    }
    break;
  }
  case chirp::gateway::HEARTBEAT_PING: {
    if (!registered) {
      Close(hub, "protocol error");
      return;
    }
    chirp::gateway::HeartbeatPong pong;
    pong.set_timestamp(0);
    pong.set_server_time(pkt.sequence());  // echo for diagnostics
    SendRawPacket(chirp::gateway::HEARTBEAT_PONG, pkt.sequence(), pong.SerializeAsString());
    ArmIdleTimer(hub);
    break;
  }
  default:
    if (!registered) {
      Close(hub, "protocol error");
      return;
    }
    chirp::common::Logger::Instance().Warn(
        "chat peer sent an unexpected msg_id=" + std::to_string(pkt.msg_id()));
    break;
  }
}

void ChatPeerHub::PeerConn::ArmIdleTimer(const std::shared_ptr<ChatPeerHub>& hub) {
  auto self = shared_from_this();
  idle_timer.cancel();
  const int timeout = std::max(2, hub->options_.heartbeat_interval_seconds * 2);
  idle_timer.expires_after(std::chrono::seconds(timeout));
  idle_timer.async_wait([self, hub](const std::error_code& ec) {
    if (ec || self->closing) return;
    chirp::common::Logger::Instance().Warn(
        "chat peer " +
        (self->service_id.empty() ? std::string("(unregistered)") : self->service_id) +
        " silent past the idle window, dropping");
    self->Close(hub, "timeout");
  });
}

void ChatPeerHub::PeerConn::SendRawPacket(chirp::gateway::MsgID msg_id, int64_t seq,
                                          const std::string& body_str) {
  if (closing) {
    return;
  }
  chirp::gateway::Packet pkt;
  pkt.set_msg_id(msg_id);
  pkt.set_sequence(seq);
  pkt.set_body(body_str);
  auto framed = chirp::network::ProtobufFraming::Encode(pkt);
  asio::error_code ec;
  // Best-effort write: the async read loop is the liveness detector - a dead
  // connection fails its next read and funnels into Close from there.
  asio::write(socket, asio::buffer(framed), ec);
}

void ChatPeerHub::PeerConn::Close(const std::shared_ptr<ChatPeerHub>& hub,
                                  const std::string& reason) {
  if (closing) {
    return;
  }
  closing = true;
  idle_timer.cancel();
  asio::error_code ec;
  socket.close(ec);

  // Deregister: erase ourselves from the peer table (unless displaced - the
  // entry then belongs to a newer connection) and from the hub's
  // pre-registration holding pen.
  {
    auto& pen = hub->peers_unregistered_;
    for (auto it = pen.begin(); it != pen.end(); ++it) {
      if (it->get() == this) {
        pen.erase(it);
        break;
      }
    }
  }
  if (!registered) {
    return;  // never entered the peer table; nothing to report
  }
  auto it = hub->peers_.find(service_id);
  if (it != hub->peers_.end() && it->second.get() == this) {
    hub->peers_.erase(it);
    chirp::common::Logger::Instance().Info("chat peer dropped: " + service_id + " (" + reason + ")");
    if (hub->on_dropped_) {
      hub->on_dropped_(service_id, reason);
    }
  }
}

}  // namespace chirp::network
