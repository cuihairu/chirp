#include "chirp/sdk.h"

namespace chirp {
namespace sdk {

namespace {

class ChatErrorCategory final : public std::error_category {
public:
  const char* name() const noexcept override { return "chirp.sdk"; }

  std::string message(int ev) const override {
    switch (static_cast<ChatError>(ev)) {
    case ChatError::OK:
      return "ok";
    case ChatError::NotConnected:
      return "not connected";
    case ChatError::AlreadyConnected:
      return "already connected";
    case ChatError::LoginFailed:
      return "login failed";
    case ChatError::SendFailed:
      return "send failed";
    case ChatError::InvalidParam:
      return "invalid parameter";
    case ChatError::Timeout:
      return "timeout";
    default:
      return "unknown error";
    }
  }
};

const ChatErrorCategory& Category() {
  static ChatErrorCategory cat;
  return cat;
}

} // namespace

std::error_code make_error_code(ChatError e) {
  return std::error_code(static_cast<int>(e), Category());
}

} // namespace sdk
} // namespace chirp
