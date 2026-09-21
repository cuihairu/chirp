#pragma once

#include <string>

namespace chirp {
namespace sdk {

// 命令处理器：游戏注册聊天命令（如 /trade、/invite、/help）。
// 玩家在聊天框输入 "/xxx args" 时，SDK 遍历已注册的 CommandHandler，
// 找到匹配的命令并执行。
class CommandHandler {
 public:
  virtual ~CommandHandler() = default;

  // 命令名（不含斜杠）。如 "trade"、"invite"、"help"。
  virtual std::string GetName() const = 0;

  // 命令描述，用于 /help 展示。
  virtual std::string GetDescription() const = 0;

  // 命令用法示例。如 "/trade <玩家名>"。
  virtual std::string GetUsage() const { return "/" + GetName(); }

  // 执行命令。args 是命令名后面的原始字符串（空格保留）。
  // sender_id 是执行命令的玩家 ID。
  // 返回 true 表示已处理，false 表示未识别（传给下一个 handler）。
  virtual bool Execute(const std::string& args,
                       const std::string& sender_id) = 0;
};

}  // namespace sdk
}  // namespace chirp
