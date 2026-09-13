#ifndef CHIRP_SERVICES_NOTIFICATION_NOTIFICATION_HANDLERS_H_
#define CHIRP_SERVICES_NOTIFICATION_NOTIFICATION_HANDLERS_H_

#include "notification_service.h"
#include "proto/common.pb.h"
#include "proto/gateway.pb.h"
#include "proto/notification.pb.h"

namespace chirp {
namespace notification {

// Packet handlers for the notification plane (6xxx). Pure routing between
// the Packet envelope and the in-process NotificationService - no sockets,
// so every branch is unit-testable. Invoked from the single io thread; the
// service guards its own state.
class NotificationHandlers {
 public:
  explicit NotificationHandlers(NotificationService& service);

  // Dispatches pkt to the service and fills *resp with the paired _RESP
  // (sequence echoed through). Returns false for unknown msg ids - the
  // caller sends nothing, mirroring the gateway's default case.
  bool HandlePacket(const chirp::gateway::Packet& pkt, chirp::gateway::Packet* resp);

 private:
  bool HandleRegister(const chirp::gateway::Packet& pkt, chirp::gateway::Packet* resp);
  bool HandleUnregister(const chirp::gateway::Packet& pkt, chirp::gateway::Packet* resp);
  bool HandleUpdateToken(const chirp::gateway::Packet& pkt, chirp::gateway::Packet* resp);
  bool HandleGetDevices(const chirp::gateway::Packet& pkt, chirp::gateway::Packet* resp);
  bool HandlePush(const chirp::gateway::Packet& pkt, chirp::gateway::Packet* resp);

  NotificationService& service_;
};

}  // namespace notification
}  // namespace chirp

#endif  // CHIRP_SERVICES_NOTIFICATION_NOTIFICATION_HANDLERS_H_
