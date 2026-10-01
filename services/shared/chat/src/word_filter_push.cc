#include "word_filter_push.h"

#include "runtime_utils.h"

namespace chirp::chat {

namespace {

WordFilterDeliveryPolicy PolicyToProto(WordFilterPolicy policy) {
  switch (policy) {
    case WordFilterPolicy::kReject:
      return WORD_FILTER_POLICY_REJECT;
    case WordFilterPolicy::kRecord:
      return WORD_FILTER_POLICY_RECORD;
    case WordFilterPolicy::kReplace:
      return WORD_FILTER_POLICY_REPLACE;
  }
  return WORD_FILTER_POLICY_REPLACE;
}

}  // namespace

void FillWordFilterLexicon(const WordFilter* filter, int64_t known_version,
                           WordFilterLexicon* lexicon) {
  if (filter == nullptr) {
    // 未装配词库：proto 缺省即语义（version 0 / enabled false / 空文本）。
    return;
  }
  lexicon->set_version(filter->version());
  lexicon->set_enabled(filter->enabled());
  lexicon->set_policy(PolicyToProto(filter->options().policy));
  lexicon->set_replacement(filter->options().replacement);
  // 条件 GET：客户端已知当前版本时省略文本，省一次全量词库的流量。
  if (filter->version() != known_version) {
    lexicon->set_lexicon(filter->SerializeLexicon());
  }
}

void HandleWordFilterFetch(const gateway::Packet& pkt,
                           const std::shared_ptr<network::Session>& session,
                           const WordFilter* filter,
                           const std::string& authenticated_user_id) {
  WordFilterFetchRequest req;
  WordFilterFetchResponse resp;
  if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
    resp.set_code(chirp::common::INVALID_PARAM);
  } else if (authenticated_user_id.empty()) {
    // 词库不是机密，但不开给未认证连接。
    resp.set_code(chirp::common::AUTH_FAILED);
  } else {
    resp.set_code(chirp::common::OK);
    FillWordFilterLexicon(filter, req.known_version(), resp.mutable_lexicon());
  }
  runtime::SendPacket(session, gateway::WORD_FILTER_FETCH_RESP, pkt.sequence(),
                      resp.SerializeAsString());
}

void BroadcastWordFilterUpdate(const std::shared_ptr<network::SessionRegistry>& state,
                               const WordFilter& filter) {
  // 锁内只收集活 shared_ptr，投递放到锁外（registry 快照语义，对齐设备清单
  // 广播）：SendPacket 会 post 到各会话 strand，持锁投递是自找死锁。
  std::vector<std::shared_ptr<network::Session>> live;
  {
    std::lock_guard<std::mutex> lock(state->mu);
    live.reserve(state->user_to_sessions.size());
    for (const auto& [user_id, platforms] : state->user_to_sessions) {
      for (const auto& [platform, session] : platforms) {
        if (auto owned = session.lock()) {
          live.push_back(std::move(owned));
        }
      }
    }
  }
  if (live.empty()) {
    return;
  }
  WordFilterUpdateNotify notify;
  FillWordFilterLexicon(&filter, /*known_version=*/-1, notify.mutable_lexicon());
  const std::string body = notify.SerializeAsString();
  for (const auto& session : live) {
    runtime::SendPacket(session, gateway::WORD_FILTER_UPDATE_NOTIFY, /*seq=*/0, body);
  }
}

}  // namespace chirp::chat
