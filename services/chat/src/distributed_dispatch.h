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

struct DistributedDispatchHandlers {
  LoginDispatch on_login;
  SendMessageDispatch on_send_message;
  GetHistoryDispatch on_get_history;
  GetHistoryV2Dispatch on_get_history_v2;
  LogoutDispatch on_logout;
};

void DispatchDistributedPacket(const std::shared_ptr<network::Session>& session,
                               const gateway::Packet& pkt,
                               const DistributedDispatchHandlers& handlers);

}  // namespace chirp::chat::runtime
