#pragma once

#include <string>
#include "session.h"
#include "protobuf_framing.h"
#include "proto/gateway.pb.h"

namespace chirp::network {

/// Helper function to send a protobuf packet through a session
/// @param session The session to send through
/// @param msg_id The gateway message ID
/// @param sequence The packet sequence number
/// @param body The serialized protobuf body
inline void SendProtobufPacket(
    Session* session,
    gateway::MsgID msg_id,
    int64_t sequence,
    const std::string& body) {

  gateway::Packet pkt;
  pkt.set_msg_id(msg_id);
  pkt.set_sequence(sequence);
  pkt.set_body(body);

  auto framed = ProtobufFraming::Encode(pkt);
  session->Send(std::string(reinterpret_cast<const char*>(framed.data()), framed.size()));
}

/// Helper function to send a protobuf packet with message directly
/// @param session The session to send through
/// @param msg_id The gateway message ID
/// @param sequence The packet sequence number
/// @param message The protobuf message to send (must have SerializeAsString())
template<typename MessageType>
inline void SendMessage(
    Session* session,
    gateway::MsgID msg_id,
    int64_t sequence,
    const MessageType& message) {

  SendProtobufPacket(session, msg_id, sequence, message.SerializeAsString());
}

} // namespace chirp::network
