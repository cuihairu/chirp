#include "peer_hub.h"

#include "logger.h"
#include "network/protobuf_framing.h"

namespace chirp {
namespace chat {

PeerHub::PeerHub(PeerHubConfig config) : config_(std::move(config)) {}

gateway::PeerRegisterResp PeerHub::HandleRegister(
    const gateway::PeerRegisterReq& req,
    std::shared_ptr<network::Session> session) {
  gateway::PeerRegisterResp resp;
  resp.set_protocol_version(req.protocol_version());

  // 检查白名单
  auto it = config_.allowed_peers.find(req.service_id());
  if (it == config_.allowed_peers.end()) {
    if (!config_.allow_unknown_peers) {
      resp.set_code(common::AUTH_FAILED);
      common::Logger::Instance().Warn(
          "peer registration rejected: " + req.service_id() + " not in whitelist");
      return resp;
    }
  } else if (it->second != req.service_secret()) {
    resp.set_code(common::AUTH_FAILED);
    common::Logger::Instance().Warn(
        "peer registration rejected: " + req.service_id() + " bad secret");
    return resp;
  }

  // 检查版本
  if (req.protocol_version() < config_.min_peer_version) {
    resp.set_code(common::VERSION_MISMATCH);
    resp.set_min_version(config_.min_peer_version);
    common::Logger::Instance().Warn(
        "peer registration rejected: " + req.service_id() +
        " version " + std::to_string(req.protocol_version()) +
        " < min " + std::to_string(config_.min_peer_version));
    return resp;
  }

  // 验证 game_id
  if (req.game_id().empty() || req.game_id().find(':') != std::string::npos) {
    resp.set_code(common::INVALID_PARAM);
    common::Logger::Instance().Warn(
        "peer registration rejected: invalid game_id '" + req.game_id() + "'");
    return resp;
  }

  // 如果同一 service_id 已有连接，挤掉旧的
  auto old_it = game_to_session_.find(req.game_id());
  if (old_it != game_to_session_.end()) {
    auto old_session = old_it->second;
    common::Logger::Instance().Info(
        "displacing old peer connection for game_id=" + req.game_id());
    peers_.erase(old_session);
    game_to_session_.erase(old_it);
    // 旧连接会在下次写入时检测到关闭
  }

  // 注册新 peer
  RegisteredPeer peer;
  peer.service_id = req.service_id();
  peer.game_id = req.game_id();
  peer.protocol_version = std::min(req.protocol_version(), 1);  // 当前版本 = 1
  peer.capabilities = {req.supported_features().begin(), req.supported_features().end()};
  peer.session = std::move(session);

  const auto* session_ptr = peer.session.get();
  game_to_session_[req.game_id()] = session_ptr;
  peers_.emplace(session_ptr, std::move(peer));

  resp.set_code(common::OK);
  resp.set_heartbeat_interval_seconds(30);
  // 返回 hub 支持的能力
  resp.add_supported_features(gateway::RELAY_READ_RECEIPTS);
  resp.add_supported_features(gateway::RELAY_TYPING);
  resp.add_supported_features(gateway::RELAY_PRESENCE);
  resp.add_supported_features(gateway::RELAY_OFFLINE_MESSAGES);

  common::Logger::Instance().Info(
      "peer registered: service_id=" + req.service_id() +
      " game_id=" + req.game_id() +
      " version=" + std::to_string(peer.protocol_version));

  return resp;
}

void PeerHub::OnPeerDisconnected(const network::Session* session) {
  auto it = peers_.find(session);
  if (it == peers_.end()) {
    return;
  }
  common::Logger::Instance().Info(
      "peer disconnected: game_id=" + it->second.game_id);
  game_to_session_.erase(it->second.game_id);
  peers_.erase(it);
}

bool PeerHub::IsRegisteredPeer(const network::Session* session) const {
  return peers_.count(session) > 0;
}

const RegisteredPeer* PeerHub::GetPeer(const network::Session* session) const {
  auto it = peers_.find(session);
  return it != peers_.end() ? &it->second : nullptr;
}

const RegisteredPeer* PeerHub::GetPeerByGameId(const std::string& game_id) const {
  auto it = game_to_session_.find(game_id);
  if (it == game_to_session_.end()) {
    return nullptr;
  }
  auto peer_it = peers_.find(it->second);
  return peer_it != peers_.end() ? &peer_it->second : nullptr;
}

bool PeerHub::SendToPeer(const std::string& game_id,
                         gateway::MsgID msg_id,
                         const google::protobuf::Message& body) {
  const auto* peer = GetPeerByGameId(game_id);
  if (!peer || !peer->session || peer->session->IsClosed()) {
    return false;
  }
  gateway::Packet pkt;
  pkt.set_msg_id(msg_id);
  pkt.set_sequence(0);
  pkt.set_body(body.SerializeAsString());
  auto framed = network::ProtobufFraming::Encode(pkt);
  peer->session->Send(
      std::string(reinterpret_cast<const char*>(framed.data()), framed.size()));
  return true;
}

void PeerHub::BroadcastToPeers(gateway::MsgID msg_id,
                               const google::protobuf::Message& body) {
  for (const auto& [key, peer] : peers_) {
    if (peer.session && !peer.session->IsClosed()) {
      gateway::Packet pkt;
      pkt.set_msg_id(msg_id);
      pkt.set_sequence(0);
      pkt.set_body(body.SerializeAsString());
      auto framed = network::ProtobufFraming::Encode(pkt);
      peer.session->Send(
          std::string(reinterpret_cast<const char*>(framed.data()), framed.size()));
    }
  }
}

size_t PeerHub::PeerCount() const {
  return peers_.size();
}

}  // namespace chat
}  // namespace chirp
