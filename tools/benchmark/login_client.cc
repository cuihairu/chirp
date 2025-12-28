#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <string>

#include <asio.hpp>

#include "network/length_prefixed_framer.h"
#include "network/protobuf_framing.h"
#include "proto/auth.pb.h"
#include "proto/gateway.pb.h"

namespace {

int64_t NowMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::string GetArg(int argc, char** argv, const std::string& key, const std::string& def) {
  for (int i = 1; i < argc; i++) {
    if (argv[i] == key && i + 1 < argc) {
      return argv[i + 1];
    }
  }
  return def;
}

bool ReadOnePacket(asio::ip::tcp::socket& sock,
                   chirp::network::LengthPrefixedFramer* framer,
                   chirp::gateway::Packet* out_pkt) {
  std::array<uint8_t, 4096> buf{};
  while (true) {
    auto frame = framer->PopFrame();
    if (frame) {
      return out_pkt->ParseFromArray(frame->data(), static_cast<int>(frame->size()));
    }

    asio::error_code ec;
    const size_t n = sock.read_some(asio::buffer(buf), ec);
    if (ec) {
      return false;
    }
    framer->Append(buf.data(), n);
  }
}

enum class ReadResult { kOk, kTimeout, kClosedOrError };

ReadResult ReadOnePacketWithTimeout(asio::ip::tcp::socket& sock,
                                    chirp::network::LengthPrefixedFramer* framer,
                                    chirp::gateway::Packet* out_pkt,
                                    int timeout_ms) {
  using namespace std::chrono;
  const auto deadline = steady_clock::now() + milliseconds(timeout_ms);
  std::array<uint8_t, 4096> buf{};

  sock.non_blocking(true);
  while (steady_clock::now() < deadline) {
    auto frame = framer->PopFrame();
    if (frame) {
      return out_pkt->ParseFromArray(frame->data(), static_cast<int>(frame->size())) ? ReadResult::kOk
                                                                                    : ReadResult::kClosedOrError;
    }

    asio::error_code ec;
    const size_t n = sock.read_some(asio::buffer(buf), ec);
    if (!ec) {
      framer->Append(buf.data(), n);
      continue;
    }
    if (ec == asio::error::would_block || ec == asio::error::try_again) {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
      continue;
    }
    return ReadResult::kClosedOrError;
  }
  return ReadResult::kTimeout;
}

} // namespace

int main(int argc, char** argv) {
  const std::string host = GetArg(argc, argv, "--host", "127.0.0.1");
  const uint16_t port = static_cast<uint16_t>(std::atoi(GetArg(argc, argv, "--port", "5000").c_str()));
  const std::string token = GetArg(argc, argv, "--token", "user_1");
  const std::string device_id = GetArg(argc, argv, "--device", "dev_1");
  const std::string platform = GetArg(argc, argv, "--platform", "pc");
  const int wait_kick_ms = std::atoi(GetArg(argc, argv, "--wait_kick_ms", "0").c_str());

  asio::io_context io;
  asio::ip::tcp::resolver resolver(io);
  asio::ip::tcp::socket sock(io);

  auto endpoints = resolver.resolve(host, std::to_string(port));
  asio::connect(sock, endpoints);

  chirp::auth::LoginRequest req;
  req.set_token(token);
  req.set_device_id(device_id);
  req.set_platform(platform);

  chirp::gateway::Packet pkt;
  pkt.set_msg_id(chirp::gateway::LOGIN_REQ);
  pkt.set_sequence(1);
  pkt.set_body(req.SerializeAsString());

  auto out = chirp::network::ProtobufFraming::Encode(pkt);
  asio::write(sock, asio::buffer(out));

  chirp::network::LengthPrefixedFramer framer;
  chirp::gateway::Packet resp;
  if (!ReadOnePacket(sock, &framer, &resp)) {
    std::cerr << "failed to read response frame\n";
    return 1;
  }

  std::cout << "resp msg_id=" << resp.msg_id() << " seq=" << resp.sequence() << "\n";
  if (resp.msg_id() == chirp::gateway::LOGIN_RESP) {
    chirp::auth::LoginResponse lr;
    if (lr.ParseFromArray(resp.body().data(), static_cast<int>(resp.body().size()))) {
      std::cout << "code=" << lr.code() << " session_id=" << lr.session_id() << " server_time=" << lr.server_time()
                << "\n";
    }
  } else if (resp.msg_id() == chirp::gateway::KICK_NOTIFY) {
    chirp::auth::KickNotify kn;
    if (kn.ParseFromArray(resp.body().data(), static_cast<int>(resp.body().size()))) {
      std::cout << "kick reason=" << kn.reason() << "\n";
    }
  }

  // Optional: send a ping after login to ensure connection stays healthy.
  chirp::gateway::HeartbeatPing ping;
  ping.set_timestamp(NowMs());

  chirp::gateway::Packet ping_pkt;
  ping_pkt.set_msg_id(chirp::gateway::HEARTBEAT_PING);
  ping_pkt.set_sequence(2);
  ping_pkt.set_body(ping.SerializeAsString());

  auto ping_out = chirp::network::ProtobufFraming::Encode(ping_pkt);
  asio::write(sock, asio::buffer(ping_out));

  chirp::gateway::Packet pong_pkt;
  if (!ReadOnePacket(sock, &framer, &pong_pkt)) {
    std::cerr << "failed to read ping response frame\n";
    return 1;
  }
  std::cout << "pong msg_id=" << pong_pkt.msg_id() << " seq=" << pong_pkt.sequence() << "\n";

  if (wait_kick_ms > 0) {
    chirp::gateway::Packet maybe_kick;
    const ReadResult r = ReadOnePacketWithTimeout(sock, &framer, &maybe_kick, wait_kick_ms);
    if (r == ReadResult::kOk && maybe_kick.msg_id() == chirp::gateway::KICK_NOTIFY) {
      chirp::auth::KickNotify kn;
      if (kn.ParseFromArray(maybe_kick.body().data(), static_cast<int>(maybe_kick.body().size()))) {
        std::cout << "kick reason=" << kn.reason() << "\n";
      } else {
        std::cout << "kick\n";
      }
      return 0;
    }
    if (r == ReadResult::kTimeout) {
      std::cerr << "no kick within " << wait_kick_ms << "ms\n";
      return 2;
    }
    std::cerr << "connection closed before kick\n";
    return 3;
  }

  return 0;
}
