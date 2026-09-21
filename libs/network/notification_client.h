#ifndef CHIRP_SERVICES_NOTIFICATION_NOTIFICATION_CLIENT_H_
#define CHIRP_SERVICES_NOTIFICATION_NOTIFICATION_CLIENT_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include <asio.hpp>

#include "proto/gateway.pb.h"
#include "proto/app_notification.pb.h"

namespace chirp {
namespace app_notification {

// Async RPC client for the notification plane (6xxx). Same architecture as
// the gateway's AuthClient: one worker thread runs blocking per-job
// connections; results are posted back onto the caller's io_context. Any
// transport failure (connect refused, drop, garbage frame, wrong response
// msg id) yields a response with code INTERNAL_ERROR instead of throwing.
class NotificationClient {
 public:
  using PushCallback = std::function<void(const PushNotificationResponse&)>;
  using RegisterCallback = std::function<void(const RegisterDeviceResponse&)>;
  using UnregisterCallback = std::function<void(const UnregisterDeviceResponse&)>;
  using UpdateTokenCallback = std::function<void(const UpdateDeviceTokenResponse&)>;
  using DevicesCallback = std::function<void(const GetUserDevicesResponse&)>;

  NotificationClient(asio::io_context& main_io, std::string host, uint16_t port);
  ~NotificationClient();  // drains queued jobs, then joins the worker

  NotificationClient(const NotificationClient&) = delete;
  NotificationClient& operator=(const NotificationClient&) = delete;

  void AsyncPush(const PushNotificationRequest& req, int64_t seq, PushCallback cb);
  void AsyncRegisterDevice(const RegisterDeviceRequest& req, int64_t seq, RegisterCallback cb);
  void AsyncUnregisterDevice(const UnregisterDeviceRequest& req, int64_t seq,
                             UnregisterCallback cb);
  void AsyncUpdateDeviceToken(const UpdateDeviceTokenRequest& req, int64_t seq,
                              UpdateTokenCallback cb);
  void AsyncGetUserDevices(const GetUserDevicesRequest& req, int64_t seq, DevicesCallback cb);

 private:
  friend void DrainAndDropNotificationClientForTest(NotificationClient& client);

  // Called on the main io with the response body (ok), or with an empty
  // body and ok=false on any transport/protocol failure.
  using DeliverFn = std::function<void(const std::string& body, bool ok)>;

  void Enqueue(chirp::gateway::MsgID req_id, chirp::gateway::MsgID resp_id, std::string body,
               int64_t seq, DeliverFn deliver);

  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// Test hook: drains the worker and releases the impl so a subsequent
// destructor exercises its null-impl guard.
void DrainAndDropNotificationClientForTest(NotificationClient& client);

}  // namespace app_notification
}  // namespace chirp

#endif  // CHIRP_SERVICES_NOTIFICATION_NOTIFICATION_CLIENT_H_
