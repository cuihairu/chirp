#include "search_server.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <map>
#include <utility>

#include "common/metrics.h"
#include "chat_validation.h"
#include "index_sync.h"
#include "logger.h"
#include "login_token_verifier.h"
#include "network/protobuf_framing.h"
#include "network/session.h"
#include "proto/auth.pb.h"
#include "proto/chat.pb.h"
#include "proto/common.pb.h"
#include "proto/game_server_gateway.pb.h"
#include "proto/gateway.pb.h"
#include "text_segmenter.h"

namespace chirp {
namespace search {

namespace {

using chirp::common::Logger;

int64_t NowMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

void SendPacket(const std::shared_ptr<network::Session>& session,
                gateway::MsgID msg_id, int64_t seq, const std::string& body) {
  gateway::Packet pkt;
  pkt.set_msg_id(msg_id);
  pkt.set_sequence(seq);
  pkt.set_body(body);
  const auto framed = network::ProtobufFraming::Encode(pkt);
  session->Send(std::string(reinterpret_cast<const char*>(framed.data()), framed.size()));
}

void SendPacketAndClose(const std::shared_ptr<network::Session>& session,
                        gateway::MsgID msg_id, int64_t seq, const std::string& body) {
  gateway::Packet pkt;
  pkt.set_msg_id(msg_id);
  pkt.set_sequence(seq);
  pkt.set_body(body);
  const auto framed = network::ProtobufFraming::Encode(pkt);
  session->SendAndClose(
      std::string(reinterpret_cast<const char*>(framed.data()), framed.size()));
}

// 会话 id 只在应答里回显（bridge 只认 code；search 不进 SessionRegistry，
// 无顶号语义），进程内单调即可。
std::string GenerateSearchSessionId() {
  static std::atomic<uint64_t> counter{1};
  return "search_" + std::to_string(NowMs()) + "_" +
         std::to_string(counter.fetch_add(1, std::memory_order_relaxed));
}

constexpr int32_t kDefaultLimit = 20;
constexpr int32_t kMaxLimit = 50;

// PRIVATE 频道门槛：请求者必须是 "userA|userB" 两端之一（与
// ValidateGetHistoryRequest 同一判据，直接复用 chat_validation）。
bool PrivateChannelVisible(const std::string& channel_id,
                           const std::string& authenticated_user_id) {
  return chirp::chat::PrivateChannelContainsUser(channel_id, authenticated_user_id);
}

}  // namespace

SearchServer::SearchServer(MessageSearchIndex& index, MessageIndexSync* sync,
                           Options options)
    : index_(index), sync_(sync), options_(std::move(options)),
      verifier_(options_.token_secret.empty()
                    ? nullptr
                    : std::make_unique<common::LoginTokenVerifier>(
                          options_.token_secret)) {}

SearchServer::~SearchServer() = default;

size_t SearchServer::trusted_count() const { return trusted_.size(); }

size_t SearchServer::authenticated_count() const { return user_of_.size(); }

void SearchServer::HandleFrame(const std::shared_ptr<network::Session>& session,
                               std::string&& payload) {
  gateway::Packet pkt;
  if (!pkt.ParseFromArray(payload.data(), static_cast<int>(payload.size()))) {
    Logger::Instance().Warn("search: failed to parse Packet from connection");
    return;
  }
  CHIRP_COUNTER("chirp_search_packets_total", 1);

  switch (pkt.msg_id()) {
  case gateway::SERVER_AUTH_REQ:
    HandleServerAuth(session, pkt);
    break;
  case gateway::LOGIN_REQ:
    HandleLogin(session, pkt);
    break;
  case gateway::SEARCH_MESSAGE_REQ:
    HandleSearch(session, pkt);
    break;
  default:
    break;  // 未知/不相关消息一律忽略：次级管道只服务检索面
  }
}

void SearchServer::HandleClose(const std::shared_ptr<network::Session>& session) {
  trusted_.erase(session.get());
  user_of_.erase(session.get());
}

void SearchServer::HandleServerAuth(const std::shared_ptr<network::Session>& session,
                                    const gateway::Packet& pkt) {
  // 与 chat 同门：无 secret 配置时帧被忽略（仅本地调试直连），有 secret 时
  // 校验失败即回 AUTH_FAILED 并断开（pipe 不可信，没有降级空间）。
  if (options_.service_secret.empty()) {
    return;
  }
  game_server_gateway::ServerAuthRequest auth_req;
  game_server_gateway::ServerAuthResponse auth_resp;
  auth_resp.set_server_time_ms(NowMs());
  if (!auth_req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size())) ||
      auth_req.secret() != options_.service_secret) {
    auth_resp.set_code(chirp::common::AUTH_FAILED);
    SendPacketAndClose(session, gateway::SERVER_AUTH_RESP, pkt.sequence(),
                       auth_resp.SerializeAsString());
    return;
  }
  trusted_.insert(session.get());
  auth_resp.set_code(chirp::common::OK);
  SendPacket(session, gateway::SERVER_AUTH_RESP, pkt.sequence(),
             auth_resp.SerializeAsString());
}

void SearchServer::HandleLogin(const std::shared_ptr<network::Session>& session,
                               const gateway::Packet& pkt) {
  chirp::auth::LoginRequest login_req;
  chirp::auth::LoginResponse resp;
  resp.set_server_time(NowMs());
  if (!login_req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
    resp.set_code(chirp::common::INVALID_PARAM);
    SendPacket(session, gateway::LOGIN_RESP, pkt.sequence(), resp.SerializeAsString());
    return;
  }

  std::string user_id;
  // verifier_ 只在有 secret 时构造（enabled 恒真），判空即判形态。
  if (verifier_ != nullptr) {
    std::string verify_err;
    if (!verifier_->Verify(login_req.token(), NowMs(), &user_id, &verify_err)) {
      Logger::Instance().Warn("search login rejected: " + verify_err);
      resp.set_code(chirp::common::AUTH_FAILED);
      SendPacket(session, gateway::LOGIN_RESP, pkt.sequence(), resp.SerializeAsString());
      return;
    }
  } else {
    // 脚手架登录：token 即 user_id（本地/烟测）。
    user_id = login_req.token();
  }

  if (user_id.empty()) {
    resp.set_code(chirp::common::INVALID_PARAM);
    SendPacket(session, gateway::LOGIN_RESP, pkt.sequence(), resp.SerializeAsString());
    return;
  }

  // 不进 SessionRegistry：同用户多端各持一条管道并存（bridge 的次级连接
  // 语义），本地映射记身份，断开由 HandleClose 清理。
  user_of_[session.get()] = user_id;
  CHIRP_COUNTER("chirp_search_logins_total", 1);

  resp.set_code(chirp::common::OK);
  resp.set_user_id(user_id);
  resp.set_session_id(GenerateSearchSessionId());
  SendPacket(session, gateway::LOGIN_RESP, pkt.sequence(), resp.SerializeAsString());
}

void SearchServer::HandleSearch(const std::shared_ptr<network::Session>& session,
                                const gateway::Packet& pkt) {
  CHIRP_COUNTER("chirp_search_queries_total", 1);

  const auto user_it = user_of_.find(session.get());
  if (user_it == user_of_.end()) {
    // 未登录的检索一律拒绝：身份只来自 LOGIN，不来自请求体。
    chirp::chat::SearchMessageResponse resp;
    resp.set_code(chirp::common::AUTH_FAILED);
    SendPacket(session, gateway::SEARCH_MESSAGE_RESP, pkt.sequence(),
               resp.SerializeAsString());
    return;
  }
  const std::string& user_id = user_it->second;

  chirp::chat::SearchMessageRequest req;
  chirp::chat::SearchMessageResponse resp;
  if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
    resp.set_code(chirp::common::INVALID_PARAM);
    resp.set_has_more(false);
    SendPacket(session, gateway::SEARCH_MESSAGE_RESP, pkt.sequence(),
               resp.SerializeAsString());
    return;
  }

  MessageSearchIndex::Query query;
  query.keyword = req.keyword();
  query.channel_id = req.channel_id();
  for (const auto t : req.content_types()) {
    query.content_types.push_back(static_cast<int>(t));
  }
  query.before_timestamp = req.before_timestamp();
  query.before_message_id = req.before_message_id();
  query.limit = req.limit() <= 0 ? kDefaultLimit
                                 : std::min(req.limit(), kMaxLimit);

  if (BuildMatchPhrase(query.keyword).empty()) {
    resp.set_code(chirp::common::INVALID_PARAM);
    resp.set_has_more(false);
    SendPacket(session, gateway::SEARCH_MESSAGE_RESP, pkt.sequence(),
               resp.SerializeAsString());
    return;
  }

  // MySQL 不可用即失败关闭：索引可能滞后，宁给错误不给错结果。
  if (sync_ == nullptr) {
    resp.set_code(chirp::common::INTERNAL_ERROR);
    resp.set_has_more(false);
    SendPacket(session, gateway::SEARCH_MESSAGE_RESP, pkt.sequence(),
               resp.SerializeAsString());
    return;
  }

  std::string search_err;
  resp.set_code(chirp::common::OK);

  // 过滤语义：撤回/陈旧命中剔除（自愈删索引），PRIVATE 只对两端之一可见。
  // 关键点——游标按「返回的命中」推进，若一页命中全被过滤，客户端没有游标
  // 可推进（重发同查询=原地打转）。所以服务端在内部翻页：直到攒满 limit 条
  // 可见命中或索引扫尽。has_more = 原始命中未扫尽（下一页至少还有原始候选，
  // 客户端用它推进（ts,id）复合游标必然单调前进，天然有终）。
  size_t visible = 0;
  bool exhausted = false;
  int64_t cursor_ts = query.before_timestamp;
  std::string cursor_id = query.before_message_id;
  while (visible < static_cast<size_t>(query.limit) && !exhausted) {
    MessageSearchIndex::Query page = query;
    page.limit = query.limit;  // 每页多捞一条判定扫尽（Search 内部 +1）
    page.before_timestamp = cursor_ts;
    page.before_message_id = cursor_id;
    const MessageSearchIndex::QueryResult result = index_.Search(page, &search_err);
    if (!search_err.empty()) {
      Logger::Instance().Warn("search query failed: " + search_err);
      resp.Clear();
      resp.set_code(chirp::common::INTERNAL_ERROR);
      resp.set_has_more(false);
      SendPacket(session, gateway::SEARCH_MESSAGE_RESP, pkt.sequence(),
                 resp.SerializeAsString());
      return;
    }
    exhausted = !result.has_more;
    if (result.hits.empty()) {
      break;
    }
    cursor_ts = result.hits.back().timestamp;
    cursor_id = result.hits.back().message_id;

    // 批量取权威事实：撤回剔除（自愈删索引）+ PRIVATE 门槛 + 展示字段。
    std::vector<std::string> ids;
    ids.reserve(result.hits.size());
    for (const auto& hit : result.hits) {
      ids.push_back(hit.message_id);
    }
    std::map<std::string, MessageIndexSync::MessageFact> facts;
    std::string facts_err;
    if (!sync_->FetchMessageFacts(ids, &facts, &facts_err)) {
      Logger::Instance().Warn("search facts query failed: " + facts_err);
      resp.Clear();
      resp.set_code(chirp::common::INTERNAL_ERROR);
      resp.set_has_more(false);
      SendPacket(session, gateway::SEARCH_MESSAGE_RESP, pkt.sequence(),
                 resp.SerializeAsString());
      return;
    }

    for (auto& hit : result.hits) {
      const auto fact_it = facts.find(hit.message_id);
      if (fact_it == facts.end()) {
        // 索引有、库里没有：陈旧条目，顺手自愈（与撤回同路径）。
        std::string del_err;
        if (!index_.DeleteMessage(hit.message_id, &del_err)) {
          Logger::Instance().Warn("search self-heal delete failed: " + del_err);
        }
        continue;
      }
      const MessageIndexSync::MessageFact& fact = fact_it->second;
      if (fact.recalled) {
        std::string del_err;
        if (!index_.DeleteMessage(hit.message_id, &del_err)) {
          Logger::Instance().Warn("search self-heal delete failed: " + del_err);
        }
        continue;
      }
      // PRIVATE 可见性（其余频道与 GET_HISTORY 同基线：登录即可见）。
      if (hit.channel_type == static_cast<int>(chirp::chat::ChannelType::PRIVATE) &&
          !PrivateChannelVisible(hit.channel_id, user_id)) {
        continue;
      }
      if (visible < static_cast<size_t>(query.limit)) {
        chirp::chat::SearchMessageMatch* match = resp.add_matches();
        match->set_message_id(hit.message_id);
        match->set_channel_id(hit.channel_id);
        match->set_channel_type(static_cast<chirp::chat::ChannelType>(hit.channel_type));
        match->set_sender_id(fact.sender_id);
        match->set_sender_kind(static_cast<chirp::chat::SenderKind>(fact.sender_kind));
        match->set_msg_type(hit.msg_type);
        match->set_timestamp(hit.timestamp);
        // 正文回权威值：索引里的 content 只用于匹配，编辑后的正文以库为准。
        match->set_content(fact.content);
        ++visible;
      }
    }
  }
  // 原始命中未扫尽即还有下一页（哪怕余下候选可能全被过滤——下一页内部
  // 继续扫，最终会给出可见命中或 has_more=false，客户端侧必然收敛）。
  resp.set_has_more(!exhausted);

  SendPacket(session, gateway::SEARCH_MESSAGE_RESP, pkt.sequence(),
             resp.SerializeAsString());
}

}  // namespace search
}  // namespace chirp
