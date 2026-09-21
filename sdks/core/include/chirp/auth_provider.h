#pragma once

#include <functional>
#include <string>

namespace chirp {
namespace sdk {

// 认证提供者：游戏实现此接口，将游戏自己的登录系统与 SDK 对接。
// SDK 在需要认证时调用 GetToken()，token 过期时调用 OnTokenExpired()。
class AuthProvider {
 public:
  virtual ~AuthProvider() = default;

  // 提供认证 token（游戏登录系统签发的 JWT 或自定义 token）。
  // SDK 在 Connect 后首次 Login 时调用。
  virtual std::string GetToken() = 0;

  // token 过期回调。SDK 检测到服务器返回 AUTH_FAILED 时触发。
  // renew 回调：游戏刷新 token 后调用 renew(new_token)，SDK 自动重试登录。
  // 如果游戏无法刷新（如登录态已失效），不调用 renew，SDK 进入 Disconnected 状态。
  virtual void OnTokenExpired(
      std::function<void(const std::string&)> renew) = 0;

  // 认证结果回调。
  virtual void OnAuthResult(int code, const std::string& user_id) {}
};

}  // namespace sdk
}  // namespace chirp
