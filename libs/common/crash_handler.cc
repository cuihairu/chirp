#include "common/crash_handler.h"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <system_error>
#include <unistd.h>
#include <vector>

#include "common/logger.h"

#if defined(CHIRP_HAVE_CRASHPAD)
// crashpad 头用了 #include_next 等 GCC 扩展;压 pedantic 只作用于本 TU 的
// 第三方 include,本文件自身代码仍受 -Wall -Wextra -Wpedantic 约束。
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#include "client/crashpad_client.h"
#pragma GCC diagnostic pop
#endif

namespace chirp::common::crash {

namespace {

namespace fs = std::filesystem;

constexpr std::string_view kDefaultCrashDir = "crash_dumps";
constexpr std::string_view kCrashDirFlag = "--crash_dir=";
constexpr std::string_view kUploadUrlFlag = "--crash_upload_url=";
constexpr const char* kEnvCrashDir = "CHIRP_CRASH_DIR";
constexpr const char* kEnvUploadUrl = "CHIRP_CRASH_UPLOAD_URL";
constexpr const char* kEnvHandler = "CHIRP_CRASH_HANDLER";
constexpr std::string_view kHandlerName = "crashpad_handler";
// build/services/<组>/<服务>/ → build/ 的层级深度。
constexpr int kHandlerWalkUpLevels = 4;
// annotations["cmd"] 的截断上限。
constexpr std::size_t kCmdAnnotationLimit = 256;

// 逐参 peek 前缀 flag(--crash_dir=DIR 形态),不消费。值空或未命中返回空串。
std::string PeekFlag(int argc, char** argv, std::string_view prefix) {
  for (int i = 1; i < argc; ++i) {
    if (argv[i] == nullptr) {
      continue;
    }
    const std::string_view arg(argv[i]);
    if (arg.size() > prefix.size() && arg.substr(0, prefix.size()) == prefix) {
      std::string value(arg.substr(prefix.size()));
      if (!value.empty()) {
        return value;
      }
    }
  }
  return std::string();
}

std::string GetEnv(const char* name) {
  const char* value = std::getenv(name);
  return value == nullptr ? std::string() : std::string(value);
}

}  // namespace

std::string SelfExeDir() {
  char buf[4096];
  const ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
  if (n <= 0) {
    return std::string();
  }
  buf[n] = '\0';
  const fs::path exe(buf);
  return exe.has_filename() ? exe.parent_path().string() : std::string();
}

Options ResolveOptions(int argc, char** argv, const std::string& exe_dir) {
  Options opts;

  opts.crash_dir = PeekFlag(argc, argv, kCrashDirFlag);
  if (opts.crash_dir.empty()) {
    opts.crash_dir = GetEnv(kEnvCrashDir);
  }
  if (opts.crash_dir.empty()) {
    opts.crash_dir = std::string(kDefaultCrashDir);
  }

  opts.upload_url = PeekFlag(argc, argv, kUploadUrlFlag);
  if (opts.upload_url.empty()) {
    opts.upload_url = GetEnv(kEnvUploadUrl);
  }

  // handler 查找序:运维 env 覆盖 → exe 同目录(安装平铺布局) → 向上至多
  // 4 级(build 树布局)。全部未命中 = 空串,Initialize 降级并打 Warn。
  const std::string env_handler = GetEnv(kEnvHandler);
  if (!env_handler.empty()) {
    std::error_code ec;
    if (fs::is_regular_file(fs::path(env_handler), ec)) {
      opts.handler_path = env_handler;
      return opts;
    }
  }

  if (exe_dir.empty()) {
    return opts;
  }
  fs::path dir = fs::path(exe_dir).lexically_normal();
  for (int level = 0; level <= kHandlerWalkUpLevels; ++level) {
    std::error_code ec;
    if (fs::is_regular_file(dir / kHandlerName, ec)) {
      opts.handler_path = (dir / kHandlerName).string();
      return opts;
    }
    if (!dir.has_parent_path() || dir.parent_path() == dir) {
      break;
    }
    dir = dir.parent_path();
  }
  return opts;
}

std::vector<std::string> PendingReports(const std::string& crash_dir) {
  // crashpad 数据库只把"未处理"的 dump 放在 new/ 与 pending/;completed/
  // 是已成功上传的(本仓上传默认关,不扫)。
  static constexpr const char* kPendingSubdirs[] = {"new", "pending"};
  std::vector<std::string> reports;
  for (const char* sub : kPendingSubdirs) {
    const fs::path base = fs::path(crash_dir) / sub;
    std::error_code ec;
    if (!fs::is_directory(base, ec)) {
      continue;
    }
    // 目录迭代构造失败(ec 置位)时 range 为空,等同无遗留报告。
    for (const auto& entry : fs::directory_iterator(base, ec)) {
      if (!entry.is_regular_file(ec)) {
        continue;
      }
      if (entry.path().extension() == ".dmp") {
        reports.push_back(std::string(sub) + "/" + entry.path().filename().string());
      }
    }
  }
  std::sort(reports.begin(), reports.end());
  return reports;
}

bool Initialize(int argc, char** argv) {
  const Options opts = ResolveOptions(argc, argv, SelfExeDir());

  std::error_code ec;
  fs::create_directories(opts.crash_dir, ec);
  if (ec) {
    Logger::Instance().Warn("crash dir unusable: " + opts.crash_dir + " (" + ec.message() + ")");
    return false;
  }

  // 日志/监控接入点:遗留 dump 由部署侧既有日志采集(落盘/stdout)接走。
  for (const std::string& report : PendingReports(opts.crash_dir)) {
    Logger::Instance().Warn("crash report pending: " + (fs::path(opts.crash_dir) / report).string());
  }

#if !defined(CHIRP_HAVE_CRASHPAD)
  // 未编译 crashpad(覆盖率构建/平台不支持):正常降级,无崩溃采集。
  return false;
#else
  if (opts.handler_path.empty()) {
    Logger::Instance().Warn("crashpad_handler not found; crash collection disabled");
    return false;
  }

  std::map<std::string, std::string> annotations;
  annotations["binary"] = argv[0] == nullptr ? std::string() : std::string(argv[0]);
  annotations["pid"] = std::to_string(::getpid());
  std::string cmd;
  for (int i = 0; i < argc && cmd.size() < kCmdAnnotationLimit; ++i) {
    if (argv[i] == nullptr) {
      continue;
    }
    if (i > 0) {
      cmd += ' ';
    }
    if (cmd.size() + std::char_traits<char>::length(argv[i]) > kCmdAnnotationLimit) {
      break;
    }
    cmd += argv[i];
  }
  annotations["cmd"] = cmd;
  annotations["crash_dir"] = opts.crash_dir;

  // 空 URL = 不外发:handler 不启 uploader 线程,dump 纯本地落盘。
  // database 与 metrics 共用 crash_dir,同库管理。
  const base::FilePath db(opts.crash_dir);
  return crashpad::CrashpadClient().StartHandler(
      base::FilePath(opts.handler_path), db, db, opts.upload_url, annotations,
      /*arguments=*/std::vector<std::string>(),
      /*restartable=*/true,
      /*asynchronous_start=*/false);
#endif
}

}  // namespace chirp::common::crash
