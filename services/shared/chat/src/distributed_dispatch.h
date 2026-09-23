#pragma once

#include <functional>
#include <memory>

#include "network/session.h"
#include "proto/auth.pb.h"
#include "proto/chat.pb.h"
#include "proto/gateway.pb.h"

namespace chirp::chat::runtime {

using LoginDispatch = std::function<void(const std::shared_ptr<network::Session>& session,
                                         const auth::LoginRequest& req,
                                         int64_t seq)>;
using SendMessageDispatch = std::function<void(const std::shared_ptr<network::Session>& session,
                                               const chat::SendMessageRequest& req,
                                               int64_t seq)>;
using GetHistoryDispatch = std::function<void(const std::shared_ptr<network::Session>& session,
                                              const chat::GetHistoryRequest& req,
                                              int64_t seq)>;
using GetHistoryV2Dispatch = std::function<void(const std::shared_ptr<network::Session>& session,
                                                const std::string& body,
                                                int64_t seq)>;
using LogoutDispatch = std::function<void(const std::shared_ptr<network::Session>& session,
                                          const auth::LogoutRequest& req,
                                          int64_t seq)>;
using MessageAckDispatch = std::function<void(const std::shared_ptr<network::Session>& session,
                                              const chat::MessageAck& req,
                                              int64_t seq)>;
using SetChannelMuteDispatch = std::function<void(const std::shared_ptr<network::Session>& session,
                                                  const chat::SetChannelMuteRequest& req,
                                                  int64_t seq)>;
using GetChannelMutesDispatch = std::function<void(const std::shared_ptr<network::Session>& session,
                                                   const chat::GetChannelMutesRequest& req,
                                                   int64_t seq)>;
using BlockMessageSenderDispatch = std::function<void(
    const std::shared_ptr<network::Session>& session, const chat::BlockMessageSenderRequest& req,
    int64_t seq)>;
using UnblockMessageSenderDispatch = std::function<void(
    const std::shared_ptr<network::Session>& session, const chat::UnblockMessageSenderRequest& req,
    int64_t seq)>;
using GetBlockedSendersDispatch = std::function<void(
    const std::shared_ptr<network::Session>& session, const chat::GetBlockedSendersRequest& req,
    int64_t seq)>;

struct DistributedDispatchHandlers {
  LoginDispatch on_login;
  SendMessageDispatch on_send_message;
  GetHistoryDispatch on_get_history;
  GetHistoryV2Dispatch on_get_history_v2;
  LogoutDispatch on_logout;
  // Client confirms it received a CHAT_MESSAGE_NOTIFY (delivery tracking).
  MessageAckDispatch on_message_ack;
  // Channel mutes (game_chat_features P0 频道屏蔽): per-user push filters.
  SetChannelMuteDispatch on_set_channel_mute;
  GetChannelMutesDispatch on_get_channel_mutes;
  // 黑名单（game_chat_features P0）：拉黑后对方的频道/私聊消息不再投递。
  BlockMessageSenderDispatch on_block_message_sender;
  UnblockMessageSenderDispatch on_unblock_message_sender;
  GetBlockedSendersDispatch on_get_blocked_senders;
};

void DispatchDistributedPacket(const std::shared_ptr<network::Session>& session,
                               const gateway::Packet& pkt,
                               const DistributedDispatchHandlers& handlers);

}  // namespace chirp::chat::runtime
