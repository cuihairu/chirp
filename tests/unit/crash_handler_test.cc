// crash_handler 单测:目录/URL/handler 查找序、遗留 dump 扫描、降级路径。
// 覆盖率构建下 crashpad 关闭(CHIRP_HAVE_CRASHPAD 未定义),Initialize 走
// no-op 桩——该分支可安全断言;crashpad-ON 构建下 Initialize 会真拉起
// handler 进程,对应用例编译为空(真拉起路径的 E2E 归 tools/crash_probe)。

#include "common/crash_handler.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace {

namespace fs = std::filesystem;

// getenv/setenv 无进程级并发,单测顺序执行;每个用例显式清场再摆场。
class ScopedEnv {
 public:
  ScopedEnv(const char* name, const std::string& value) : name_(name) {
    setenv(name_.c_str(), value.c_str(), 1);
  }
  ~ScopedEnv() { unsetenv(name_.c_str()); }

 private:
  std::string name_;
};

class UnsetEnv {
 public:
  explicit UnsetEnv(const char* name) : name_(name) { unsetenv(name_.c_str()); }

 private:
  std::string name_;
};

// argv 构造器:字符串列表 → char*(持有存储,测试期存活)。
struct Argv {
  std::vector<std::string> args;
  std::vector<char*> ptrs;
  char** get() { return ptrs.data(); }
};

Argv MakeArgv(std::initializer_list<std::string> args) {
  Argv argv;
  argv.args = args;
  for (const std::string& a : argv.args) {
    argv.ptrs.push_back(const_cast<char*>(a.c_str()));
  }
  argv.ptrs.push_back(nullptr);
  return argv;
}

fs::path WriteFile(const fs::path& path) {
  fs::create_directories(path.parent_path());
  FILE* f = std::fopen(path.c_str(), "wb");
  if (f != nullptr) {
    std::fputs("x", f);
    std::fclose(f);
  }
  return path;
}

}  // namespace

TEST(CrashHandlerTest, ResolveOptionsDefaults) {
  UnsetEnv d("CHIRP_CRASH_DIR"), u("CHIRP_CRASH_UPLOAD_URL"), h("CHIRP_CRASH_HANDLER");
  Argv argv = MakeArgv({"prog", "--unrelated=1"});
  const auto opts = chirp::common::crash::ResolveOptions(1, argv.get(), "/nonexistent/exe/dir");
  EXPECT_EQ(opts.crash_dir, "crash_dumps");
  EXPECT_EQ(opts.upload_url, "");
  // exe 目录及上溯都没有 handler:空串(禁用采集),不抛错。
  EXPECT_EQ(opts.handler_path, "");
}

TEST(CrashHandlerTest, ResolveCrashDirPrecedence) {
  UnsetEnv u("CHIRP_CRASH_UPLOAD_URL"), h("CHIRP_CRASH_HANDLER");
  // flag > env > 缺省。
  {
    ScopedEnv env("CHIRP_CRASH_DIR", "/from/env");
    Argv argv = MakeArgv({"prog"});
    EXPECT_EQ(chirp::common::crash::ResolveOptions(1, argv.get(), "").crash_dir, "/from/env");
  }
  {
    ScopedEnv env("CHIRP_CRASH_DIR", "/from/env");
    Argv argv = MakeArgv({"prog", "--crash_dir=/from/flag", "other"});
    EXPECT_EQ(chirp::common::crash::ResolveOptions(3, argv.get(), "").crash_dir, "/from/flag");
  }
  {
    UnsetEnv d("CHIRP_CRASH_DIR");
    Argv argv = MakeArgv({"prog"});
    EXPECT_EQ(chirp::common::crash::ResolveOptions(1, argv.get(), "").crash_dir, "crash_dumps");
  }
}

TEST(CrashHandlerTest, ResolveUploadUrlPrecedence) {
  UnsetEnv d("CHIRP_CRASH_DIR"), h("CHIRP_CRASH_HANDLER");
  {
    ScopedEnv env("CHIRP_CRASH_UPLOAD_URL", "https://collector/env");
    Argv argv = MakeArgv({"prog"});
    EXPECT_EQ(chirp::common::crash::ResolveOptions(1, argv.get(), "").upload_url, "https://collector/env");
  }
  {
    ScopedEnv env("CHIRP_CRASH_UPLOAD_URL", "https://collector/env");
    Argv argv = MakeArgv({"prog", "--crash_upload_url=https://collector/flag"});
    EXPECT_EQ(chirp::common::crash::ResolveOptions(2, argv.get(), "").upload_url, "https://collector/flag");
  }
  {
    UnsetEnv u("CHIRP_CRASH_UPLOAD_URL");
    Argv argv = MakeArgv({"prog"});
    EXPECT_EQ(chirp::common::crash::ResolveOptions(1, argv.get(), "").upload_url, "");
  }
}

TEST(CrashHandlerTest, ResolveHandlerEnvOverride) {
  UnsetEnv d("CHIRP_CRASH_DIR"), u("CHIRP_CRASH_UPLOAD_URL");
  Argv argv = MakeArgv({"prog"});
  const fs::path base = fs::path(::testing::TempDir()) / "crash_handler_test";
  fs::remove_all(base);

  // env 指向真实文件:直接采用。
  const fs::path handler = WriteFile(base / "custom_handler");
  {
    ScopedEnv env("CHIRP_CRASH_HANDLER", handler.string());
    EXPECT_EQ(chirp::common::crash::ResolveOptions(1, argv.get(), "").handler_path, handler.string());
  }
  // env 指向不存在文件:落回 exe 目录查找序。
  {
    ScopedEnv env("CHIRP_CRASH_HANDLER", (base / "missing").string());
    EXPECT_EQ(chirp::common::crash::ResolveOptions(1, argv.get(), "/nonexistent/exe/dir").handler_path, "");
  }
}

TEST(CrashHandlerTest, ResolveHandlerWalkUp) {
  UnsetEnv d("CHIRP_CRASH_DIR"), u("CHIRP_CRASH_UPLOAD_URL"), h("CHIRP_CRASH_HANDLER");
  Argv argv = MakeArgv({"prog"});
  const fs::path base = fs::path(::testing::TempDir()) / "crash_handler_walkup";
  fs::remove_all(base);

  // exe 同目录命中。
  WriteFile(base / "bin" / "crashpad_handler");
  EXPECT_EQ(chirp::common::crash::ResolveOptions(1, argv.get(), (base / "bin").string()).handler_path,
            (base / "bin" / "crashpad_handler").string());

  // 上溯命中(build/services/<组>/<服务>/ → build/ 为 3 级,在 4 级上限内)。
  fs::remove_all(base);
  WriteFile(base / "crashpad_handler");
  EXPECT_EQ(chirp::common::crash::ResolveOptions(1, argv.get(), (base / "s1" / "s2" / "s3").string()).handler_path,
            (base / "crashpad_handler").string());

  // 5 层深:同目录+4 级上溯都越不过,不命中。
  fs::remove_all(base);
  WriteFile(base / "crashpad_handler");
  EXPECT_EQ(chirp::common::crash::ResolveOptions(1, argv.get(), (base / "a" / "b" / "c" / "d" / "e").string()).handler_path,
            "");

  // 空 exe 目录:不做任何查找。
  fs::remove_all(base);
  EXPECT_EQ(chirp::common::crash::ResolveOptions(1, argv.get(), "").handler_path, "");
}

TEST(CrashHandlerTest, PeekFlagSkipsNullArgvSlots) {
  UnsetEnv d("CHIRP_CRASH_DIR"), u("CHIRP_CRASH_UPLOAD_URL"), h("CHIRP_CRASH_HANDLER");
  Argv argv = MakeArgv({"prog", "unused"});
  argv.ptrs[1] = nullptr;  // 循环体内 null 槽:跳过不崩,查找序不受影响
  const auto opts = chirp::common::crash::ResolveOptions(2, argv.get(), "");
  EXPECT_EQ(opts.crash_dir, "crash_dumps");
}

TEST(CrashHandlerTest, PendingReportsScansNewAndPendingOnly) {
  const fs::path base = fs::path(::testing::TempDir()) / "crash_pending_test";
  fs::remove_all(base);
  EXPECT_TRUE(chirp::common::crash::PendingReports(base.string()).empty());

  WriteFile(base / "new" / "b.dmp");
  WriteFile(base / "new" / "a.dmp");
  WriteFile(base / "new" / "ignored.txt");
  fs::create_directories(base / "new" / "subdir");  // 目录条目跳过
  WriteFile(base / "pending" / "c.dmp");
  WriteFile(base / "completed" / "d.dmp");  // 已上传区不扫
  const auto reports = chirp::common::crash::PendingReports(base.string());
  EXPECT_EQ(reports, (std::vector<std::string>{"new/a.dmp", "new/b.dmp", "pending/c.dmp"}));
}

#ifndef CHIRP_HAVE_CRASHPAD
// no-op 桩分支:建目录成功、返回 false、遗留 dump 不影响返回值。
// (crashpad-ON 构建下 Initialize 会真拉起 handler 进程,不在此断言。)
TEST(CrashHandlerTest, InitializeNoopStubCreatesDirAndDegrades) {
  UnsetEnv d("CHIRP_CRASH_DIR"), u("CHIRP_CRASH_UPLOAD_URL"), h("CHIRP_CRASH_HANDLER");
  const fs::path base = fs::path(::testing::TempDir()) / "crash_init_test";
  fs::remove_all(base);

  Argv argv = MakeArgv({"prog", "--crash_dir=" + base.string()});
  EXPECT_FALSE(chirp::common::crash::Initialize(2, argv.get()));
  EXPECT_TRUE(fs::is_directory(base));

  // 复跑:遗留 dump 在场,Warn 行照打,仍正常降级。
  WriteFile(base / "new" / "leftover.dmp");
  EXPECT_FALSE(chirp::common::crash::Initialize(2, argv.get()));
  EXPECT_EQ(chirp::common::crash::PendingReports(base.string()),
            (std::vector<std::string>{"new/leftover.dmp"}));
}

TEST(CrashHandlerTest, InitializeBadCrashDirDegrades) {
  UnsetEnv u("CHIRP_CRASH_UPLOAD_URL"), h("CHIRP_CRASH_HANDLER");
  // 目录路径被普通文件占位:create_directories 失败 → Warn + false。
  const fs::path blocker = WriteFile(fs::path(::testing::TempDir()) / "crash_init_blocker");
  Argv argv = MakeArgv({"prog", "--crash_dir=" + blocker.string()});
  EXPECT_FALSE(chirp::common::crash::Initialize(2, argv.get()));
}
#endif  // !CHIRP_HAVE_CRASHPAD
