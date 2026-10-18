#ifndef CHIRP_SERVICES_NOTIFICATION_CALLER_AUTH_H_
#define CHIRP_SERVICES_NOTIFICATION_CALLER_AUTH_H_

// Caller authentication gate for the notification plane (audit 表一 P0 half:
// the 6xxx handlers trusted whoever could reach the port, so any caller with
// network access could register devices, read device lists or fire pushes for
// arbitrary users).
//
// With a non-empty secret every inbound connection must complete the
// SERVER_AUTH_REQ handshake (body = game_server_gateway.ServerAuthRequest)
// before any 6xxx packet is served. With an empty secret the plane stays open
// exactly as before, which keeps the dev compose stack and integration
// scripts working unchanged.
//
// Both helpers are pure decisions: the connection layer (main.cc) tracks
// per-connection trust and applies the verdict, so the gate itself is
// unit-testable without sockets.

#include "proto/common.pb.h"
#include "proto/game_server_gateway.pb.h"
#include "proto/gateway.pb.h"

namespace chirp {
namespace app_notification {

// Verdict for an inbound packet on a connection whose trust state is known.
enum class CallerAuthVerdict {
  kServe,        // no secret configured, or the connection is already trusted
  kHandshake,    // untrusted connection: this must be SERVER_AUTH_REQ
  kUnauthorized, // untrusted connection sent a non-handshake packet: drop it
};

inline CallerAuthVerdict GateCallerPacket(bool connection_trusted,
                                          const std::string& secret,
                                          const chirp::gateway::Packet& pkt) {
  if (secret.empty() || connection_trusted) {
    return CallerAuthVerdict::kServe;
  }
  if (pkt.msg_id() == chirp::gateway::SERVER_AUTH_REQ) {
    return CallerAuthVerdict::kHandshake;
  }
  return CallerAuthVerdict::kUnauthorized;
}

// Verifies a SERVER_AUTH_REQ body against the configured secret. Always fills
// *resp with the SERVER_AUTH_RESP the caller should send (code OK /
// AUTH_FAILED, sequence echoed) and returns true only for a matching secret;
// a malformed body, a non-handshake packet or a wrong secret yield false.
inline bool VerifyCallerSecret(const chirp::gateway::Packet& pkt,
                               const std::string& secret,
                               chirp::gateway::Packet* resp) {
  resp->set_msg_id(chirp::gateway::SERVER_AUTH_RESP);
  resp->set_sequence(pkt.sequence());
  chirp::game_server_gateway::ServerAuthResponse body;
  bool match = false;
  if (pkt.msg_id() == chirp::gateway::SERVER_AUTH_REQ) {
    chirp::game_server_gateway::ServerAuthRequest req;
    match = req.ParseFromString(pkt.body()) && req.secret() == secret;
  }
  body.set_code(match ? chirp::common::OK : chirp::common::AUTH_FAILED);
  resp->set_body(body.SerializeAsString());
  return match;
}

}  // namespace app_notification
}  // namespace chirp

#endif  // CHIRP_SERVICES_NOTIFICATION_CALLER_AUTH_H_
