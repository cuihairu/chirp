#include "peer_spoke.h"

#include "logger.h"
#include "network/protobuf_framing.h"

namespace chirp {
namespace chat {

PeerSpoke::PeerSpoke(asio::io_context& io, PeerSpokeConfig config)
    : io_(io), config_(std::move(config)) {}

PeerSpoke::~PeerSpoke() { Disconnect(); }

void PeerSpoke::Connect() {
  if (connected_) {
    return;
  }

  auto resolver = std::make_shared<asio::ip::tcp::resolver>(io_);
  resolver->async_resolve(
      config_.hub_host, std::to_string(config_.hub_port),
      [this, resolver](const std::error_code& ec,
                       asio::ip::tcp::resolver::results_type results) {
        if (ec) {
          common::Logger::Instance().Warn(
              "peer spoke: resolve failed: " + ec.message());
          return;
        }

        socket_ = std::make_unique<asio::ip::tcp::socket>(io_);
        asio::async_connect(
            *socket_, results,
            [this](const std::error_code& ec2,
                   const asio::ip::tcp::endpoint&) {
              if (ec2) {
                common::Logger::Instance().Warn(
                    "peer spoke: connect failed: " + ec2.message());
                return;
              }
              OnConnected();
            });
      });
}

void PeerSpoke::Disconnect() {
  if (socket_ && socket_->is_open()) {
    std::error_code ec;
    socket_->close(ec);
  }
  connected_ = false;
  registered_ = false;
}

bool PeerSpoke::IsConnected() const { return connected_ && registered_; }

void PeerSpoke::SendChannelMessage(const std::string& channel_id,
                                   const chat::ChatMessage& msg) {
  if (!IsConnected() || !socket_ || socket_->is_closed()) {
    return;
  }

  gateway::ChannelMessageNotify notify;
  notify.set_game_id(config_.game_id);
  notify.set_channel_id(channel_id);
  *notify.mutable_message() = msg;

  gateway::Packet pkt;
  pkt.set_msg_id(gateway::CHANNEL_MESSAGE_NOTIFY);
  pkt.set_sequence(0);
  pkt.set_body(notify.SerializeAsString());
  auto framed = network::ProtobufFraming::Encode(pkt);
  asio::error_code ec;
  asio::write(*socket_,
             asio::buffer(framed.data(), framed.size()), ec);
  if (ec) {
    common::Logger::Instance().Warn(
        "peer spoke: send channel message failed: " + ec.message());
    OnDisconnected();
  }
}

void PeerSpoke::SetConnectedCallback(ConnectedCallback cb) {
  on_connected_ = std::move(cb);
}

void PeerSpoke::SetDisconnectedCallback(DisconnectedCallback cb) {
  on_disconnected_ = std::move(cb);
}

void PeerSpoke::SetInjectCallback(InjectCallback cb) {
  on_inject_ = std::move(cb);
}

void PeerSpoke::OnConnected() {
  connected_ = true;
  common::Logger::Instance().Info(
      "peer spoke: connected to hub " + config_.hub_host + ":" +
      std::to_string(config_.hub_port));

  // 发送注册请求
  gateway::PeerRegisterReq req;
  req.set_service_id(config_.service_id);
  req.set_service_secret(config_.service_secret);
  req.set_protocol_version(config_.protocol_version);
  req.set_game_id(config_.game_id);
  req.add_supported_features(gateway::RELAY_READ_RECEIPTS);
  req.add_supported_features(gateway::RELAY_TYPING);
  req.add_supported_features(gateway::RELAY_PRESENCE);

  gateway::Packet pkt;
  pkt.set_msg_id(gateway::PEER_REGISTER_REQ);
  pkt.set_sequence(0);
  pkt.set_body(req.SerializeAsString());
  auto framed = network::ProtobufFraming::Encode(pkt);
  asio::error_code ec;
  asio::write(*socket_, asio::buffer(framed.data(), framed.size()), ec);
  if (ec) {
    common::Logger::Instance().Warn(
        "peer spoke: send register failed: " + ec.message());
    OnDisconnected();
    return;
  }

  // 开始读取响应
  auto buf = std::make_shared<std::vector<uint8_t>>(4096);
  socket_->async_read_some(
      asio::buffer(*buf),
      [this, buf](const std::error_code& ec, size_t bytes) {
        if (ec) {
          common::Logger::Instance().Warn(
              "peer spoke: read failed: " + ec.message());
          OnDisconnected();
          return;
        }
        read_buf_.append(reinterpret_cast<const char*>(buf->data()), bytes);
        // 尝试解析一个完整的帧
        if (read_buf_.size() >= 4) {
          uint32_t len = ntohl(
              *reinterpret_cast<const uint32_t*>(read_buf_.data()));
          if (read_buf_.size() >= 4 + len) {
            gateway::Packet resp_pkt;
            if (resp_pkt.ParseFromArray(read_buf_.data() + 4,
                                        static_cast<int>(len))) {
              HandlePacket(resp_pkt);
            }
            read_buf_.erase(0, 4 + len);
          }
        }
      });
}

void PeerSpoke::OnDisconnected() {
  if (!connected_) {
    return;
  }
  connected_ = false;
  registered_ = false;
  common::Logger::Instance().Info("peer spoke: disconnected from hub");
  if (on_disconnected_) {
    on_disconnected_();
  }
}

void PeerSpoke::HandlePacket(const gateway::Packet& pkt) {
  switch (pkt.msg_id()) {
    case gateway::PEER_REGISTER_RESP: {
      gateway::PeerRegisterResp resp;
      if (!resp.ParseFromString(pkt.body())) {
        common::Logger::Instance().Warn("peer spoke: bad register response");
        OnDisconnected();
        return;
      }
      if (resp.code() != common::OK) {
        common::Logger::Instance().Warn(
            "peer spoke: registration failed, code=" +
            std::to_string(resp.code()));
        OnDisconnected();
        return;
      }
      registered_ = true;
      common::Logger::Instance().Info(
          "peer spoke: registered to hub, negotiated version=" +
          std::to_string(resp.protocol_version()));
      if (on_connected_) {
        on_connected_();
      }
      break;
    }
    case gateway::PEER_INJECT_MESSAGE_NOTIFY: {
      gateway::PeerInjectMessageNotify notify;
      if (notify.ParseFromString(pkt.body()) && on_inject_) {
        on_inject_(notify);
      }
      break;
    }
    default:
      common::Logger::Instance().Warn(
          "peer spoke: unexpected msg_id=" +
          std::to_string(pkt.msg_id()));
      break;
  }
}

}  // namespace chat
}  // namespace chirp
