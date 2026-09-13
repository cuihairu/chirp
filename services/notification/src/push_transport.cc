#include "push_transport.h"

#include "logger.h"

namespace chirp {
namespace notification {

std::string LoggingPushTransport::Post(const PushRequest& request) {
  // Provider HTTP is not implemented; the empty response keeps the service's
  // stub-era semantics: devices without a token "succeed", devices with a
  // token fail until a real transport is injected.
  common::Logger::Instance().Info(
      "push-transport dropping " + request.provider + " request for " +
      request.url + " (" + std::to_string(request.payload.size()) + " bytes)");
  return "";
}

}  // namespace notification
}  // namespace chirp
