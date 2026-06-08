#include "distributed_dispatch.h"

#include "runtime_utils.h"

namespace chirp::chat::runtime {

void DispatchDistributedPacket(const std::shared_ptr<network::Session>& session,
                               const gateway::Packet& pkt,
                               const DistributedDispatchHandlers& handlers) {
  switch (pkt.msg_id()) {
    case gateway::LOGIN_REQ: {
      auth::LoginRequest req;
      if (handlers.on_login &&
          req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
        handlers.on_login(session, req, pkt.sequence());
      }
      break;
    }
    case gateway::SEND_MESSAGE_REQ: {
      chat::SendMessageRequest req;
      if (handlers.on_send_message &&
          req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
        handlers.on_send_message(session, req, pkt.sequence());
      }
      break;
    }
    case gateway::GET_HISTORY_REQ: {
      chat::GetHistoryRequest req;
      if (handlers.on_get_history &&
          req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
        handlers.on_get_history(session, req, pkt.sequence());
      }
      break;
    }
    case gateway::GET_HISTORY_V2_REQ: {
      if (handlers.on_get_history_v2) {
        handlers.on_get_history_v2(session, pkt.body(), pkt.sequence());
      }
      break;
    }
    case gateway::LOGOUT_REQ: {
      auth::LogoutRequest req;
      if (handlers.on_logout &&
          req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
        handlers.on_logout(session, req, pkt.sequence());
      }
      break;
    }
    case gateway::HEARTBEAT_PING: {
      gateway::HeartbeatPong pong;
      pong.set_timestamp(NowMs());
      pong.set_server_time(NowMs());
      SendPacket(session, gateway::HEARTBEAT_PONG, pkt.sequence(), pong.SerializeAsString());
      break;
    }
    default:
      break;
  }
}

}  // namespace chirp::chat::runtime
