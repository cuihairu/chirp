// crash_probe:Crashpad 接入验收探针(docs/design-notes/CRASH_COLLECTION.md)。
//
//   crash_probe [--crash_dir=DIR] [--crash_probe_list_pending]
//              [--crash_probe_segfault | --crash_probe_abort]
//
// 验收腿:初始化(建库 + 拉起 handler)→ 打印解析结果 → 开关触发空指针写
// 或 abort → dump 落盘于 crash_dir → minidump-stackwalk 符号化还原调用栈。
// 不带崩溃开关时打印就绪信息正常退出(用于复跑时观察遗留 dump Warn 行)。

#include <cstdio>
#include <cstring>
#include <string>

#include "common/crash_handler.h"

namespace {

bool HasFlag(int argc, char** argv, const char* name) {
  const std::size_t n = std::strlen(name);
  for (int i = 1; i < argc; ++i) {
    if (argv[i] != nullptr && std::strncmp(argv[i], name, n) == 0) {
      return true;
    }
  }
  return false;
}

}  // namespace

int main(int argc, char** argv) {
  const bool want_segfault = HasFlag(argc, argv, "--crash_probe_segfault");
  const bool want_abort = HasFlag(argc, argv, "--crash_probe_abort");
  const bool list_pending = HasFlag(argc, argv, "--crash_probe_list_pending");

  // 与服务 main() 同源:首条语句初始化,返回值只打印不阻塞。
  const bool handler_ready = chirp::common::crash::Initialize(argc, argv);

  const auto opts = chirp::common::crash::ResolveOptions(
      argc, argv, chirp::common::crash::SelfExeDir());
  std::printf("crash_probe: handler_ready=%d\n", handler_ready ? 1 : 0);
  std::printf("crash_probe: crash_dir=%s upload_url=%s handler=%s\n",
              opts.crash_dir.c_str(), opts.upload_url.c_str(),
              opts.handler_path.c_str());

  if (list_pending) {
    for (const std::string& report : chirp::common::crash::PendingReports(opts.crash_dir)) {
      std::printf("crash_probe: pending: %s\n", report.c_str());
    }
  }
  std::fflush(stdout);

  if (want_segfault) {
    std::printf("crash_probe: triggering null-pointer write\n");
    std::fflush(stdout);
    volatile int* null_target = nullptr;
    *null_target = 42;  // SIGSEGV:dump 应落盘于 crash_dir
    return 0;           // 不可达
  }
  if (want_abort) {
    std::printf("crash_probe: triggering abort\n");
    std::fflush(stdout);
    std::abort();
  }

  std::printf("crash_probe: ready (no crash requested)\n");
  return 0;
}
