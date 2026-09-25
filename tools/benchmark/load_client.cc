// chirp_load_client:并发发送/RTT 压测工具。
//
// N 条连接登录为 N 个互不相同的用户,按环形配对(i -> i+1)互发私聊,
// 统计 SEND_MESSAGE_REQ -> RESP 的往返延迟分位数与吞吐。所有发送方与
// 接收方都在线,测的是纯服务端处理 RTT,不掺离线队列路径。
//
// 用法示例(对着本机 basic chat):
//   ./chirp_load_client --host 127.0.0.1 --port 7000 --conns 32 --rounds 20
//
// 注意服务端的发送防线(见 docs/guide/integration-pitfalls.md):
//   - 私聊节奏 1s/用户:默认 --interval-ms 1100 避开;调低会拿到
//     RATE_LIMITED(计入分码统计,不算失败);
//   - 模糊闸默认 120 条/分/用户:压测吞吐受它约束,要压上限先调
//     --send_rate_limit_per_min;
//   - 私聊长度上限 200 码点:--size 默认 32。

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <asio.hpp>

#include "network/byte_order.h"
#include "network/protobuf_framing.h"
#include "proto/auth.pb.h"
#include "proto/chat.pb.h"
#include "proto/common.pb.h"
#include "proto/gateway.pb.h"

namespace {

int64_t NowMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(steady_clock::now().time_since_epoch())
      .count();
}

std::string GetArg(int argc, char** argv, const std::string& key,
                   const std::string& def) {
  for (int i = 1; i < argc; i++) {
    if (std::string(argv[i]) == key && i + 1 < argc) {
      return argv[i + 1];
    }
  }
  return def;
}

bool ReadFrame(asio::ip::tcp::socket& sock, std::string* payload) {
  uint8_t len_be[4];
  asio::error_code ec;
  asio::read(sock, asio::buffer(len_be, 4), ec);
  if (ec) {
    return false;
  }
  const uint32_t len = chirp::network::ReadU32BE(len_be);
  payload->resize(len);
  asio::read(sock, asio::buffer(payload->data(), payload->size()), ec);
  return !ec;
}

// 发一帧、等对应 sequence 的响应;中途到达的 notify(聊天推送等)跳过。
bool SendAndRead(asio::ip::tcp::socket& sock,
                 chirp::gateway::MsgID msg_id, int64_t seq,
                 const std::string& body,
                 chirp::gateway::Packet* out_pkt) {
  chirp::gateway::Packet pkt;
  pkt.set_msg_id(msg_id);
  pkt.set_sequence(seq);
  pkt.set_body(body);
  const auto out = chirp::network::ProtobufFraming::Encode(pkt);
  asio::write(sock, asio::buffer(out));
  for (;;) {
    std::string payload;
    if (!ReadFrame(sock, &payload)) {
      return false;
    }
    if (!out_pkt->ParseFromArray(payload.data(),
                                 static_cast<int>(payload.size()))) {
      return false;
    }
    if (out_pkt->sequence() == seq) {
      return true;
    }
  }
}

struct WorkerResult {
  std::vector<int64_t> rtts_ms;      // 被 OK 受理的发送 RTT
  std::vector<int> reject_codes;     // 业务码非 OK 的响应
  int transport_failures = 0;        // 读/写/解析失败(连接级)
  std::string detail;
};

// 单连接 worker:登录为 bench 用户 idx,向环形下家发 rounds 条私聊。
WorkerResult RunWorker(const std::string& host, uint16_t port, int idx,
                       int conns, int rounds, int interval_ms, int size,
                       const std::string& prefix) {
  WorkerResult res;
  asio::io_context io;
  asio::ip::tcp::resolver resolver(io);
  asio::ip::tcp::socket sock(io);
  asio::error_code ec;
  asio::connect(sock, resolver.resolve(host, std::to_string(port)), ec);
  if (ec) {
    res.transport_failures = 1;
    res.detail = "connect: " + ec.message();
    return res;
  }

  const std::string self = prefix + std::to_string(idx);
  const std::string peer = prefix + std::to_string((idx + 1) % conns);

  chirp::auth::LoginRequest login;
  login.set_token(self);
  login.set_device_id("load");
  login.set_platform("pc");
  chirp::gateway::Packet resp_pkt;
  if (!SendAndRead(sock, chirp::gateway::LOGIN_REQ, 1,
                   login.SerializeAsString(), &resp_pkt) ||
      resp_pkt.msg_id() != chirp::gateway::LOGIN_RESP) {
    res.transport_failures = 1;
    res.detail = "login transport failure";
    return res;
  }
  chirp::auth::LoginResponse login_resp;
  if (!login_resp.ParseFromArray(resp_pkt.body().data(),
                                 static_cast<int>(resp_pkt.body().size())) ||
      login_resp.code() != chirp::common::OK) {
    res.transport_failures = 1;
    res.detail = "login rejected";
    return res;
  }

  const std::string content(size, 'x');
  for (int r = 0; r < rounds; ++r) {
    if (r > 0 && interval_ms > 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
    }
    // 内容逐轮变化:相同内容连发 3 条会触发服务端重复禁言(5 分钟)，
    // 固定内容的压测从第 3 条起全是 RATE_LIMITED。
    const std::string varied =
        "r" + std::to_string(r) + "/" + content.substr(
            std::min<std::string::size_type>(content.size(),
                                             2 + std::to_string(r).size()));
    chirp::chat::SendMessageRequest req;
    req.set_sender_id(self);
    req.set_receiver_id(peer);
    req.set_channel_type(chirp::chat::PRIVATE);
    req.set_content(varied);

    const int64_t t0 = NowMs();
    if (!SendAndRead(sock, chirp::gateway::SEND_MESSAGE_REQ, 2 + r,
                     req.SerializeAsString(), &resp_pkt) ||
        resp_pkt.msg_id() != chirp::gateway::SEND_MESSAGE_RESP) {
      ++res.transport_failures;
      res.detail = "send transport failure at round " + std::to_string(r);
      break;
    }
    const int64_t rtt = NowMs() - t0;
    chirp::chat::SendMessageResponse sresp;
    if (!sresp.ParseFromArray(resp_pkt.body().data(),
                              static_cast<int>(resp_pkt.body().size()))) {
      ++res.transport_failures;
      res.detail = "unparseable SEND_MESSAGE_RESP";
      break;
    }
    if (sresp.code() == chirp::common::OK) {
      res.rtts_ms.push_back(rtt);
    } else {
      res.reject_codes.push_back(sresp.code());
    }
  }
  return res;
}

int Percentile(std::vector<int64_t> samples, double p) {
  if (samples.empty()) {
    return -1;
  }
  std::sort(samples.begin(), samples.end());
  const size_t idx = std::min(
      samples.size() - 1,
      static_cast<size_t>(p * static_cast<double>(samples.size() - 1)));
  return static_cast<int>(samples[idx]);
}

} // namespace

int main(int argc, char** argv) {
  const std::string host = GetArg(argc, argv, "--host", "127.0.0.1");
  const uint16_t port = static_cast<uint16_t>(
      std::atoi(GetArg(argc, argv, "--port", "7000").c_str()));
  const int conns = std::atoi(GetArg(argc, argv, "--conns", "16").c_str());
  const int rounds = std::atoi(GetArg(argc, argv, "--rounds", "10").c_str());
  const int interval_ms =
      std::atoi(GetArg(argc, argv, "--interval-ms", "1100").c_str());
  const int size = std::atoi(GetArg(argc, argv, "--size", "32").c_str());
  const std::string prefix = GetArg(argc, argv, "--prefix", "load_user_");

  if (conns < 2 || rounds < 1) {
    std::cerr << "--conns >= 2(环形配对需要至少两个用户),--rounds >= 1\n";
    return 2;
  }

  std::cout << "chirp_load_client -> " << host << ":" << port
            << " conns=" << conns << " rounds=" << rounds
            << " interval_ms=" << interval_ms << " size=" << size << "\n";

  std::vector<WorkerResult> results(conns);
  std::vector<std::thread> threads;
  threads.reserve(conns);
  const int64_t t0 = NowMs();
  for (int i = 0; i < conns; ++i) {
    threads.emplace_back([&, i] {
      results[i] =
          RunWorker(host, port, i, conns, rounds, interval_ms, size, prefix);
    });
  }
  for (auto& t : threads) {
    t.join();
  }
  const int64_t wall_ms = NowMs() - t0;

  std::vector<int64_t> all_rtts;
  int total_rejects = 0;
  int total_failures = 0;
  int code_count[16] = {0};
  for (const auto& r : results) {
    all_rtts.insert(all_rtts.end(), r.rtts_ms.begin(), r.rtts_ms.end());
    total_rejects += static_cast<int>(r.reject_codes.size());
    total_failures += r.transport_failures;
    for (int c : r.reject_codes) {
      if (c >= 0 && c < 16) {
        ++code_count[c];
      }
    }
    if (!r.detail.empty()) {
      std::cerr << "  [conn] " << r.detail << "\n";
    }
  }

  const int total_ok = static_cast<int>(all_rtts.size());
  const double throughput =
      wall_ms > 0 ? 1000.0 * total_ok / static_cast<double>(wall_ms) : 0.0;

  std::cout << "结果: ok=" << total_ok << " 拒收=" << total_rejects
            << " 传输失败=" << total_failures << "\n";
  std::cout << "耗时 " << wall_ms << "ms,吞吐 " << throughput << " msg/s\n";
  if (!all_rtts.empty()) {
    std::cout << "RTT(ms): p50=" << Percentile(all_rtts, 0.50)
              << " p90=" << Percentile(all_rtts, 0.90)
              << " p99=" << Percentile(all_rtts, 0.99)
              << " max=" << Percentile(all_rtts, 1.0) << "\n";
  }
  for (int c = 0; c < 16; ++c) {
    if (code_count[c] > 0) {
      std::cout << "  code=" << c << " x" << code_count[c]
                << (c == chirp::common::RATE_LIMITED
                        ? " (RATE_LIMITED:调大 --interval-ms 或服务端阈值)"
                        : "")
                << "\n";
    }
  }

  return total_failures == 0 ? 0 : 1;
}
