#include <gtest/gtest.h>

#include <sys/socket.h>
#include <unistd.h>

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <asio.hpp>
#include <asio/ssl.hpp>

#include "network/session.h"
#include "network/session_registry.h"
#include "network/ssl_context.h"
#include "network/tcp_server.h"
#include "network/tls_server.h"
#include "network/tls_session.h"
#include "network/websocket_frame.h"
#include "network/websocket_server.h"
#include "network/websocket_util.h"

using chirp::network::Session;

namespace {

// ---------------------------------------------------------------------------
// Loopback TLS harness. Mirrors LoopbackLinkTest in network_session_test.cc:
// the server io runs on a helper thread (io -> server -> thread declaration
// order, joined before destructors), callbacks funnel into mutex-guarded
// sinks, and the client is fully synchronous on an io_context that is never
// run. Ports come from server.Port() so nothing is bound twice.
// ---------------------------------------------------------------------------

std::string FrameBytes(const std::string& payload) {
  std::string out;
  const uint32_t n = static_cast<uint32_t>(payload.size());
  out.push_back(static_cast<char>((n >> 24) & 0xFF));
  out.push_back(static_cast<char>((n >> 16) & 0xFF));
  out.push_back(static_cast<char>((n >> 8) & 0xFF));
  out.push_back(static_cast<char>(n & 0xFF));
  out += payload;
  return out;
}

// TSan-clean sinks: callbacks fire on the server io thread while the test
// thread polls, so every access takes the same mutex.
struct LockedFrames {
  std::mutex mu;
  std::vector<std::string> frames;

  void Push(std::string s) {
    std::lock_guard<std::mutex> lock(mu);
    frames.push_back(std::move(s));
  }
  bool Empty() {
    std::lock_guard<std::mutex> lock(mu);
    return frames.empty();
  }
  bool SizeIs(size_t n) {
    std::lock_guard<std::mutex> lock(mu);
    return frames.size() == n;
  }
  std::string At(size_t i) {
    std::lock_guard<std::mutex> lock(mu);
    return frames.at(i);
  }
};

struct LockedSession {
  std::mutex mu;
  std::shared_ptr<Session> session;

  void Set(std::shared_ptr<Session> s) {
    std::lock_guard<std::mutex> lock(mu);
    session = std::move(s);
  }
  std::shared_ptr<Session> Get() {
    std::lock_guard<std::mutex> lock(mu);
    return session;
  }
};

void SleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

// Stop() posts the acceptor close through the strand, so it lands
// asynchronously; poll until a fresh connect gets refused, which both proves
// the close ran and gives the posted lambda a deterministic chance to.
bool WaitConnectRefused(uint16_t port) {
  for (int i = 0; i < 300; ++i) {
    SleepMs(2);
    asio::io_context probe_io;
    asio::ip::tcp::socket probe(probe_io);
    asio::error_code ec;
    probe.connect(asio::ip::tcp::endpoint(asio::ip::make_address("127.0.0.1"), port), ec);
    if (ec == asio::error::connection_refused) {
      return true;
    }
  }
  return false;
}

// Blocking TLS client: synchronous asio calls on a never-run io_context,
// with a receive timeout so a stalled server cannot hang the test.
class SyncTlsClient {
 public:
  SyncTlsClient() : ctx_(asio::ssl::context::tls_client) {}

  bool Connect(uint16_t port) {
    asio::error_code ec;
    socket_ = std::make_unique<asio::ssl::stream<asio::ip::tcp::socket>>(io_, ctx_);
    SetReadTimeout(3000);
    socket_->lowest_layer().connect(
        asio::ip::tcp::endpoint(asio::ip::make_address("127.0.0.1"), port), ec);
    if (ec) {
      return false;
    }
    socket_->handshake(asio::ssl::stream_base::client, ec);
    return !ec;
  }

  bool Write(const std::string& bytes) {
    asio::error_code ec;
    asio::write(*socket_, asio::buffer(bytes), ec);
    return !ec;
  }

  std::string ReadExactly(size_t n, int timeout_ms = 2000) {
    std::string out(n, '\0');
    size_t got = 0;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (got < n) {
      asio::error_code ec;
      got += asio::read(*socket_, asio::buffer(out.data() + got, n - got), ec);
      if (ec || std::chrono::steady_clock::now() > deadline) {
        break;
      }
    }
    out.resize(got);
    return out;
  }

  std::string ReadUntil(const std::string& delim, int timeout_ms = 2000) {
    std::string acc;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (acc.find(delim) == std::string::npos) {
      char buf[512];
      asio::error_code ec;
      size_t r = socket_->read_some(asio::buffer(buf), ec);
      if (r > 0) {
        acc.append(buf, r);
      }
      if ((ec && ec != asio::error::would_block) ||
          std::chrono::steady_clock::now() > deadline) {
        break;
      }
    }
    return acc;
  }

  // True once the server side closed: any transport error other than the
  // receive timeout (clean close_notify reads back as eof).
  bool WaitEof(int timeout_ms = 2000) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
      char b;
      asio::error_code ec;
      asio::read(*socket_, asio::buffer(&b, 1), ec);
      if (ec == asio::error::would_block) {
        continue;
      }
      if (ec) {
        return true;
      }
    }
    return false;
  }

  void Disconnect() {
    if (!socket_) {
      return;
    }
    asio::error_code ec;
    socket_->lowest_layer().close(ec);
  }

 private:
  void SetReadTimeout(int ms) {
    struct timeval tv {};
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    ::setsockopt(socket_->lowest_layer().native_handle(), SOL_SOCKET, SO_RCVTIMEO, &tv,
                 sizeof(tv));
  }

  asio::io_context io_;  // never run: all calls are synchronous
  asio::ssl::context ctx_;
  std::unique_ptr<asio::ssl::stream<asio::ip::tcp::socket>> socket_;
};

class TlsEdgeTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    // Fresh self-signed cert + key (10 years) in a private temp dir, loaded
    // through the production MakeServerSslContext so its success path is
    // covered by the fixture itself.
    std::string tmpl = "/tmp/chirp_tls_test_XXXXXX";
    std::vector<char> dir(tmpl.begin(), tmpl.end());
    dir.push_back('\0');
    ASSERT_NE(mkdtemp(dir.data()), nullptr) << "mkdtemp failed: " << errno;
    cert_dir_ = dir.data();
    cert_path_ = cert_dir_ + "/cert.pem";
    key_path_ = cert_dir_ + "/key.pem";

    ASSERT_TRUE(WriteSelfSignedCert(cert_path_, key_path_));

    std::string error;
    ssl_ = chirp::network::MakeServerSslContext(cert_path_, key_path_, &error);
    ASSERT_NE(ssl_, nullptr) << error;
  }

  static void TearDownTestSuite() {
    ssl_.reset();
    if (!cert_dir_.empty()) {
      ::unlink(cert_path_.c_str());
      ::unlink(key_path_.c_str());
      ::rmdir(cert_dir_.c_str());
    }
  }

  static bool WriteSelfSignedCert(const std::string& cert_path, const std::string& key_path) {
    EVP_PKEY* pkey = EVP_RSA_gen(2048);
    if (!pkey) {
      return false;
    }
    X509* x509 = X509_new();
    if (!x509) {
      EVP_PKEY_free(pkey);
      return false;
    }
    bool ok = false;
    do {
      ASN1_INTEGER_set(X509_get_serialNumber(x509), 1);
      X509_gmtime_adj(X509_getm_notBefore(x509), 0);
      X509_gmtime_adj(X509_getm_notAfter(x509), 10L * 365 * 24 * 3600);
      X509_set_pubkey(x509, pkey);
      X509_NAME* name = X509_get_subject_name(x509);
      X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                                 reinterpret_cast<const unsigned char*>("chirp-tls-test"), -1, -1,
                                 0);
      X509_set_issuer_name(x509, name);
      if (X509_sign(x509, pkey, EVP_sha256()) == 0) {
        break;
      }
      FILE* cf = fopen(cert_path.c_str(), "wb");
      ok = cf != nullptr && PEM_write_X509(cf, x509) == 1;
      if (cf) {
        fclose(cf);
      }
      if (!ok) {
        break;
      }
      FILE* kf = fopen(key_path.c_str(), "wb");
      ok = kf != nullptr &&
           PEM_write_PrivateKey(kf, pkey, nullptr, nullptr, 0, nullptr, nullptr) == 1;
      if (kf) {
        fclose(kf);
      }
    } while (false);
    X509_free(x509);
    EVP_PKEY_free(pkey);
    return ok;
  }

  static std::string cert_dir_;
  static std::string cert_path_;
  static std::string key_path_;
  static std::shared_ptr<asio::ssl::context> ssl_;
};

std::string TlsEdgeTest::cert_dir_;
std::string TlsEdgeTest::cert_path_;
std::string TlsEdgeTest::key_path_;
std::shared_ptr<asio::ssl::context> TlsEdgeTest::ssl_;

// ---------------------------------------------------------------------------
// TLS-TCP edge
// ---------------------------------------------------------------------------

TEST_F(TlsEdgeTest, TlsTcpServerEchoesLengthPrefixedFrames) {
  asio::io_context io;
  LockedFrames got;
  std::atomic<bool> closed{false};

  // Port 0: the ctor caches the kernel-picked ephemeral port.
  chirp::network::TlsTcpServer server(
      io, /*port=*/0, ssl_,
      [&](std::shared_ptr<Session> s, std::string&& payload) {
        got.Push(std::move(payload));
        s->Send(FrameBytes("pong"));
      },
      [&](std::shared_ptr<Session>) { closed.store(true); });
  server.Start();
  const uint16_t port = server.Port();
  ASSERT_NE(port, 0);

  std::thread server_thread([&io] { io.run(); });

  SyncTlsClient client;
  ASSERT_TRUE(client.Connect(port)) << "TLS handshake against the server failed";
  ASSERT_TRUE(client.Write(FrameBytes("ping")));

  for (int i = 0; i < 300 && got.Empty(); ++i) {
    SleepMs(2);
  }
  ASSERT_TRUE(got.SizeIs(1));
  EXPECT_EQ(got.At(0), "ping");

  EXPECT_EQ(client.ReadExactly(4 + 4), FrameBytes("pong"));

  client.Disconnect();
  server.Stop();
  io.stop();
  server_thread.join();
}

TEST_F(TlsEdgeTest, PlainClientToTlsPortIsRejectedCleanly) {
  asio::io_context io;
  std::atomic<int> closes{0};

  chirp::network::TlsTcpServer server(
      io, /*port=*/0, ssl_,
      nullptr,
      [&](std::shared_ptr<Session>) { closes.fetch_add(1); });
  server.Start();
  const uint16_t port = server.Port();
  std::thread server_thread([&io] { io.run(); });

  {
    // Plaintext bytes at a TLS port: the server handshake must fail, the
    // session must close exactly once, and the listener must survive.
    asio::io_context plain_io;  // never run
    asio::ip::tcp::socket s(plain_io);
    asio::error_code ec;
    s.connect(asio::ip::tcp::endpoint(asio::ip::make_address("127.0.0.1"), port), ec);
    ASSERT_FALSE(ec);
    asio::write(s, asio::buffer(std::string("GET / HTTP/1.1\r\n\r\n")), ec);
    ASSERT_FALSE(ec);
    for (int i = 0; i < 300 && closes.load() == 0; ++i) {
      SleepMs(2);
    }
    EXPECT_EQ(closes.load(), 1);
    s.close(ec);
  }

  SyncTlsClient client;
  EXPECT_TRUE(client.Connect(port)) << "listener did not survive the bad handshake";

  client.Disconnect();
  server.Stop();
  io.stop();
  server_thread.join();
}

TEST_F(TlsEdgeTest, TlsSendAndCloseRemoteAddressAndPeerHalfClosed) {
  asio::io_context io;
  LockedSession captured;
  std::atomic<bool> closed{false};

  chirp::network::TlsTcpServer server(
      io, /*port=*/0, ssl_,
      [&](std::shared_ptr<Session> s, std::string&&) { captured.Set(s); },
      [&](std::shared_ptr<Session>) { closed.store(true); });
  server.Start();
  const uint16_t port = server.Port();
  std::thread server_thread([&io] { io.run(); });

  SyncTlsClient client;
  ASSERT_TRUE(client.Connect(port));
  ASSERT_TRUE(client.Write(FrameBytes("hi")));
  for (int i = 0; i < 300 && !captured.Get(); ++i) {
    SleepMs(2);
  }
  auto session = captured.Get();
  ASSERT_TRUE(session);

  EXPECT_FALSE(session->IsClosed());
  // TLS specialization: half-close detection is not wired up (its only
  // consumer is the plaintext chat bridge) and must report false.
  EXPECT_FALSE(session->PeerHalfClosed());
  EXPECT_EQ(session->RemoteAddress(), "127.0.0.1");

  session->SendAndClose(FrameBytes("bye"));
  EXPECT_EQ(client.ReadExactly(4 + 3), FrameBytes("bye"));
  EXPECT_TRUE(client.WaitEof());

  for (int i = 0; i < 300 && !closed.load(); ++i) {
    SleepMs(2);
  }
  EXPECT_TRUE(closed.load());

  server.Stop();
  io.stop();
  server_thread.join();
}

TEST_F(TlsEdgeTest, TlsSessionBindsInSessionRegistry) {
  asio::io_context io;
  LockedSession captured;

  chirp::network::TlsTcpServer server(
      io, /*port=*/0, ssl_,
      [&](std::shared_ptr<Session> s, std::string&&) { captured.Set(s); },
      nullptr);
  server.Start();
  const uint16_t port = server.Port();
  std::thread server_thread([&io] { io.run(); });

  SyncTlsClient client;
  ASSERT_TRUE(client.Connect(port));
  ASSERT_TRUE(client.Write(FrameBytes("hi")));
  for (int i = 0; i < 300 && !captured.Get(); ++i) {
    SleepMs(2);
  }
  auto session = captured.Get();
  ASSERT_TRUE(session);

  // The registry is stream-agnostic: a TLS session binds and is found as
  // the same shared_ptr, exactly like a plain one.
  auto registry = std::make_shared<chirp::network::SessionRegistry>();
  auto previous = chirp::network::BindAuthenticatedSession(registry, "tls-user", "sess-1",
                                                           /*device_id=*/"", session);
  EXPECT_EQ(previous, nullptr);

  EXPECT_EQ(chirp::network::GetSession(registry, "tls-user", "default"), session);
  EXPECT_EQ(chirp::network::GetAuthenticatedSession(registry, session).user_id, "tls-user");

  EXPECT_TRUE(chirp::network::RemoveAuthenticatedSession(registry, session));
  EXPECT_EQ(chirp::network::GetSession(registry, "tls-user", "default"), nullptr);

  server.Stop();
  io.stop();
  server_thread.join();
}

// ---------------------------------------------------------------------------
// TLS-WebSocket edge (wss)
// ---------------------------------------------------------------------------

TEST_F(TlsEdgeTest, TlsWebSocketServerHandshakeAndEcho) {
  asio::io_context io;
  LockedFrames got;
  std::atomic<bool> closed{false};

  chirp::network::TlsWebSocketServer server(
      io, /*port=*/0, ssl_,
      [&](std::shared_ptr<Session> s, std::string&& payload) {
        got.Push(std::move(payload));
        s->Send(FrameBytes("pong"));
      },
      [&](std::shared_ptr<Session>) { closed.store(true); });
  server.Start();
  const uint16_t port = server.Port();
  std::thread server_thread([&io] { io.run(); });

  {
    // Plaintext bytes at the wss port: the TLS handshake fails, the session
    // closes exactly once, and the listener survives (same contract as the
    // TLS-TCP edge).
    asio::io_context plain_io;  // never run
    asio::ip::tcp::socket s(plain_io);
    asio::error_code ec;
    s.connect(asio::ip::tcp::endpoint(asio::ip::make_address("127.0.0.1"), port), ec);
    ASSERT_FALSE(ec);
    asio::write(s, asio::buffer(std::string("GET / HTTP/1.1\r\n\r\n")), ec);
    ASSERT_FALSE(ec);
    for (int i = 0; i < 300 && closed.load() == 0; ++i) {
      SleepMs(2);
    }
    EXPECT_EQ(closed.load(), 1);
    s.close(ec);
  }

  SyncTlsClient client;
  ASSERT_TRUE(client.Connect(port)) << "TLS handshake against the wss port failed";

  // Upgrade handshake travels inside the TLS tunnel.
  ASSERT_TRUE(client.Write(chirp::network::BuildWebSocketHandshake("localhost", port, "/ws")));
  const std::string resp = client.ReadUntil("\r\n\r\n");
  EXPECT_TRUE(chirp::network::IsWebSocketUpgradeSuccessful(resp)) << resp;

  ASSERT_TRUE(client.Write(chirp::network::BuildWebSocketFrame(0x2, FrameBytes("ping"),
                                                              /*mask=*/true)));
  for (int i = 0; i < 300 && got.Empty(); ++i) {
    SleepMs(2);
  }
  ASSERT_TRUE(got.SizeIs(1));
  EXPECT_EQ(got.At(0), "ping");

  // Server frames are unmasked: 2-byte header + 8-byte payload.
  const std::string wire = client.ReadExactly(2 + 4 + 4);
  chirp::network::WebSocketFrameParser parser;
  parser.Append(reinterpret_cast<const uint8_t*>(wire.data()), wire.size());
  auto f = parser.PopFrame();
  ASSERT_TRUE(f.has_value());
  EXPECT_EQ(f->opcode, 0x2);
  EXPECT_TRUE(f->fin);
  EXPECT_EQ(f->payload, FrameBytes("pong"));

  client.Disconnect();
  server.Stop();
  EXPECT_TRUE(WaitConnectRefused(port));
  io.stop();
  server_thread.join();
}

TEST_F(TlsEdgeTest, TlsWebSocketSessionCloseEndpointAndSendAndClose) {
  asio::io_context io;
  LockedSession captured;
  std::atomic<bool> closed{false};

  chirp::network::TlsWebSocketServer server(
      io, /*port=*/0, ssl_,
      [&](std::shared_ptr<Session> s, std::string&&) { captured.Set(s); },
      [&](std::shared_ptr<Session>) { closed.store(true); });
  server.Start();
  const uint16_t port = server.Port();
  std::thread server_thread([&io] { io.run(); });

  SyncTlsClient client;
  ASSERT_TRUE(client.Connect(port));
  ASSERT_TRUE(client.Write(chirp::network::BuildWebSocketHandshake("localhost", port, "/ws")));
  const std::string resp = client.ReadUntil("\r\n\r\n");
  ASSERT_TRUE(chirp::network::IsWebSocketUpgradeSuccessful(resp)) << resp;
  ASSERT_TRUE(client.Write(chirp::network::BuildWebSocketFrame(0x2, FrameBytes("ping"),
                                                               /*mask=*/true)));

  for (int i = 0; i < 300 && !captured.Get(); ++i) {
    SleepMs(10);
  }
  auto session = captured.Get();
  ASSERT_TRUE(session);

  // Cover the TLS specialization of WebSocketSessionT: RemoteEndpoint,
  // RemoteAddress, IsClosed (inline), Close (posts lambda), then
  // SendAndClose for a second Close path once the write drains.
  EXPECT_FALSE(session->IsClosed());
  EXPECT_EQ(session->RemoteAddress(), "127.0.0.1");

  session->Close();
  for (int i = 0; i < 300 && !closed.load(); ++i) {
    SleepMs(10);
  }
  EXPECT_TRUE(closed.load());
  EXPECT_TRUE(session->IsClosed());

  // SendAndClose after the socket is already closed: the posted lambda
  // still runs, sees closed_ and returns without touching the socket.
  session->SendAndClose(FrameBytes("late"));
  SleepMs(50);

  client.Disconnect();
  server.Stop();
  io.stop();
  server_thread.join();
}

TEST_F(TlsEdgeTest, TlsWebSocketSessionSendAndCloseDrainsThenCloses) {
  asio::io_context io;
  LockedSession captured;
  std::atomic<bool> closed{false};

  chirp::network::TlsWebSocketServer server(
      io, /*port=*/0, ssl_,
      [&](std::shared_ptr<Session> s, std::string&&) { captured.Set(s); },
      [&](std::shared_ptr<Session>) { closed.store(true); });
  server.Start();
  const uint16_t port = server.Port();
  std::thread server_thread([&io] { io.run(); });

  SyncTlsClient client;
  ASSERT_TRUE(client.Connect(port));
  ASSERT_TRUE(client.Write(chirp::network::BuildWebSocketHandshake("localhost", port, "/ws")));
  const std::string resp = client.ReadUntil("\r\n\r\n");
  ASSERT_TRUE(chirp::network::IsWebSocketUpgradeSuccessful(resp)) << resp;
  ASSERT_TRUE(client.Write(chirp::network::BuildWebSocketFrame(0x2, FrameBytes("ping"),
                                                               /*mask=*/true)));
  for (int i = 0; i < 300 && !captured.Get(); ++i) {
    SleepMs(10);
  }
  auto session = captured.Get();
  ASSERT_TRUE(session);

  // SendAndClose queues a frame with close_after_write_; DoWrite drains it
  // and fires DoClose → on_close. This is the primary close path for wss.
  session->SendAndClose(FrameBytes("bye"));
  for (int i = 0; i < 300 && !closed.load(); ++i) {
    SleepMs(10);
  }
  EXPECT_TRUE(closed.load());
  EXPECT_TRUE(session->IsClosed());

  client.Disconnect();
  server.Stop();
  io.stop();
  server_thread.join();
}

// ---------------------------------------------------------------------------
// ssl_context failure arms
// ---------------------------------------------------------------------------

TEST_F(TlsEdgeTest, MakeServerSslContextFailures) {
  std::string error;

  auto ctx = chirp::network::MakeServerSslContext(cert_dir_ + "/missing.pem", key_path_, &error);
  EXPECT_EQ(ctx, nullptr);
  EXPECT_NE(error.find("missing.pem"), std::string::npos) << error;

  error.clear();
  ctx = chirp::network::MakeServerSslContext(cert_path_, cert_dir_ + "/missing-key.pem", &error);
  EXPECT_EQ(ctx, nullptr);
  EXPECT_NE(error.find("missing-key.pem"), std::string::npos) << error;
}

TEST_F(TlsEdgeTest, TlsServerStopJoinsCleanly) {
  asio::io_context io;
  chirp::network::TlsTcpServer server(io, /*port=*/0, ssl_, nullptr, nullptr);
  server.Start();
  const uint16_t port = server.Port();
  std::thread server_thread([&io] { io.run(); });

  SyncTlsClient client;
  ASSERT_TRUE(client.Connect(port));  // idle, live session across the stop

  server.Stop();
  EXPECT_TRUE(WaitConnectRefused(port));  // the strand-posted close has run
  io.stop();
  server_thread.join();  // must return promptly with a session attached
  SUCCEED();
}

TEST_F(TlsEdgeTest, TlsTcpSessionCloseViaInterface) {
  asio::io_context io;
  LockedSession captured;
  std::atomic<bool> closed{false};

  chirp::network::TlsTcpServer server(
      io, /*port=*/0, ssl_,
      [&](std::shared_ptr<Session> s, std::string&&) { captured.Set(s); },
      [&](std::shared_ptr<Session>) { closed.store(true); });
  server.Start();
  const uint16_t port = server.Port();
  std::thread server_thread([&io] { io.run(); });

  SyncTlsClient client;
  ASSERT_TRUE(client.Connect(port));
  ASSERT_TRUE(client.Write(FrameBytes("hi")));
  for (int i = 0; i < 300 && !captured.Get(); ++i) {
    SleepMs(10);
  }
  auto session = captured.Get();
  ASSERT_TRUE(session);
  EXPECT_FALSE(session->IsClosed());

  // Close() is the explicit interface path (posts DoClose through the
  // strand) — the TLS specialization of TcpSessionT::Close and its lambda.
  session->Close();
  for (int i = 0; i < 300 && !closed.load(); ++i) {
    SleepMs(10);
  }
  EXPECT_TRUE(closed.load());
  EXPECT_TRUE(session->IsClosed());

  client.Disconnect();
  server.Stop();
  io.stop();
  server_thread.join();
}

// ---------------------------------------------------------------------------
// Session interface
// ---------------------------------------------------------------------------

// Start() defaults to a no-op: only the accept loop calls it, so mock or
// pre-started sessions never need an override.
TEST(SessionInterfaceTest, DefaultStartIsNoOp) {
  struct BareSession : Session {
    void Send(std::string) override { sent = true; }
    void SendAndClose(std::string) override { sent = true; }
    void Close() override { closed = true; }
    bool IsClosed() const override { return closed; }
    std::string RemoteAddress() const override { return {}; }
    bool sent = false;
    bool closed = false;
  };
  std::shared_ptr<Session> s = std::make_shared<BareSession>();
  s->Start();
  // The base-class default must not have written, sent or closed anything.
  auto* bare = static_cast<BareSession*>(s.get());
  EXPECT_FALSE(bare->sent);
  EXPECT_FALSE(bare->closed);
  EXPECT_FALSE(s->IsClosed());
  EXPECT_TRUE(s->RemoteAddress().empty());
  // ... and the pure-virtual entry points still dispatch through the base.
  s->Send("x");
  EXPECT_TRUE(bare->sent);
  s->Close();
  EXPECT_TRUE(s->IsClosed());
}

} // namespace
