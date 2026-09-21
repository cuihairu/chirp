#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

#include "network/session.h"
#include "proto/chat.pb.h"
#include "proto/gateway.pb.h"

namespace chirp {
namespace chat {

// Hub 配置
struct PeerHubConfig {
  // allowed_peers: service_id:secret 对，逗号分隔
  // 空 = 拒绝所有
  std::map<std::string, std::string> allowed_peers;
  // 最低接受的协议版本
  int min_peer_version = 1;
  // 是否接受不在白名单中的 peer（开放注册模式）
  bool allow_unknown_peers = false;
};

// 已注册的 peer 信息
struct RegisteredPeer {
  std::string service_id;
  std::string game_id;
  int protocol_version = 1;
  std::vector<gateway::PeerCapability> capabilities;
  std::shared_ptr<network::Session> session;
};

// Hub 模式：接受 game_chat 的注册，管理跨平面消息路由。
// 部署为 app_chat 时启用。
class PeerHub {
 public:
  explicit PeerHub(PeerHubConfig config);
  ~PeerHub() = default;

  // 处理 PEER_REGISTER_REQ。返回 PEER_REGISTER_RESP。
  gateway::PeerRegisterResp HandleRegister(
      const gateway::PeerRegisterReq& req,
      std::shared_ptr<network::Session> session);

  // peer 断线时调用
  void OnPeerDisconnected(const network::Session* session);

  // 检查 session 是否是已注册的 peer
  bool IsRegisteredPeer(const network::Session* session) const;

  // 获取 peer 信息
  const RegisteredPeer* GetPeer(const network::Session* session) const;

  // 按 game_id 获取 peer
  const RegisteredPeer* GetPeerByGameId(const std::string& game_id) const;

  // 向指定 game_id 的 peer 发送消息
  bool SendToPeer(const std::string& game_id,
                  gateway::MsgID msg_id,
                  const google::protobuf::Message& body);

  // 向所有 peer 广播消息
  void BroadcastToPeers(gateway::MsgID msg_id,
                        const google::protobuf::Message& body);

  // 已注册 peer 数量
  size_t PeerCount() const;

 private:
  PeerHubConfig config_;
  // session* -> RegisteredPeer
  std::unordered_map<const network::Session*, RegisteredPeer> peers_;
  // game_id -> session* (反向索引)
  std::unordered_map<std::string, const network::Session*> game_to_session_;
};

}  // namespace chat
}  // namespace chirp
