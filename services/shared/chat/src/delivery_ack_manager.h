#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

#include <asio.hpp>

#include "network/session.h"

namespace chirp::chat {

/// @brief 离线队列的 default 桶：无设备可知时的共享桶（发送时接收方无任何
/// 在线端 → 目标端未知，副本落这里，任何端的登录先到先得）。键形与拆分前的
/// user 级队列一致（零迁移）；命名设备桶 = 同键再拼 ":<slot>"。
/// slot 本身 = NormalizePlatformId(platform)，与 SessionRegistry 的
/// (user, platform) 槽位同单位。
inline constexpr char kDefaultOfflineSlot[] = "default";

/// @brief Tracks live private-message deliveries until the receiving client
/// acknowledges them (MESSAGE_ACK). A delivery that is not acknowledged within
/// the timeout is handed back to the owner via on_requeue so it lands in the
/// offline queue and is refilled on the next login - the recovery path for
/// messages pushed into a zombie connection.
///
/// 每笔在途投递带一个离线桶 slot：在线扇出的首投记 kDefaultOfflineSlot
/// （回队回共享桶，任何端下次登录可领）；登录补投记认领端自己的 slot
/// （回队只回该端，重投不再被其他端的登录截走）。迟到 ack 清理按同一
/// (receiver, slot) 精确落桶。
///
/// Only sessions that declared the capability at login (supports_message_ack)
/// are tracked; legacy clients keep the send-and-forget behavior because they
/// would never acknowledge anything.
///
/// Store interaction goes through callbacks so the manager stays independent
/// of any MessageStore implementation. All callbacks run on the io_context
/// thread, like every other chat dispatch path.
class DeliveryAckManager {
public:
  struct Config {
    // 0 disables the feature entirely: Track becomes a no-op and no session
    // is ever marked capable (kill switch for incidents).
    int64_t timeout_ms = 10000;
    int64_t scan_interval_ms = 500;
    // How long a requeued message stays remembered for late-ack cleanup
    // (removing the offline copy the client actually acknowledged late).
    int64_t requeued_retention_ms = 600000;
  };

  // (receiver_id, offline slot, payload bytes): payload is exactly the byte
  // string that was tracked, so offline-queue removal can match it byte for
  // byte; slot 是回队/清理要落的那只离线桶（Track 时记录，见类注释）。
  using PayloadCallback = std::function<void(const std::string& receiver_id,
                                             const std::string& slot,
                                             const std::string& payload)>;

  DeliveryAckManager(asio::io_context& io, Config config,
                     PayloadCallback on_requeue, PayloadCallback on_late_ack);
  ~DeliveryAckManager();

  DeliveryAckManager(const DeliveryAckManager&) = delete;
  DeliveryAckManager& operator=(const DeliveryAckManager&) = delete;

  void Start();
  void Stop();

  // Capability bookkeeping -------------------------------------------------
  // Sessions are remembered weakly: an expired entry proves the session is
  // gone even if its pointer was reused by a new connection.
  void MarkCapable(const std::shared_ptr<network::Session>& session);
  bool IsCapable(const network::Session* session);
  void ForgetSession(const network::Session* session);

  // Pending bookkeeping ----------------------------------------------------
  // 投递主语 = delivery_id 非空时用之（补投/重投各成一笔，精确匹配）；
  // 空 = 首次在线投递，主语即 message_id。Idempotent: 同一主语再 Track
  // 只是刷新（重投同 id 的场景天然收敛）。slot 是这笔记账对应的离线桶
  // （在线首投 = kDefaultOfflineSlot；补投 = 认领端 slot），超时回队与
  // 迟到 ack 清理都落这只桶。
  void Track(const std::string& message_id,
             const std::string& delivery_id,
             const std::string& receiver_id,
             const std::string& slot,
             const std::string& payload);
  // Returns true when the ack meant something: it cleared a pending entry, or
  // it arrived late and on_late_ack was invoked to clean the offline copy.
  // delivery_id 空 = 旧客户端，退化按 message_id 匹配首投。
  bool Acknowledge(const std::string& message_id,
                   const std::string& delivery_id);

  size_t pending_count() const;

private:
  void RunCheck();

  Config config_;
  PayloadCallback on_requeue_;
  PayloadCallback on_late_ack_;

  struct Pending {
    std::string receiver_id;
    std::string slot;  // 回队要落的离线桶（Track 传入，见类注释）
    std::string payload;
    int64_t deadline_ms;
  };
  struct Requeued {
    std::string receiver_id;
    std::string slot;
    std::string payload;
    int64_t requeued_at_ms;
  };
  // key = 投递主语：delivery_id（非空）或首投的 message_id（空 id 场景）。
  std::unordered_map<std::string, Pending> pending_;
  // Messages already handed back to the offline queue, remembered so a late
  // ack can still remove the offline copy; swept on requeued_retention_ms.
  std::unordered_map<std::string, Requeued> requeued_;
  std::unordered_map<const void*, std::weak_ptr<network::Session>> capable_;

  mutable std::mutex mu_;
  asio::steady_timer timer_;
  std::atomic<bool> running_{false};
};

} // namespace chirp::chat
