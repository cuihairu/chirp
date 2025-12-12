#pragma once

#include <mutex>
#include <string>
#include <string_view>

namespace chirp::common {

class Logger {
public:
  enum class Level { kTrace = 0, kDebug, kInfo, kWarn, kError };

  static Logger& Instance();

  void SetLevel(Level level);
  Level GetLevel() const;

  void Log(Level level, std::string_view msg);

  void Trace(std::string_view msg) { Log(Level::kTrace, msg); }
  void Debug(std::string_view msg) { Log(Level::kDebug, msg); }
  void Info(std::string_view msg) { Log(Level::kInfo, msg); }
  void Warn(std::string_view msg) { Log(Level::kWarn, msg); }
  void Error(std::string_view msg) { Log(Level::kError, msg); }

private:
  Logger() = default;

  static std::string LevelToString(Level level);

  mutable std::mutex mu_;
  Level level_{Level::kInfo};
};

} // namespace chirp::common

