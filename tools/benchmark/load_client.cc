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
#include <condition_variable>
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
// trace_frames > 0 时把读到的每一帧(类型/sequence)打到 stderr,用来验证
// 「跳过不匹配帧」是否会误吞真正的响应。
bool SendAndRead(asio::ip::tcp::socket& sock,
                 chirp::gateway::MsgID msg_id, int64_t seq,
                 const std::string& body,
                 chirp::gateway::Packet* out_pkt, bool trace_frames) {
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
    if (trace_frames) {
      std::cerr << "[frame] want seq=" << seq << " got msg_id="
                << static_cast<int>(out_pkt->msg_id()) << " seq="
                << out_pkt->sequence() << " @" << NowMs() << "\n";
    }
  }
}

// 建连栅栏:所有 worker 登录后先在这里会合,再同时进入测量阶段。
// 「N 条并发连接」要求 N 条连接在同一时刻都在线——若各连接连上就立刻发完
// 走人,任意时刻的在线数远小于 N,量到的不是并发容量。
// 退出条件三选一:全部到达 / 静默 quiet_ms(有人失败时不必干等) / 总超时。
//
// 为什么不是「等 2ms 再问一遍」的轮询栅栏:万级连接时那是 conns/2ms 次唤醒
// (12000 连接 ≈ 600 万次/秒),压测客户端自己把核抢光——实测 loadavg 打到 1181,
// 而被测的 chat/gateway 只能分到零星核,量出来的 RTT 是客户端排队不是服务能力。
// 这里改成条件变量:等住的 worker 各自只醒一次(全员到齐或 watchdog 判超时),
// 静默/总超时两个条件由单个 watchdog 线程按「最后一次到达时刻」重算触发时刻。
class Barrier {
 public:
  // 初始化顺序须与成员声明顺序一致(-Wreorder)。
  // 静默时钟必须以「构造时刻」起算:留 0 的话 fire=min(0+quiet, deadline)
  // 相对 steady_clock(自开机毫秒)是过去的时刻,watchdog 第一拍就判静默超时
  // 放行——全员一个都没等到(实测同刻在线峰值恒为 0,容量档退化成「60s 登录
  // 流水」而非「同刻在线」,握手超时/吞吐数字全部失真)。
  Barrier(int expected, int64_t quiet_ms, int64_t deadline_ms)
      : last_arrival_ms_(NowMs()), expected_(expected), quiet_ms_(quiet_ms),
        deadline_ms_(deadline_ms) {
    watchdog_ = std::thread([this] { Watchdog(); });
  }

  ~Barrier() {
    {
      std::lock_guard<std::mutex> lk(mu_);
      stop_ = true;
    }
    arrive_cv_.notify_all();
    if (watchdog_.joinable()) watchdog_.join();
  }

  void Arrive() {
    std::unique_lock<std::mutex> lk(mu_);
    if (state_ != kOpen) return;
    last_arrival_ms_ = NowMs();
    if (++arrived_ >= expected_) {
      peak_arrived_ = arrived_;
      state_ = kSatisfied;  // 最后一名:叫醒所有等住的连接一起进场
      release_cv_.notify_all();
      return;
    }
    if (arrived_ > peak_arrived_) peak_arrived_ = arrived_;
    arrive_cv_.notify_one();  // 只叫 watchdog 重算静默时刻,不叫等住的连接
  }

  // 任一时刻「已到达且持连接在等」的峰值。放行(无论满足/超时)后早到者会
  // 陆续退出,峰值就是可证明的同刻在线连接数——比「最终 established」口径严格。
  int peak_arrived() {
    std::lock_guard<std::mutex> lk(mu_);
    return peak_arrived_;
  }

  bool Wait() {
    std::unique_lock<std::mutex> lk(mu_);
    release_cv_.wait_until(
        lk, std::chrono::steady_clock::now() + std::chrono::milliseconds(deadline_ms_),
        [this] { return state_ != kOpen; });
    return state_ == kSatisfied;
  }

  int arrived() {
    std::lock_guard<std::mutex> lk(mu_);
    return arrived_;
  }

 private:
  enum State { kOpen, kSatisfied, kBail };

  // 单线程看门:等到「最后一次到达 + quiet_ms」或总截止时刻,到点仍未齐就放行。
  void Watchdog() {
    std::unique_lock<std::mutex> lk(mu_);
    while (!stop_ && state_ == kOpen) {
      const int64_t now = NowMs();
      const int64_t fire = std::min(last_arrival_ms_ + quiet_ms_, deadline_ms_);
      if (now >= fire) break;
      // 新到达会 notify 本 cv,醒来后重算 fire(静默窗口被顺延)
      arrive_cv_.wait_until(
          lk, std::chrono::steady_clock::now() + std::chrono::milliseconds(fire - now),
          [this] { return stop_ || state_ != kOpen; });
    }
    if (!stop_ && state_ == kOpen) {
      state_ = kBail;  // 静默或总超时:到点进场,让已建立的连接照常参与测量
      release_cv_.notify_all();
    }
  }

  std::mutex mu_;
  std::condition_variable arrive_cv_;    // 只对 watchdog 广播
  std::condition_variable release_cv_;  // 只对等住的连接广播
  std::thread watchdog_;
  int arrived_ = 0;
  int peak_arrived_ = 0;
  int64_t last_arrival_ms_ = 0;
  int expected_;
  int64_t quiet_ms_;
  int64_t deadline_ms_;
  State state_ = kOpen;
  bool stop_ = false;
};

struct WorkerResult {
  std::vector<int64_t> rtts_ms;      // 被 OK 受理的发送 RTT
  std::vector<int> reject_codes;     // 业务码非 OK 的响应
  int transport_failures = 0;        // 读/写/解析失败(连接级)
  std::string detail;
  int64_t login_ms = -1;             // 建连+登录往返(不进 RTT 分位)
  int64_t send_begin_ms = -1;        // 首条发送发出的时刻(发送窗口起)
  int64_t send_end_ms = -1;          // 最后一条响应回来的时刻(发送窗口止)
  bool barrier_released = true;      // 栅栏是否等到全员(超时/静默为 false)
  int worst_round = -1;              // 最慢 RTT 落在第几轮
  int64_t worst_rtt_ms = -1;
};

// 单连接 worker:登录为 bench 用户 idx,向环形下家发 rounds 条私聊。
// ramp_ms > 0 时按序号错峰启动(总铺展窗口),把连接建立速率压到
// ramp_ms/conns 每条——避免同瞬连接洪峰把网关到 chat 的握手通道打穿。
// barrier 关闭时不做会合:栅栏会把 ramp 铺开的相位重新对齐成一个
// 惊群,量到的是「N 条同时首发」的排队,不是稳态发送延迟;测吞吐/稳态
// 延迟要关掉它(并发连接数由「在线连接」一项单独作证)。
// peer_mode = "fixed" 时全部发往同一个不发送的汇点用户,把发送侧容量与
// 接收侧投递解耦(环模式下收发同一对会互相牵连,延迟数字不可归因)。
WorkerResult RunWorker(const std::string& host, uint16_t port, int idx,
                       int conns, int rounds, int interval_ms, int size,
                       const std::string& prefix, int ramp_ms,
                       bool peer_fixed, int trace_stride, bool use_barrier,
                       Barrier* barrier) {
  WorkerResult res;
  if (ramp_ms > 0 && conns > 1) {
    const int64_t delay_ms =
        static_cast<int64_t>(idx) * ramp_ms / conns;
    std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
  }
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
  const std::string peer =
      peer_fixed ? (prefix + "sink")
                 : (prefix + std::to_string((idx + 1) % conns));

  chirp::auth::LoginRequest login;
  login.set_token(self);
  login.set_device_id("load");
  login.set_platform("pc");
  chirp::gateway::Packet resp_pkt;
  const int64_t t_login = NowMs();
  if (!SendAndRead(sock, chirp::gateway::LOGIN_REQ, 1,
                   login.SerializeAsString(), &resp_pkt,
                   trace_stride > 0 && (idx % trace_stride) == 0) ||
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
  res.login_ms = NowMs() - t_login;

  // 会合:等所有连接登录完成,再同时进入测量阶段。
  if (use_barrier && barrier != nullptr) {
    barrier->Arrive();
    res.barrier_released = barrier->Wait();
  }
  if (rounds == 0) {
    return res;  // 纯连接容量档:到栅栏即止,不进入发消息阶段
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
    if (r == 0) {
      res.send_begin_ms = t0;
    }
    const bool traced = trace_stride > 0 && (idx % trace_stride) == 0;
    if (traced) {
      std::cerr << "[trace] conn#" << idx << " r" << r << " send@" << t0 << "\n";
    }
    if (!SendAndRead(sock, chirp::gateway::SEND_MESSAGE_REQ, 2 + r,
                     req.SerializeAsString(), &resp_pkt, traced) ||
        resp_pkt.msg_id() != chirp::gateway::SEND_MESSAGE_RESP) {
      ++res.transport_failures;
      res.detail = "send transport failure at round " + std::to_string(r);
      break;
    }
    const int64_t rtt = NowMs() - t0;
    if (traced) {
      std::cerr << "[trace] conn#" << idx << " r" << r << " resp@" << (t0 + rtt)
                << " rtt=" << rtt << "\n";
    }
    chirp::chat::SendMessageResponse sresp;
    if (!sresp.ParseFromArray(resp_pkt.body().data(),
                              static_cast<int>(resp_pkt.body().size()))) {
      ++res.transport_failures;
      res.detail = "unparseable SEND_MESSAGE_RESP";
      break;
    }
    if (sresp.code() == chirp::common::OK) {
      res.rtts_ms.push_back(rtt);
      if (rtt > res.worst_rtt_ms) {
        res.worst_rtt_ms = rtt;
        res.worst_round = r;
      }
    } else {
      res.reject_codes.push_back(sresp.code());
    }
  }
  if (res.send_begin_ms >= 0) {
    res.send_end_ms = NowMs();
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
  const int ramp_ms = std::atoi(GetArg(argc, argv, "--ramp-ms", "0").c_str());
  const bool peer_fixed =
      GetArg(argc, argv, "--peer-mode", "ring") == "fixed";
  const int trace_stride =
      std::atoi(GetArg(argc, argv, "--trace-stride", "0").c_str());
  // --barrier on(默认):全员登录后会合再同时开测,证明 N 条连接同刻在线。
  // --barrier off:各连接保持 ramp 铺开的相位直接开测,量稳态发送延迟/吞吐
  // (栅栏会把相位重新对齐成惊群,尾部连接的 RTT 量到的是排队不是服务延迟)。
  const std::string barrier_mode = GetArg(argc, argv, "--barrier", "on");
  const bool use_barrier = barrier_mode != "off";
  // 静默宽限:最后一次到达后多久判「能来的都来了」。默认 5s;共享机上负载
  // 抖动可能把建连尾巴拖出 >5s 的空档,提前放行会让「全员到齐」的证明失效
  // (放行后早到者退出、晚到者还在登录,established≠同刻在线)。压大档时调大。
  const int barrier_quiet_ms =
      std::atoi(GetArg(argc, argv, "--barrier-quiet-ms", "5000").c_str());

  if (conns < 2 || rounds < 0) {
    std::cerr << "--conns >= 2(环形配对需要至少两个用户),--rounds >= 0"
                 "(0 = 只建连不测吞吐,纯连接容量档)\n";
    return 2;
  }

  std::cout << "chirp_load_client -> " << host << ":" << port
            << " conns=" << conns << " rounds=" << rounds
            << " interval_ms=" << interval_ms << " size=" << size
            << " ramp_ms=" << ramp_ms
            << " peer_mode=" << (peer_fixed ? "fixed" : "ring")
            << " barrier=" << (use_barrier ? "on" : "off")
            << (use_barrier ? "(quiet=" + std::to_string(barrier_quiet_ms) + "ms)" : "")
            << "\n";

  std::vector<WorkerResult> results(conns);
  std::vector<std::thread> threads;
  threads.reserve(conns);
  const int64_t t0 = NowMs();
  // 栅栏期限:铺开窗口 + 兜底宽限(至少盖住静默窗,不与它互相打架)。
  const int64_t barrier_margin_ms =
      std::max<int64_t>(60000, static_cast<int64_t>(barrier_quiet_ms) + 10000);
  Barrier barrier(conns, barrier_quiet_ms, t0 + ramp_ms + barrier_margin_ms);
  for (int i = 0; i < conns; ++i) {
    threads.emplace_back([&, i] {
      results[i] = RunWorker(host, port, i, conns, rounds, interval_ms, size,
                             prefix, ramp_ms, peer_fixed, trace_stride,
                             use_barrier, &barrier);
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

  // 发送窗口吞吐:只算「全员过栅栏后第一条发出」到「最后一条响应回来」。
  // 全程吞吐把 ramp 建连 + 登录串行握手也算进分母,规模越大摊薄越狠,
  // 不是稳态发送能力,两个数一起看才有意义。
  int64_t send_begin = -1;
  int64_t send_end = -1;
  for (const auto& r : results) {
    if (r.send_begin_ms >= 0 && (send_begin < 0 || r.send_begin_ms < send_begin)) {
      send_begin = r.send_begin_ms;
    }
    if (r.send_end_ms > send_end) {
      send_end = r.send_end_ms;
    }
  }
  const int64_t send_wall_ms =
      (send_begin >= 0 && send_end > send_begin) ? send_end - send_begin : 0;
  const double send_throughput =
      send_wall_ms > 0 ? 1000.0 * total_ok / static_cast<double>(send_wall_ms)
                       : 0.0;

  std::cout << "结果: ok=" << total_ok << " 拒收=" << total_rejects
            << " 传输失败=" << total_failures << "\n";
  std::cout << "耗时 " << wall_ms << "ms,吞吐 " << throughput << " msg/s\n";
  if (send_wall_ms > 0) {
    std::cout << "发送窗口 " << send_wall_ms << "ms,窗口吞吐 "
              << send_throughput << " msg/s\n";
  }
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

  // 建连/登录是网关→chat 握手串行化的窗口,单独出分位,便于把
  // 「建连能力」与「稳态发送延迟」拆开看。
  std::vector<int64_t> logins;
  for (const auto& r : results) {
    if (r.login_ms >= 0) {
      logins.push_back(r.login_ms);
    }
  }
  if (!logins.empty()) {
    std::cout << "登录RTT(ms): p50=" << Percentile(logins, 0.50)
              << " p90=" << Percentile(logins, 0.90)
              << " p99=" << Percentile(logins, 0.99)
              << " max=" << Percentile(logins, 1.0)
              << " (成功 " << logins.size() << "/" << conns << ")\n";
  }

  // 并发容量口径:真正同时在线的连接数(过栅栏的),以及建连速率。
  int established = 0;
  int barrier_timeout = 0;
  for (const auto& r : results) {
    if (r.login_ms >= 0) {
      ++established;
    }
    if (!r.barrier_released) {
      ++barrier_timeout;
    }
  }
  std::cout << "并发在线连接: " << established << "/" << conns
            << " (栅栏超时/静默退出 " << barrier_timeout << ")\n";
  if (use_barrier) {
    std::cout << "同刻在线峰值(栅栏在册): " << barrier.peak_arrived() << "/"
              << conns << "\n";
  }
  if (established > 1 && ramp_ms > 0) {
    std::cout << "建连速率: " << (1000.0 * established / ramp_ms)
              << " conn/s (铺开窗口 " << ramp_ms << "ms)\n";
  }

  // 逐轮分位:区分「延迟集中在某一轮(位置效应)」与「整体偏移(服务端压力)」。
  std::vector<std::vector<int64_t>> per_round(rounds);
  for (const auto& r : results) {
    for (size_t i = 0; i < r.rtts_ms.size() && i < per_round.size(); ++i) {
      per_round[i].push_back(r.rtts_ms[i]);
    }
  }
  for (int r = 0; r < rounds; ++r) {
    if (per_round[r].empty()) {
      continue;
    }
    std::cout << "  round r" << r << " RTT(ms): n=" << per_round[r].size()
              << " p50=" << Percentile(per_round[r], 0.50)
              << " p99=" << Percentile(per_round[r], 0.99)
              << " max=" << Percentile(per_round[r], 1.0) << "\n";
  }

  // 最慢 5 条连接:慢在建连还是慢在发送,round 号直接给出判据。
  std::vector<std::pair<int64_t, int>> by_worst;
  for (int i = 0; i < conns; ++i) {
    if (results[i].worst_rtt_ms >= 0) {
      by_worst.emplace_back(results[i].worst_rtt_ms, i);
    }
  }
  std::sort(by_worst.rbegin(), by_worst.rend());
  std::cout << "最慢连接(按最大发送 RTT):\n";
  for (size_t i = 0; i < by_worst.size() && i < 5; ++i) {
    const auto& r = results[by_worst[i].second];
    std::cout << "  conn#" << by_worst[i].second
              << " 登录=" << r.login_ms << "ms 最慢轮次=r" << r.worst_round
              << " RTT=" << by_worst[i].first << "ms\n";
  }

  return total_failures == 0 ? 0 : 1;
}
