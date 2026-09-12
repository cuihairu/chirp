#ifndef CHIRP_CHAT_INJECT_CONSUMER_H_
#define CHIRP_CHAT_INJECT_CONSUMER_H_

#include <functional>
#include <string>
#include <vector>

#include "proto/chat.pb.h"
#include "proto/common.pb.h"
#include "proto/server_gateway.pb.h"

namespace chirp::chat {

// Transport hooks the consumer uses to reach the chat runtime (the same tail
// the SEND_MESSAGE path runs through). Everything is invoked on the io
// thread, so implementations need no locking.
struct InjectHooks {
  // Canonical "a|b" channel id for a 1:1 pair (MessageStore::PrivateChannelId).
  std::function<std::string(const std::string& a, const std::string& b)>
      private_channel_id;
  // Persists the message into history and registers it with the
  // receipt/reaction/edit features (the SEND_MESSAGE store tail).
  std::function<void(const chirp::chat::ChatMessage&)> store_message;
  // Delivers to a connected receiver; false means it is offline and the
  // consumer queues the message instead.
  std::function<bool(const std::string& receiver_id,
                     const chirp::chat::ChatMessage& msg)> deliver_private;
  // Pushes the message into one user's offline queue.
  std::function<void(const std::string& user_id,
                     const chirp::chat::ChatMessage& msg)> queue_offline;
  // Broadcasts to a channel's members (except the sender) and returns the
  // ids of the members that were offline.
  std::function<std::vector<std::string>(
      const std::string& channel_id, const chirp::chat::ChatMessage& msg)>
      broadcast_channel;
};

struct InjectOutcome {
  chirp::common::ErrorCode code = chirp::common::OK;
  std::string message_id;  // set when the injection was accepted
};

// Turns hub-forwarded injections (INJECT_MESSAGE_NOTIFY) into normal chat
// deliveries. Injection senders are non-user identities (SYSTEM / NPC /
// SERVICE): they are never membership-checked and skip mention cooldown
// enforcement. Validation mirrors the hub's own checks - the plane is
// trusted, but malformed payloads still must not enter history.
class InjectConsumer {
 public:
  explicit InjectConsumer(InjectHooks hooks);

  InjectOutcome HandleInject(const chirp::server_gateway::InjectMessageNotify& notify);

 private:
  InjectHooks hooks_;
};

}  // namespace chirp::chat

#endif  // CHIRP_CHAT_INJECT_CONSUMER_H_
