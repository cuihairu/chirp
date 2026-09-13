#ifndef CHIRP_SERVICES_NOTIFICATION_PUSH_TRANSPORT_H_
#define CHIRP_SERVICES_NOTIFICATION_PUSH_TRANSPORT_H_

#include <string>
#include <unordered_map>

namespace chirp {
namespace notification {

// One outbound provider request (legacy FCM HTTP or APNs). The service
// builds it; the transport only moves bytes.
struct PushRequest {
  std::string provider;  // "fcm" | "apns"
  std::string url;
  std::string payload;   // prebuilt JSON body
  std::string device_token;
  std::unordered_map<std::string, std::string> headers;
};

// Sends one provider request and returns the HTTP response body. An empty
// return means "no usable response" - the stub-era semantics the service's
// success predicate (`!response.empty() || token.empty()`) is built on.
class PushTransport {
 public:
  virtual ~PushTransport() = default;
  virtual std::string Post(const PushRequest& request) = 0;
};

// Default backend: provider HTTP needs TLS (APNs additionally HTTP/2),
// which this build does not carry, so it logs the attempt and answers
// empty - behavior-identical to the previous in-class HTTPPost stub.
class LoggingPushTransport : public PushTransport {
 public:
  std::string Post(const PushRequest& request) override;
};

}  // namespace notification
}  // namespace chirp

#endif  // CHIRP_SERVICES_NOTIFICATION_PUSH_TRANSPORT_H_
