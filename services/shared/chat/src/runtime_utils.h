#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "network/session.h"
#include "proto/chat.pb.h"
#include "proto/gateway.pb.h"

namespace chirp::chat::runtime {

int64_t NowMs();
std::string GetArg(int argc, char** argv, const std::string& key, const std::string& def);
uint16_t ParseU16Arg(int argc, char** argv, const std::string& key, uint16_t def);
int ParseIntArg(int argc, char** argv, const std::string& key, int def);
std::string RandomHex(size_t bytes);
std::string GenerateMessageId();

void SendPacket(const std::shared_ptr<network::Session>& session,
                gateway::MsgID msg_id,
                int64_t seq,
                const std::string& body);

void SendChatNotify(const std::shared_ptr<network::Session>& session,
                    const chirp::chat::ChatMessage& msg);

}  // namespace chirp::chat::runtime
