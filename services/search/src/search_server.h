#ifndef CHIRP_SERVICES_SEARCH_SEARCH_SERVER_H_
#define CHIRP_SERVICES_SEARCH_SEARCH_SERVER_H_

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "message_search_index.h"

namespace chirp {
namespace common {
class LoginTokenVerifier;
}  // namespace common

namespace network {
class Session;
}  // namespace network

namespace gateway {
class Packet;
}  // namespace gateway

namespace search {

class MessageIndexSync;

// chirp_search 的连接面协议处理（message_search 批）：SERVER_AUTH_REQ 信任
// 门 + LOGIN 连接身份 + SEARCH_MESSAGE_REQ 检索。与 chat 的直连入口同一套
// 信任链——gateway 的 per-client pipe 先过服务门，再原样重放客户端登录；
// 连接身份只认登录结果，不信任请求体里的任何身份字段。
//
// 与 chat 的两处有意差异：
// 1. 不做登录限流——5007 不是面向公网的直连入口（生产里 secret + token 双
//    配置，直连只用于本地调试），网关扇出的登录洪泛由 chat 的限流挡在前面；
// 2. 不做会话槽位（同用户多端并存）——search 管道是每客户端一条的次级连
//    接（SessionRegistry 的 BindAuthenticatedSession 会顶掉同端旧槽，语义
//    不对），用本地映射记身份即可，断开即清。
class SearchServer {
 public:
  struct Options {
    // 共享服务密钥；空 = SERVER_AUTH_REQ 被忽略（仅本地调试直连）。
    std::string service_secret;
    // JWT 校验密钥；空 = 脚手架登录（token 即 user_id，本地/烟测用）。
    std::string token_secret;
  };

  // sync 可为空（无 MySQL 形态）：此时检索一律 INTERNAL_ERROR 失败关闭，
  // 其余协议行为不变。
  SearchServer(MessageSearchIndex& index, MessageIndexSync* sync, Options options);
  ~SearchServer();

  SearchServer(const SearchServer&) = delete;
  SearchServer& operator=(const SearchServer&) = delete;

  // 单帧处理入口（TcpServer 的 on_frame 回调）。
  void HandleFrame(const std::shared_ptr<network::Session>& session,
                   std::string&& payload);

  // 会话断开（TcpServer 的 on_close 回调）：清本地身份与信任记录。
  void HandleClose(const std::shared_ptr<network::Session>& session);

  // 测试与统计观察。
  size_t trusted_count() const;
  size_t authenticated_count() const;

 private:
  void HandleServerAuth(const std::shared_ptr<network::Session>& session,
                        const chirp::gateway::Packet& pkt);
  void HandleLogin(const std::shared_ptr<network::Session>& session,
                   const chirp::gateway::Packet& pkt);
  void HandleSearch(const std::shared_ptr<network::Session>& session,
                    const chirp::gateway::Packet& pkt);

  MessageSearchIndex& index_;
  MessageIndexSync* sync_;
  Options options_;
  std::unique_ptr<common::LoginTokenVerifier> verifier_;

  // 本地（非 registry）连接态：信任门通过的服务连接 + 已登录的检索连接。
  std::unordered_set<const network::Session*> trusted_;
  std::unordered_map<const network::Session*, std::string> user_of_;
};

}  // namespace search
}  // namespace chirp

#endif  // CHIRP_SERVICES_SEARCH_SEARCH_SERVER_H_
