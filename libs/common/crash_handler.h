#pragma once

#include <string>
#include <vector>

namespace chirp::common::crash {

// 崩溃采集配置(查找序解析结果;见 docs/design-notes/CRASH_COLLECTION.md)。
struct Options {
  std::string crash_dir;     // dump 落盘目录(兼作 crashpad 数据库根)
  std::string upload_url;    // 空 = 不外发(缺省;外发属数据外发决策)
  std::string handler_path;  // 独立 handler 可执行文件;空 = 未找到(禁用采集)
};

// 本进程可执行文件所在目录(/proc/self/exe;失败返回空串)。
std::string SelfExeDir();

// 解析崩溃目录/上传 URL/handler 路径(可测面)。
//   crash_dir:   --crash_dir= > $CHIRP_CRASH_DIR > ./crash_dumps
//   upload_url:  --crash_upload_url= > $CHIRP_CRASH_UPLOAD_URL > ""
//   handler:     $CHIRP_CRASH_HANDLER > exe_dir/crashpad_handler > 向上至多 4 级
// argv 只做逐参 peek,不消费、不接管各服务自己的 flag 解析。
Options ResolveOptions(int argc, char** argv, const std::string& exe_dir);

// 扫描数据库遗留 dump(上一会话未处理),返回相对 crash_dir 的路径列表。
// 目录不存在返回空;结果按字典序稳定输出。
std::vector<std::string> PendingReports(const std::string& crash_dir);

// 服务 main() 的首条语句调用:建目录 → 遗留 dump 打 Warn(日志/监控接入点)
// → 拉起独立 handler 进程。返回 true = handler 已就位。未编译 crashpad
// (覆盖率构建/平台不支持)、目录不可用、handler 缺失或 StartHandler 失败时
// 返回 false,服务照常运行(无崩溃采集降级)。
bool Initialize(int argc, char** argv);

}  // namespace chirp::common::crash
