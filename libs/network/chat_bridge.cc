#include "network/chat_bridge.h"

#include <array>
#include <chrono>
#include <deque>
#include <utility>
#include <vector>

#include "logger.h"
#include "network/byte_order.h"
#include "network/protobuf_framing.h"
#include "proto/auth.pb.h"
#include "proto/common.pb.h"
#include "proto/gateway.pb.h"
#include "proto/game_server_gateway.pb.h"

namespace chirp::gateway {
namespace {

// Shared frame cap with the other internal-plane peers (server_gateway_peer).
constexpr uint32_t kMaxFrameBytes = 4u * 1024u * 1024u;
// The whole handshake (dial + service auth + login replay) must finish within
// this budget or the client is kicked with a clear reason instead of hanging.
constexpr auto kHandshakeTimeout = std::chrono::seconds(5);
// Bounded queue for 2xxx packets that arrive while the handshake is running.
constexpr size_t kMaxPendingPackets = 64;

std::vector<uint8_t> FramePacket(const chirp::gateway::Packet& pkt) {
  auto framed = chirp::network::ProtobufFraming::Encode(pkt);
  return framed;
}

void SendKickAndClose(const std::shared_ptr<chirp::network::Session>& client,
                      const std::string& reason) {
  chirp::auth::KickNotify kick;
  kick.set_reason(reason.empty() ? "kicked" : reason);

  chirp::gateway::Packet kick_pkt;
  kick_pkt.set_msg_id(chirp::gateway::KICK_NOTIFY);
  kick_pkt.set_sequence(0);
  kick_pkt.set_body(kick.SerializeAsString());

  auto framed = FramePacket(kick_pkt);
  client->SendAndClose(std::string(reinterpret_cast<const char*>(framed.data()), framed.size()));
}

} // namespace

struct ChatBridge::InternalConn : std::enable_shared_from_this<InternalConn> {
  enum class State { kConnecting, kAuthenticating, kLoggingIn, kReady };

  InternalConn(asio::io_context& io, ChatBridge* bridge,
               std::shared_ptr<chirp::network::Session> client_in)
      : socket(io),
        handshake_timer(io),
        owner(bridge),
        client(std::move(client_in)) {}

  asio::ip::tcp::socket socket;
  asio::steady_timer handshake_timer;
  ChatBridge* owner;
  std::weak_ptr<chirp::network::Session> client;

  State state{State::kConnecting};
  // closing: the internal side is down (Detach / failure); in-flight
  // completion handlers must neither fail the client nor re-close.
  // failed: FailClient has already run for this connection - the map entry
  // is gone and the real client has been kicked exactly once.
  bool closing{false};
  bool failed{false};
  int64_t next_seq{1};

  std::array<uint8_t, 4> header{};
  std::string body;

  // One-in-flight async write queue: a blocking write on the gateway's main
  // io thread would stall every client, and interleaved async_writes would
  // corrupt the frame stream.
  std::deque<std::vector<uint8_t>> write_q;
  bool writing{false};

  // 2xxx packets captured between Attach() and ready, flushed in order.
  std::deque<std::vector<uint8_t>> pending;

  void Close() {
    if (closing) {
      return;
    }
    closing = true;
    asio::error_code ignored;
    socket.shutdown(asio::ip::tcp::socket::shutdown_both, ignored);
    socket.close(ignored);
    handshake_timer.cancel();
  }

  void EnqueueFrame(std::vector<uint8_t> framed) {
    write_q.push_back(std::move(framed));
    if (!writing) {
      DoWrite();
    }
  }

  void DoWrite() {
    if (closing || write_q.empty()) {
      return;
    }
    writing = true;
    auto self = shared_from_this();
    asio::async_write(
        socket, asio::buffer(write_q.front().data(), write_q.front().size()),
        [self](const std::error_code& ec, std::size_t) {
          self->writing = false;
          if (ec) {
            self->OnConnectionLost();
            return;
          }
          self->write_q.pop_front();
          self->DoWrite();
        });
  }

  void ReadHeader() {
    auto self = shared_from_this();
    asio::async_read(socket, asio::buffer(header), [self](const std::error_code& ec, std::size_t) {
      if (self->closing || ec) {
        self->OnConnectionLost();
        return;
      }
      const uint32_t size = chirp::network::ReadU32BE(self->header.data());
      if (size == 0 || size > kMaxFrameBytes) {
        chirp::common::Logger::Instance().Warn("chat bridge: invalid frame size from chat");
        self->OnConnectionLost();
        return;
      }
      self->ReadBody(size);
    });
  }

  void ReadBody(uint32_t size) {
    auto self = shared_from_this();
    body.resize(size);
    asio::async_read(socket, asio::buffer(body.data(), body.size()),
                     [self](const std::error_code& ec, std::size_t) {
                       if (self->closing || ec) {
                         self->OnConnectionLost();
                         return;
                       }
                       chirp::gateway::Packet pkt;
                       if (!pkt.ParseFromArray(self->body.data(),
                                               static_cast<int>(self->body.size()))) {
                         chirp::common::Logger::Instance().Warn(
                             "chat bridge: failed to parse Packet from chat");
                         self->OnConnectionLost();
                         return;
                       }
                       self->HandleFrame(pkt);
                       if (!self->closing) {
                         self->ReadHeader();
                       }
                     });
  }

  void HandleFrame(const chirp::gateway::Packet& pkt) {
    switch (state) {
    case State::kAuthenticating: {
      if (pkt.msg_id() != chirp::gateway::SERVER_AUTH_RESP) {
        chirp::common::Logger::Instance().Warn(
            "chat bridge: unexpected first frame from chat, msg_id=" +
            std::to_string(static_cast<int>(pkt.msg_id())));
        owner->FailClient(*this, "chat unavailable");
        return;
      }
      chirp::game_server_gateway::ServerAuthResponse resp;
      if (!resp.ParseFromString(pkt.body()) || resp.code() != chirp::common::OK) {
        chirp::common::Logger::Instance().Warn("chat bridge: chat rejected the service auth");
        owner->FailClient(*this, "chat unavailable");
        return;
      }
      state = State::kLoggingIn;
      // The handshake timer keeps running: login must land within the same
      // budget, a half-open dial should not strand the client either way.
      break;
    }
    case State::kLoggingIn: {
      if (pkt.msg_id() != chirp::gateway::LOGIN_RESP) {
        chirp::common::Logger::Instance().Warn(
            "chat bridge: unexpected frame while logging in, msg_id=" +
            std::to_string(static_cast<int>(pkt.msg_id())));
        owner->FailClient(*this, "chat unavailable");
        return;
      }
      chirp::auth::LoginResponse resp;
      if (!resp.ParseFromString(pkt.body()) || resp.code() != chirp::common::OK) {
        chirp::common::Logger::Instance().Warn("chat bridge: chat rejected the login replay");
        owner->FailClient(*this, "chat session rejected");
        return;
      }
      state = State::kReady;
      handshake_timer.cancel();
      while (!pending.empty()) {
        EnqueueFrame(std::move(pending.front()));
        pending.pop_front();
      }
      break;
    }
    case State::kReady: {
      // Verbatim pipe: responses and every push chat emits for this user go
      // straight back to the one client behind this connection.
      if (auto c = client.lock()) {
        auto framed = FramePacket(pkt);
        c->Send(std::string(reinterpret_cast<const char*>(framed.data()), framed.size()));
      }
      break;
    }
    case State::kConnecting:
      break;  // unreachable: reads start only after connect
    }
  }

  void OnConnectionLost() {
    if (closing) {
      return;  // Detach() or a prior failure already handled this connection
    }
    owner->FailClient(*this, state == State::kReady ? "chat session lost" : "chat unavailable");
  }
};

ChatBridge::ChatBridge(asio::io_context& io, std::string host, uint16_t port,
                       std::string service_id, std::string service_secret)
    : io_(io),
      enabled_(!host.empty()),
      host_(std::move(host)),
      port_(port),
      service_id_(std::move(service_id)),
      service_secret_(std::move(service_secret)) {}

ChatBridge::~ChatBridge() {
  for (auto& entry : conns_) {
    entry.second->Close();
  }
}

ChatBridge::InternalConn& ChatBridge::StartConn(
    const std::shared_ptr<chirp::network::Session>& client, const std::string& token,
    const std::string& device_id) {
  auto conn = std::make_shared<InternalConn>(io_, this, client);
  auto [it, inserted] = conns_.emplace(client.get(), conn);
  if (!inserted) {
    // Defensive: a second Attach for a live client replaces the old pipe.
    it->second->Close();
    it->second = conn;
  }

  // Bounded handshake: if chat never answers, the client is kicked with a
  // reason instead of leaking a half-open pipe.
  conn->handshake_timer.expires_after(kHandshakeTimeout);
  std::weak_ptr<InternalConn> weak = conn;
  conn->handshake_timer.async_wait([weak](const std::error_code& ec) {
    auto conn = weak.lock();
    if (ec || !conn || conn->closing || conn->failed ||
        conn->state == InternalConn::State::kReady) {
      return;
    }
    chirp::common::Logger::Instance().Warn("chat bridge: handshake timed out");
    conn->owner->FailClient(*conn, "chat unavailable");
  });

  auto resolver = std::make_shared<asio::ip::tcp::resolver>(io_);
  resolver->async_resolve(host_, std::to_string(port_),
                          [this, conn, resolver, token, device_id](
                              const std::error_code& ec,
                              asio::ip::tcp::resolver::results_type results) {
                            if (conn->closing) {
                              return;
                            }
                            if (ec) {
                              chirp::common::Logger::Instance().Warn(
                                  "chat bridge: resolve failed: " + ec.message());
                              FailClient(*conn, "chat unavailable");
                              return;
                            }
                            asio::async_connect(
                                conn->socket, results,
                                [this, conn, token, device_id](
                                    const std::error_code& ec2,
                                    const asio::ip::tcp::endpoint&) {
                                  if (conn->closing) {
                                    return;
                                  }
                                  if (ec2) {
                                    chirp::common::Logger::Instance().Warn(
                                        "chat bridge: connect failed: " + ec2.message());
                                    FailClient(*conn, "chat unavailable");
                                    return;
                                  }
                                  conn->state = InternalConn::State::kAuthenticating;
                                  chirp::game_server_gateway::ServerAuthRequest auth;
                                  auth.set_service_id(service_id_);
                                  auth.set_secret(service_secret_);
                                  auth.set_protocol_version(1);
                                  chirp::gateway::Packet auth_pkt;
                                  auth_pkt.set_msg_id(chirp::gateway::SERVER_AUTH_REQ);
                                  auth_pkt.set_sequence(0);
                                  auth_pkt.set_body(auth.SerializeAsString());
                                  conn->EnqueueFrame(FramePacket(auth_pkt));

                                  chirp::auth::LoginRequest login;
                                  login.set_token(token);
                                  login.set_device_id(device_id);
                                  chirp::gateway::Packet login_pkt;
                                  login_pkt.set_msg_id(chirp::gateway::LOGIN_REQ);
                                  login_pkt.set_sequence(conn->next_seq++);
                                  login_pkt.set_body(login.SerializeAsString());
                                  conn->EnqueueFrame(FramePacket(login_pkt));

                                  conn->ReadHeader();
                                });
                          });
  return *conn;
}

void ChatBridge::Attach(const std::shared_ptr<chirp::network::Session>& client,
                        const std::string& token, const std::string& device_id) {
  if (!enabled_) {
    return;
  }
  StartConn(client, token, device_id);
}

void ChatBridge::ForwardToChat(const chirp::network::Session* client,
                               const chirp::gateway::Packet& pkt) {
  if (!enabled_) {
    return;
  }
  const auto it = conns_.find(client);
  if (it == conns_.end() || it->second->closing) {
    return;  // handshake already failed; the client is on its way out
  }
  auto& conn = *it->second;
  auto framed = FramePacket(pkt);
  if (conn.state == InternalConn::State::kReady) {
    conn.EnqueueFrame(std::move(framed));
    return;
  }
  if (conn.pending.size() >= kMaxPendingPackets) {
    chirp::common::Logger::Instance().Warn(
        "chat bridge: pending queue full, dropping a 2xxx packet");
    return;
  }
  conn.pending.push_back(std::move(framed));
}

void ChatBridge::Detach(const chirp::network::Session* client) {
  const auto it = conns_.find(client);
  if (it == conns_.end()) {
    return;
  }
  it->second->Close();
  conns_.erase(it);
}

void ChatBridge::FailClient(InternalConn& conn, const std::string& reason) {
  if (conn.failed || conn.closing) {
    return;
  }
  conn.failed = true;
  conn.Close();
  if (auto client = conn.client.lock()) {
    conns_.erase(client.get());
    SendKickAndClose(client, reason);
  }
}

} // namespace chirp::gateway
