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
    case gateway::MESSAGE_ACK: {
      chat::MessageAck req;
      if (handlers.on_message_ack &&
          req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
        handlers.on_message_ack(session, req, pkt.sequence());
      }
      break;
    }
    case gateway::SET_CHANNEL_MUTE_REQ: {
      chat::SetChannelMuteRequest req;
      if (handlers.on_set_channel_mute &&
          req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
        handlers.on_set_channel_mute(session, req, pkt.sequence());
      }
      break;
    }
    case gateway::GET_CHANNEL_MUTES_REQ: {
      chat::GetChannelMutesRequest req;
      if (handlers.on_get_channel_mutes &&
          req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
        handlers.on_get_channel_mutes(session, req, pkt.sequence());
      }
      break;
    }
    case gateway::BLOCK_MESSAGE_SENDER_REQ: {
      chat::BlockMessageSenderRequest req;
      if (handlers.on_block_message_sender &&
          req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
        handlers.on_block_message_sender(session, req, pkt.sequence());
      }
      break;
    }
    case gateway::UNBLOCK_MESSAGE_SENDER_REQ: {
      chat::UnblockMessageSenderRequest req;
      if (handlers.on_unblock_message_sender &&
          req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
        handlers.on_unblock_message_sender(session, req, pkt.sequence());
      }
      break;
    }
    case gateway::GET_BLOCKED_SENDERS_REQ: {
      chat::GetBlockedSendersRequest req;
      if (handlers.on_get_blocked_senders &&
          req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
        handlers.on_get_blocked_senders(session, req, pkt.sequence());
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
