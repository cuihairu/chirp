#include "stream_broker.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <optional>
#include <unordered_map>
#include <utility>

#include "logger.h"
#include "proto/chat.pb.h"

namespace chirp::server_gateway {

namespace {
// [[id, [field, value, ...]], ...] -> StreamEntry list; defined below.
bool ParseEntryList(const chirp::network::RedisResp& arr, std::vector<StreamEntry>* out);
}  // namespace

bool ParseInjectEnvelope(const std::vector<std::string>& flat, StreamInjectEnvelope* out) {
  if (flat.size() % 2 != 0) {
    return false;
  }
  std::unordered_map<std::string, std::string> fields;
  for (size_t i = 0; i + 1 < flat.size(); i += 2) {
    fields.emplace(flat[i], flat[i + 1]);
  }
  const auto get = [&fields](const char* key) {
    const auto it = fields.find(key);
    return it == fields.end() ? std::string() : it->second;
  };

  std::string kind = get("sender_kind");
  if (kind.rfind("SENDER_", 0) == 0) {
    kind = kind.substr(7);
  }
  if (kind == "SYSTEM") {
    out->req.set_sender_kind(SENDER_SYSTEM);
  } else if (kind == "NPC") {
    out->req.set_sender_kind(SENDER_NPC);
  } else if (kind == "SERVICE") {
    out->req.set_sender_kind(SENDER_SERVICE);
  } else {
    return false;
  }

  const std::string channel = get("channel_type");
  int32_t channel_type = 0;
  if (channel == "PRIVATE") {
    channel_type = static_cast<int32_t>(chirp::chat::PRIVATE);
  } else if (channel == "TEAM") {
    channel_type = static_cast<int32_t>(chirp::chat::TEAM);
  } else if (channel == "GUILD") {
    channel_type = static_cast<int32_t>(chirp::chat::GUILD);
  } else if (channel == "WORLD") {
    channel_type = static_cast<int32_t>(chirp::chat::WORLD);
  } else {
    const char* begin = channel.c_str();
    char* end = nullptr;
    const long value = channel.empty() ? -1 : std::strtol(begin, &end, 10);
    if (end == begin || *end != '\0' || value < 0 || value > 3) {
      return false;
    }
    channel_type = static_cast<int32_t>(value);
  }
  out->req.set_channel_type(channel_type);

  out->req.set_sender_id(get("sender_id"));
  out->req.set_channel_id(get("channel_id"));
  out->req.set_receiver_id(get("receiver_id"));
  out->req.set_content(get("content"));
  // Fan-in game context (WP-8 slice 3); absent on legacy entries, which
  // keeps their direct-injection semantics.
  out->req.set_game_id(get("game_id"));
  out->service_id = get("service_id");
  out->reply_to = get("reply_to");
  return true;
}

bool ParseXReadGroupResp(const chirp::network::RedisResp& resp, std::vector<StreamEntry>* out) {
  out->clear();
  if (resp.type == chirp::network::RedisResp::Type::kNull) {
    return true;  // BLOCK timed out without data
  }
  if (resp.type != chirp::network::RedisResp::Type::kArray || resp.array.size() != 1 ||
      resp.array[0].type != chirp::network::RedisResp::Type::kArray ||
      resp.array[0].array.size() != 2) {
    return false;
  }
  return ParseEntryList(resp.array[0].array[1], out);
}

bool ParseXAutoClaimResp(const chirp::network::RedisResp& resp, std::vector<StreamEntry>* out) {
  out->clear();
  // Element 3 (deleted ids) only exists on Redis 7+; index into element 2.
  if (resp.type != chirp::network::RedisResp::Type::kArray || resp.array.size() < 2) {
    return false;
  }
  return ParseEntryList(resp.array[1], out);
}

namespace {

using chirp::common::Logger;
using chirp::network::BuildRedisCommand;
using chirp::network::RedisResp;
using RedisRespParser = chirp::network::RedisRespParser;

// Per-connection loop state; Run() fills it from its members once.
struct LoopCtx {
  const StreamBrokerConfig& config;
  StreamBrokerConsumer::InjectHandler& handle;
  uint64_t& inject_seq;
  std::string inject_prefix;
};

const std::string* FindField(const std::vector<std::string>& flat, const std::string& name) {
  if (flat.size() % 2 != 0) {
    return nullptr;
  }
  for (size_t i = 0; i + 1 < flat.size(); i += 2) {
    if (flat[i] == name) {
      return &flat[i + 1];
    }
  }
  return nullptr;
}

// [[id, [field, value, ...]], ...]
bool ParseEntryList(const chirp::network::RedisResp& arr, std::vector<StreamEntry>* out) {
  if (arr.type != RedisResp::Type::kArray) {
    return false;
  }
  out->clear();
  for (const auto& item : arr.array) {
    if (item.type != RedisResp::Type::kArray || item.array.size() != 2 ||
        item.array[1].type != RedisResp::Type::kArray) {
      return false;
    }
    StreamEntry entry;
    entry.id = item.array[0].str;
    for (const auto& v : item.array[1].array) {
      entry.fields.push_back(v.str);
    }
    out->push_back(std::move(entry));
  }
  return true;
}

std::optional<RedisResp> RoundTrip(asio::ip::tcp::socket& sock,
                                   const std::vector<std::string>& args) {
  asio::error_code ec;
  asio::write(sock, asio::buffer(BuildRedisCommand(args)), ec);
  RedisRespParser parser;
  std::array<uint8_t, 4096> buf;
  for (;;) {
    if (auto value = parser.Pop()) {
      return value;
    }
    const size_t n = sock.read_some(asio::buffer(buf), ec);
    if (ec) {
      return std::nullopt;
    }
    parser.Append(buf.data(), n);
  }
}

bool ConnectSocket(asio::io_context& io, asio::ip::tcp::socket& sock,
                   const StreamBrokerConfig& config) {
  asio::error_code ec;
  asio::ip::tcp::resolver resolver(io);
  const auto endpoints = resolver.resolve(config.redis_host, std::to_string(config.redis_port), ec);
  if (ec) {
    return false;
  }
  asio::connect(sock, endpoints, ec);
  return !ec;
}

// Idempotent: an existing group answers BUSYGROUP, which is fine.
bool EnsureGroup(asio::ip::tcp::socket& sock, const StreamBrokerConfig& config) {
  const auto r = RoundTrip(sock, {"XGROUP", "CREATE", config.stream, config.group, "0", "MKSTREAM"});
  if (!r) {
    return false;
  }
  if (r->type == RedisResp::Type::kSimpleString && r->str == "OK") {
    return true;
  }
  return r->type == RedisResp::Type::kError && r->str.find("BUSYGROUP") != std::string::npos;
}

bool ReadNew(asio::ip::tcp::socket& sock, const StreamBrokerConfig& config,
             std::vector<StreamEntry>* out) {
  const auto r = RoundTrip(sock, {"XREADGROUP", "GROUP", config.group, config.consumer, "COUNT",
                                  "16", "BLOCK", std::to_string(config.block_ms), "STREAMS",
                                  config.stream, ">"});
  if (!r) {
    return false;
  }
  return ParseXReadGroupResp(*r, out);
}

bool ProcessEntry(asio::ip::tcp::socket& sock, LoopCtx& ctx, const StreamEntry& entry) {
  MessageInjectResponse resp;
  const std::string* inject_id_field = FindField(entry.fields, "inject_id");
  std::string inject_id = inject_id_field != nullptr ? *inject_id_field : std::string();

  StreamInjectEnvelope env;
  if (!ParseInjectEnvelope(entry.fields, &env)) {
    resp.set_code(chirp::common::INVALID_PARAM);
    resp.set_inject_id(inject_id);
    Logger::Instance().Warn("stream-broker dropping unmappable stream entry " + entry.id);
  } else {
    const auto secret = ctx.config.service_secrets.find(env.service_id);
    const std::string* secret_field = FindField(entry.fields, "secret");
    if (secret == ctx.config.service_secrets.end() || secret_field == nullptr ||
        *secret_field != secret->second) {
      resp.set_code(chirp::common::AUTH_FAILED);
      resp.set_inject_id(inject_id);
      Logger::Instance().Warn("stream-broker rejected credentials for service " + env.service_id);
    } else {
      if (inject_id.empty()) {
        inject_id = ctx.inject_prefix + "-" + std::to_string(++ctx.inject_seq);
      }
      env.req.set_inject_id(inject_id);
      resp = ctx.handle(env.req);
    }
  }

  // SERVER_UNAVAILABLE keeps the entry pending: no ack, no reply - the PEL
  // sweep redelivers it once the chat service is back.
  if (resp.code() == chirp::common::SERVER_UNAVAILABLE) {
    return true;
  }
  // Best-effort ack: a failed write surfaces as a transport error on the
  // next command, and the entry replays from the PEL (at-least-once).
  const auto ack = RoundTrip(sock, {"XACK", ctx.config.stream, ctx.config.group, entry.id});
  if (!ack) {
    return false;
  }
  const std::string* reply_to = FindField(entry.fields, "reply_to");
  if (reply_to != nullptr && !reply_to->empty()) {
    const auto wr = RoundTrip(sock, {"XADD", *reply_to, "inject_id", inject_id, "code",
                                     chirp::common::ErrorCode_Name(resp.code())});
    if (!wr) {
      return false;
    }
  }
  return true;
}

bool SweepPending(asio::ip::tcp::socket& sock, LoopCtx& ctx) {
  const auto r = RoundTrip(sock, {"XAUTOCLAIM", ctx.config.stream, ctx.config.group,
                                  ctx.config.consumer,
                                  std::to_string(ctx.config.claim_min_idle_ms), "0-0", "COUNT",
                                  "16"});
  if (!r) {
    return false;
  }
  std::vector<StreamEntry> claimed;
  if (!ParseXAutoClaimResp(*r, &claimed)) {
    Logger::Instance().Warn("stream-broker got a malformed XAUTOCLAIM reply; reconnecting");
    return false;
  }
  for (const auto& entry : claimed) {
    if (!ProcessEntry(sock, ctx, entry)) {
      return false;
    }
  }
  return true;
}

}  // namespace

StreamBrokerConsumer::StreamBrokerConsumer(StreamBrokerConfig config, InjectHandler handle_inject)
    : config_(std::move(config)), handle_inject_(std::move(handle_inject)) {}

StreamBrokerConsumer::~StreamBrokerConsumer() { Stop(); }

void StreamBrokerConsumer::Start() {
  if (started_ || stopping_) {
    return;
  }
  started_ = true;
  thread_ = std::thread([this] { Run(); });
}

void StreamBrokerConsumer::Stop() {
  if (!started_ || stopping_) {
    return;
  }
  stopping_ = true;
  // Shutting the socket down wakes a BLOCKed XREADGROUP read so the join
  // below completes promptly.
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (live_socket_ != nullptr) {
      asio::error_code ec;
      live_socket_->shutdown(asio::ip::tcp::socket::shutdown_both, ec);
    }
  }
  if (thread_.joinable()) {
    thread_.join();
  }
}

void StreamBrokerConsumer::SetLive(asio::ip::tcp::socket* s) {
  std::lock_guard<std::mutex> lock(mu_);
  live_socket_ = s;
}

void StreamBrokerConsumer::SleepInterruptible(int ms) {
  for (int waited = 0; waited < ms && !stopping_; waited += 50) {
    std::this_thread::sleep_for(std::chrono::milliseconds(std::min(50, ms - waited)));
  }
}

void StreamBrokerConsumer::Run() {
  asio::io_context io;
  LoopCtx ctx{config_, handle_inject_, inject_seq_,
              config_.consumer.empty() ? std::string("brk") : config_.consumer};

  while (!stopping_) {
    asio::ip::tcp::socket sock(io);
    SetLive(&sock);
    if (!ConnectSocket(io, sock, config_)) {
      SetLive(nullptr);
      if (stopping_) {
        break;
      }
      SleepInterruptible(config_.reconnect_delay_ms);
      continue;
    }
    Logger::Instance().Info("stream-broker connected to redis at " + config_.redis_host + ":" +
                            std::to_string(config_.redis_port));

    bool alive = EnsureGroup(sock, config_);
    auto next_claim = std::chrono::steady_clock::now();
    while (alive && !stopping_) {
      if (std::chrono::steady_clock::now() >= next_claim) {
        alive = SweepPending(sock, ctx);
        next_claim = std::chrono::steady_clock::now() +
                     std::chrono::milliseconds(config_.claim_interval_ms);
        continue;
      }
      std::vector<StreamEntry> entries;
      if (!ReadNew(sock, config_, &entries)) {
        break;
      }
      if (entries.empty()) {
        // No data: sleep one tick so a non-blocking server cannot spin the
        // loop; real Redis already waited BLOCK milliseconds.
        SleepInterruptible(config_.block_ms);
        continue;
      }
      for (const auto& entry : entries) {
        if (!ProcessEntry(sock, ctx, entry)) {
          alive = false;
          break;
        }
      }
    }
    SetLive(nullptr);
    asio::error_code ec;
    sock.close(ec);
    if (stopping_) {
      break;
    }
    Logger::Instance().Warn("stream-broker redis connection lost; retrying");
    SleepInterruptible(config_.reconnect_delay_ms);
  }
}

}  // namespace chirp::server_gateway
