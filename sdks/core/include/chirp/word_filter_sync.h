// 词库下发同步——C++ core 面的 词库下发协议(docs/design-notes/word_filter.md
// 「词库下发协议」,MsgID 2245-2247)。与 TS 面 word_filter_sync.ts 同一契约:
// 包一个可热替换的 WordFilterInterceptor,FETCH_RESP / UPDATE_NOTIFY 载荷
// 原地重建它,发送侧预检跟随服务端词库走,而不是手工部署的词库文件。
//
//   * 版本门:version <= 已应用版本的词库是重复投递或乱序到达,忽略;
//   * enabled=false(服务端不过滤)或策略 RECORD(只做服务端审计)时清空
//     本地词库、放行透传——客户端面永不比服务端更严;
//   * Fetch 失败静默返回 false(超时/断线/非 OK 都算),按提案失败同步不得
//     冒泡到 UI,兜底词库继续生效,下次连接再拉;
//   * LoadLocal 保留旧 WordFilterLoader 的本地兜底面,version 不动(0),
//     第一份服务端词库(version >= 1)到达即取代它。
//
// 线程模型:Request/OnNotify 回调全部在 SDK io 线程触发,LoadLocal 可能来自
// 调用线程,inner/version 用互斥量保护。Fetch 阻塞等待回调(复用 Request 的
// 单请求超时),不要在 SDK 回调线程里调 Fetch。
#pragma once

#include <cstdint>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "chirp/message_interceptor.h"
#include "chirp/sdk_client.h"
#include "chirp/word_filter.h"
#include "proto/chat.pb.h"
#include "proto/gateway.pb.h"

namespace chirp {
namespace sdk {

class WordFilterSync : public MessageInterceptor {
 public:
  explicit WordFilterSync(ChatClient& client) : client_(client) {}

  ~WordFilterSync() override { Stop(); }

  WordFilterSync(const WordFilterSync&) = delete;
  WordFilterSync& operator=(const WordFilterSync&) = delete;

  // 当前生效词库版本;0 = 尚未收到服务端词库。
  int64_t version() const {
    std::lock_guard<std::mutex> lock(mu_);
    return version_;
  }

  // 预检当前装载的词数(服务端词库或本地兜底)。
  size_t word_count() const {
    std::lock_guard<std::mutex> lock(mu_);
    return inner_ ? inner_->word_count() : 0;
  }

  // 订阅 UPDATE_NOTIFY。登录后调用一次——notify 与 fetch 都骑在已认证
  // 的会话上。重复 Start 幂等。
  void Start() {
    std::lock_guard<std::mutex> lock(mu_);
    if (notify_handle_ != 0) {
      return;
    }
    notify_handle_ = client_.OnNotify(
        chirp::gateway::WORD_FILTER_UPDATE_NOTIFY, [this](const std::string& body) {
          chirp::chat::WordFilterUpdateNotify notify;
          if (!notify.ParseFromString(body)) {
            return;  // 畸形 notify:忽略,下次 fetch 重同步。
          }
          Apply(notify.lexicon());
        });
  }

  void Stop() {
    // OffNotify 不持锁:句柄置空后交出去,避免与在途回调互等。
    NotifyHandle handle = 0;
    {
      std::lock_guard<std::mutex> lock(mu_);
      std::swap(handle, notify_handle_);
    }
    if (handle != 0) {
      client_.OffNotify(handle);
    }
  }

  // 条件 GET:带本地已知版本拉全量词库(known_version == 服务端版本时
  // 回帧词库文本为空,走版本门自然忽略)。永不抛错——ec / 非 OK 一律
  // false;阻塞直至 Request 回调(单请求超时由 ChatConfig 兜底)。
  bool Fetch() {
    chirp::chat::WordFilterFetchRequest req;
    req.set_known_version(version());
    std::string body;
    if (!req.SerializeToString(&body)) {
      return false;
    }
    // shared_ptr 持 promise:若调用方超时放弃,迟到的回调写共享状态,不悬垂。
    auto done = std::make_shared<std::promise<bool>>();
    auto result = done->get_future();
    client_.Request(
        chirp::gateway::WORD_FILTER_FETCH_REQ, chirp::gateway::WORD_FILTER_FETCH_RESP, body,
        [this, done](const std::error_code& ec, const std::string& resp_body) {
          bool applied = false;
          chirp::chat::WordFilterFetchResponse resp;
          if (!ec && resp.ParseFromString(resp_body) &&
              resp.code() == chirp::common::ErrorCode::OK) {
            Apply(resp.lexicon());
            applied = true;
          }
          done->set_value(applied);
        });
    return result.get();
  }

  // 本地兜底词库:按服务端文件格式给词行(旧 WordFilterLoader 面)。
  // version 不动,首个服务端词库到达即取代。
  void LoadLocal(const std::vector<std::string>& lines,
                 WordFilterPolicy policy = WordFilterPolicy::kReplace,
                 const std::string& replacement = "**") {
    WordFilterOptions options;
    options.terms = lines;
    options.policy = policy;
    options.replacement = replacement;
    auto inner = std::make_shared<WordFilterInterceptor>(std::move(options));
    std::lock_guard<std::mutex> lock(mu_);
    inner_ = std::move(inner);
  }

  // 无词库生效(服务端未启用/仅审计)时透传。
  bool OnBeforeSend(chirp::chat::SendMessageRequest& msg) override {
    std::shared_ptr<WordFilterInterceptor> inner;
    {
      std::lock_guard<std::mutex> lock(mu_);
      inner = inner_;
    }
    return inner ? inner->OnBeforeSend(msg) : true;
  }

 private:
  // 版本门重建:<= 已应用版本的词库是重复/乱序投递,忽略。
  // Apply 只在 SDK io 线程跑(fetch 回调与 notify 回调),mu_ 防的是与
  // 调用线程 LoadLocal/读取方的竞争。
  void Apply(const chirp::chat::WordFilterLexicon& lexicon) {
    if (lexicon.version() <= version()) {
      return;
    }
    std::shared_ptr<WordFilterInterceptor> inner;
    if (lexicon.enabled() &&
        lexicon.policy() != chirp::chat::WORD_FILTER_POLICY_RECORD) {
      WordFilterOptions options;
      options.terms = SplitLines(lexicon.lexicon());
      options.policy = lexicon.policy() == chirp::chat::WORD_FILTER_POLICY_REJECT
                           ? WordFilterPolicy::kReject
                           : WordFilterPolicy::kReplace;
      options.replacement =
          lexicon.replacement().empty() ? std::string("**") : lexicon.replacement();
      inner = std::make_shared<WordFilterInterceptor>(std::move(options));
    }
    std::lock_guard<std::mutex> lock(mu_);
    version_ = lexicon.version();
    inner_ = std::move(inner);  // enabled=false / RECORD → 置空透传
  }

  static std::vector<std::string> SplitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::string::size_type start = 0;
    while (true) {
      const std::string::size_type pos = text.find('\n', start);
      if (pos == std::string::npos) {
        if (start < text.size()) {
          lines.emplace_back(text.substr(start));
        }
        break;
      }
      lines.emplace_back(text.substr(start, pos - start));
      start = pos + 1;
    }
    return lines;
  }

  ChatClient& client_;
  mutable std::mutex mu_;
  int64_t version_ = 0;
  std::shared_ptr<WordFilterInterceptor> inner_;
  NotifyHandle notify_handle_ = 0;
};

}  // namespace sdk
}  // namespace chirp
