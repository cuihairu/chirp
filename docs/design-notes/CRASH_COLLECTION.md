# 崩溃采集（Crashpad）设计简档

> 2026-10-08 简档（先设计后实现）。选型已钉：**Google Crashpad**，C++ 直集成；
> Breakpad 只作历史对照，不引入（其 on-process handler 与构建链均为上一代方案）。

## 目标与边界

- 进程崩溃（SIGSEGV/SIGABRT/…）落 minidump 到本地磁盘，符号化还原出调用栈。
- **先本地落盘不外发**：上传属数据外发，默认关（留位见下）。
- 不新增监控系统：与既有日志面（`libs/common/logger`）打通，Warn 级结构化行即
  接入点。

## 接入方式（CMake）

- **FetchContent 钉固定版本**，不走子模块：与根 `CMakeLists.txt` 既有 abseil/asio
  接入同款（`CHIRP_FETCH_DEPS` 门控），免去 CI workflow 全部加
  `submodules: checkout` 的改动面。
  - 仓库：`https://github.com/backtrace-labs/crashpad.git`
  - 钉 **tag `v0.3.0`**（分支 backtrace，tag 提交 `3f7fee9e54…`，2026 年仍在
    维护的分叉；其自带完整分目录 CMake 构建——`client`/`handler`/`tools`/
    `compat`/`snapshot`/`minidump` 各为一个 target，不需社区 wrapper 仓）。
- 新开关 `CHIRP_ENABLE_CRASHPAD`（缺省 `ON`，仅 Linux 与 `CHIRP_FETCH_DEPS=ON`
  时实际拉取；关闭/拉取失败时 `crash::Initialize` 退化为返回 `false` 的 no-op
  桩，所有服务照常编译运行）。
- **警告与插桩隔离**：根目录 `add_compile_options`（`-Wall -Wextra -Wpedantic`
  `-Werror`（CI）与覆盖率 `-ftest-coverage`）通过目录属性继承会灌进第三方源。
  接入前临时清空根目录 `COMPILE_OPTIONS`、`FetchContent_MakeAvailable` 之后
  恢复，保证：① CI `-Werror` 不被 2010s 风格的第三方代码撞翻；② 覆盖率构建
  不给 crashpad 插桩（其源码不在门禁 universe 内，只白耗时间）。

## handler 初始化点

- 位置：**每个服务 `main()` 的第一条语句**
  `chirp::common::crash::Initialize(argc, argv)`——在一切 flag 解析、日志初始化
  之前，启动期崩溃同样接住。返回值忽略（失败降级为无崩溃采集 + 一行 Warn 日志）。
- 初始化做的事（`libs/common/crash_handler.{h,cc}`）：
  1. 解析崩溃目录与上传 URL（见下）；
  2. 扫描上次会话遗留 dump → 打 Warn 日志（日志/监控打通面）；
  3. `CrashpadClient::StartHandler(handler 路径, database, database, url,
     annotations, arguments, /*restartable*/ true, /*asynchronous_start*/ false)`。
- 服务面覆盖：全部 `services/**/main*.cc`（探针 `tools/crash_probe` 同源调用）。
  SDK 示例（`sdks/core/examples`）不在本批范围。

## dump 落盘目录

查找序（先命中先用）：

1. `--crash_dir=DIR`（对 argv 做逐参 peek，不接管各服务自己的 flag 解析）；
2. 环境变量 `CHIRP_CRASH_DIR`；
3. 缺省 `./crash_dumps`（进程 cwd）。

目录不存在则创建（mkdir -p 语义）。目录内容是 crashpad 自有的数据库布局
（`new/` `pending/` `completed/` `last_upload_attempt`），**不手工管理**，也
不进 git（`.gitignore` 加 `crash_dumps/`）。

## 独立 handler 进程打包

- `crashpad_handler` 是独立可执行文件，由 client `StartHandler` 从进程外拉起
  （handler 崩了不连坐主进程；主进程崩溃时 handler 还活着才能写盘）。
- 运行时查找序：
  1. 环境变量 `CHIRP_CRASH_HANDLER`（运维覆盖）；
  2. 与调用进程**同目录**（安装/发布包的平铺布局）；
  3. 从 exe 目录**向上最多 4 级父目录**（build 树：`build/services/<组>/<服务>/`
     → `build/`）。
- 产物面：CMake 构建收尾把 handler 复制为 **build 根产物**
  （`build/crashpad_handler`），与 `test_services.sh` 的 `build/` 布局同处一级。
- 安装/nightly 打包面（已落地）：nightly Linux 腿构建步加编 handler 复制
  目标（`chirp_crashpad_handler_copy`），装配把 `build/crashpad_handler`
  平铺进 `cpp/<arch>/bin/`，package job 经既有 `bin/` 目录探测带进
  tarball；`install.sh --component cpp` 把包内 `bin/` 平铺进
  `$PREFIX/bin`。darwin/windows 腿 crashpad 关（平台门），包内如实不含
  handler（`install.ps1` 有注记）。安装后查找序命中已实测：probe 与
  handler 同目录、子目录上溯一级均命中 `$PREFIX/bin/crashpad_handler`。

## 符号表管理

- **构建全程留 `-g`**：dev/coverage preset 是 Debug 天然带；ci/minimal 是
  Release，靠分离符号流程：
  1. `scripts/extract_symbols.sh <binary> [symbols_dir]`：
     `objcopy --only-keep-debug`（分离符号归档）→ `dump_syms <bin>` 生成
     breakpad `.sym` → 按符号服务器布局 `<name>/<debug-id>/<name>.sym`
     摆放；**原二进制不动**，发布面需要瘦身时另跑 `strip --strip-debug`。
- **还原栈**：`minidump-stackwalk <dump> --symbols-path symbols/`，输出带
  `file:line` 与函数名的调用栈。
- 工具依赖（如实声明，不入仓）：`dump_syms` 与 `minidump-stackwalk` 均为
  rust-minidump 套件，`cargo install dump_syms minidump-stackwalk` 安装。
  （Breakpad 语义的 `.sym` 格式是两者共同的中间格式；不引入 Breakpad 源码。）
- **nightly 符号包归档（已落地）**：Linux 腿 `cargo install dump_syms`
  （cargo 由既有 Setup Rust 步提供）后对 `libchirp_core_sdk.so` 与
  `crashpad_handler` 跑 `extract_symbols.sh`，产
  `symbols/<arch>/chirp-cpp-symbols-<arch>.tar.gz` **独立成件**（不进主
  安装包），随 nightly-dist 分支镜像与滚动 Release 归档，manifest 单列
  `symbols` 组件；静态库无运行时符号面不产。

## 上传留位（默认关）

- `StartHandler` 的 URL 参数：**空串 = 纯本地落盘，不外发**（crashpad 不启
  uploader 线程）。
- 留位：`CHIRP_CRASH_UPLOAD_URL` 环境变量（或 `--crash_upload_url=` peek）。
  **缺省为空 = 关**；非空时才把该 URL 传给 handler 启用其自带上传面。打开
  属数据外发决策，需单独评估，不随本批默认开。

## 与既有日志/监控打通

- `Initialize` 扫描数据库：上一会话若有未处理 dump，打 Warn
  （`crash report pending: <path>`）——部署侧既有日志采集（log 落盘/stdout）
  即接入点，不新开监控面。
- annotations 注入每份 dump：`binary`（argv0）、`pid`、`cmd`（截断后的命令行）、
  `crash_dir`——现场还原与日志行互为索引。

## 验收（故意崩溃测试）

1. 新增 `tools/crash_probe`：初始化 crashpad → 打印就绪信息 →
   `--crash_probe_segfault` 开关触发空指针写；
2. 运行 → dump 落盘于指定 `--crash_dir`，退出后数据库留有记录；
3. `dump_syms` 生成 .sym → `minidump-stackwalk` 还原出**含 `crash_probe` 帧与
   `file:line`** 的调用栈；日志/输出留档报验收。

## 覆盖率与门禁对账

- `libs/common/crash_handler.cc` 在门禁 universe（`libs/**`）内：可测面——
  目录查找序（argv/env/缺省）、目录创建、URL 缺省关、数据库扫描出 Warn、
  handler 缺失时的失败降级——全部用单测覆盖。
- **覆盖率构建（`CHIRP_ENABLE_COVERAGE=ON`）强制关 crashpad**：第三方源
  既不入 universe（gcov 聚合只认 `libs/` `services/` `sdks/core/src`
  前缀）也不参与插桩（省时）；此时 `StartHandler` 分支整体不编译
  （`CHIRP_HAVE_CRASHPAD` 未定义），无 uncovered 行、无需豁免钉——
  `Initialize` 编为短路径桩（建目录 + 遗留 Warn + `return false`），
  单测安全断言。真拉起 handler 的 E2E 责任归 `crash_probe` 验收腿。
- 本仓源 include 的 crashpad 头带 GCC 扩展（`#include_next`），
  `crash_handler.cc` 在 include 点局部压 `-Wpedantic`，自身代码照常受
  `-Wall -Wextra -Wpedantic`（CI 叠 `-Werror`）约束。
- `sdks/core` 的 `libchirp_core_sdk.so` 链入 crashpad 静态库：FetchContent
  作用域内全局 `CMAKE_POSITION_INDEPENDENT_CODE=ON`。

## 范围外（如实）

- 崩溃**采集**目前只在 Linux 生效：`CHIRP_ENABLE_CRASHPAD` 被平台门限制在
  Linux（与仓内 CI 一致），darwin/windows 包如实不含 handler。但
  `libs/common/crash_handler.*` 本身可跨平台编译（exe 目录探测按
  `_WIN32` 分支到 `GetModuleFileNameA`，pid 用 `_getpid`），非 Linux 平台
  上是无采集的降级桩，不阻断 SDK 构建。
- 上传端点、多机集中检索、alert 联动：外发面默认关，另批评估。
