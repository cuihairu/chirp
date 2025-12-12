#include "common/logger.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>

namespace chirp::common {

Logger& Logger::Instance() {
  static Logger inst;
  return inst;
}

void Logger::SetLevel(Level level) {
  std::lock_guard<std::mutex> lock(mu_);
  level_ = level;
}

Logger::Level Logger::GetLevel() const {
  std::lock_guard<std::mutex> lock(mu_);
  return level_;
}

std::string Logger::LevelToString(Level level) {
  switch (level) {
  case Level::kTrace:
    return "TRACE";
  case Level::kDebug:
    return "DEBUG";
  case Level::kInfo:
    return "INFO";
  case Level::kWarn:
    return "WARN";
  case Level::kError:
    return "ERROR";
  }
  return "UNKNOWN";
}

void Logger::Log(Level level, std::string_view msg) {
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (static_cast<int>(level) < static_cast<int>(level_)) {
      return;
    }
  }

  // Light-weight, dependency-free logger for early scaffolding.
  auto now = std::chrono::system_clock::now();
  auto t = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
#if defined(_WIN32)
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif

  std::ostringstream line;
  line << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
  line << " [" << LevelToString(level) << "]";
  line << " [tid=" << std::this_thread::get_id() << "] ";
  line << msg;
  line << "\n";

  // stderr to keep logs visible even if stdout is redirected.
  std::cerr << line.str();
}

} // namespace chirp::common

