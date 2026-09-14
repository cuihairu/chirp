#ifndef CHIRP_SERVICES_CHAT_PUSH_BRIDGE_H_
#define CHIRP_SERVICES_CHAT_PUSH_BRIDGE_H_

#include <memory>
#include <string>

#include "network/notification_client.h"
#include "proto/chat.pb.h"

namespace chirp::chat {

// Fire-and-forget bridge from offline chat messages to the notification
// plane. A null client (feature disabled) makes every call a no-op; enqueue
// failures are logged, never propagated - the send path must not block.
class PushBridge {
 public:
  explicit PushBridge(std::shared_ptr<notification::NotificationClient> client);

  // Queues a push for a user who was offline when the message arrived.
  void NotifyOffline(const ChatMessage& msg, const std::string& offline_user_id);

 private:
  std::shared_ptr<notification::NotificationClient> client_;
};

}  // namespace chirp::chat

#endif  // CHIRP_SERVICES_CHAT_PUSH_BRIDGE_H_
