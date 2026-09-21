#include "push_bridge.h"

#include "logger.h"

namespace chirp::chat {

namespace {

// Matches the notification service's own body truncation for previews.
constexpr size_t kMaxBodyChars = 100;

std::string TruncateBody(const std::string& content) {
  if (content.size() <= kMaxBodyChars) {
    return content;
  }
  return content.substr(0, kMaxBodyChars) + "...";
}

const char* ChannelTypeName(chirp::chat::ChannelType type) {
  switch (type) {
  case chirp::chat::TEAM:
    return "team";
  case chirp::chat::GUILD:
    return "guild";
  case chirp::chat::WORLD:
    return "world";
  case chirp::chat::PRIVATE:
  default:
    return "private";
  }
}

}  // namespace

PushBridge::PushBridge(std::shared_ptr<app_notification::NotificationClient> client)
    : client_(std::move(client)) {}

void PushBridge::NotifyOffline(const ChatMessage& msg, const std::string& offline_user_id) {
  if (!client_ || offline_user_id.empty()) {
    return;
  }

  app_notification::PushNotificationRequest req;
  req.set_user_id(offline_user_id);
  req.set_type(app_notification::MESSAGE);
  req.set_title(msg.sender_id());
  req.set_body(TruncateBody(msg.content()));
  req.set_tag(msg.channel_id());
  req.set_click_action("chirp://chat/" + msg.channel_id());
  (*req.mutable_data())["type"] = "message";
  (*req.mutable_data())["from_user_id"] = msg.sender_id();
  (*req.mutable_data())["channel_id"] = msg.channel_id();
  (*req.mutable_data())["channel_type"] = ChannelTypeName(msg.channel_type());
  (*req.mutable_data())["message_id"] = msg.message_id();

  // The client posts the callback onto the chat io_context; a failure is just
  // a lost push (the message itself is already queued offline).
  client_->AsyncPush(req, /*seq=*/0, [](const app_notification::PushNotificationResponse& resp) {
    if (resp.code() != chirp::common::OK) {
      chirp::common::Logger::Instance().Warn("offline push failed with code " +
                                             std::to_string(resp.code()));
    }
  });
}

}  // namespace chirp::chat
