// chirp_search: 消息检索服务（message_search 批）。
//
// MySQL messages 表 -> SQLite FTS5 索引（启动全量回填 + 100ms tail 泵），
// 5007 上挂 gateway 的 per-client pipe：SERVER_AUTH_REQ 信任门 + LOGIN 重放
// + SEARCH_MESSAGE_REQ 检索。拓扑与协议见 docs/design-notes/message_search.md。
#include <asio.hpp>

#include <csignal>
#include <cstdio>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "common/metrics.h"
#include "common/metrics_http_server.h"
#include "common/crash_handler.h"
#include "index_sync.h"
#include "logger.h"
#include "message_search_index.h"
#include "mysql_message_store.h"
#include "network/tcp_server.h"
#include "runtime_utils.h"
#include "search_server.h"

namespace {

using chirp::common::Logger;

}  // namespace

int main(int argc, char** argv) {
  // 崩溃采集:main 首条语句,早于一切 flag/日志初始化(见 CRASH_COLLECTION.md)。
  chirp::common::crash::Initialize(argc, argv);
  Logger::Instance().SetLevel(Logger::Level::kInfo);

  const std::string host =
      chirp::chat::runtime::GetArg(argc, argv, "--host", "0.0.0.0");
  const uint16_t port =
      chirp::chat::runtime::ParseU16Arg(argc, argv, "--port", 5007);
  const std::string db_path =
      chirp::chat::runtime::GetArg(argc, argv, "--db_path", "search_index.db");
  const uint16_t metrics_port =
      chirp::chat::runtime::ParseU16Arg(argc, argv, "--metrics_port", 0);

  // MySQL 是权威事实源：没有它检索只剩一个可能滞后的索引（错误结果比没有
  // 结果更糟），与 npc_dialog 无 hub 即退出同一姿态。
  const std::string mysql_host =
      chirp::chat::runtime::GetArg(argc, argv, "--mysql_host", "");
  if (mysql_host.empty()) {
    Logger::Instance().Warn(
        "no --mysql_host configured; search has no authoritative source to "
        "serve from - exiting");
    return 0;
  }
  const uint16_t mysql_port =
      chirp::chat::runtime::ParseU16Arg(argc, argv, "--mysql_port", 3306);
  const std::string mysql_db =
      chirp::chat::runtime::GetArg(argc, argv, "--mysql_database", "chirp");
  const std::string mysql_user =
      chirp::chat::runtime::GetArg(argc, argv, "--mysql_user", "chirp");
  const std::string mysql_password =
      chirp::chat::runtime::GetArg(argc, argv, "--mysql_password", "");

  chirp::search::SearchServer::Options options;
  // 生产：网关 per-client pipe 先过 SERVER_AUTH_REQ 信任门。两项都留空 =
  // 脚手架形态（本地直连调试），与 chat 直连入口同一约定。
  options.service_secret =
      chirp::chat::runtime::GetArg(argc, argv, "--gateway_service_secret", "");
  options.token_secret =
      chirp::chat::runtime::GetArg(argc, argv, "--token_secret", "");

  asio::io_context io;

  auto pool = std::make_shared<chirp::chat::MySQLConnectionPool>(
      4, mysql_host, mysql_port, mysql_db, mysql_user, mysql_password);

  auto index = std::make_shared<chirp::search::MessageSearchIndex>();
  std::string open_err;
  if (!index->Open(db_path, &open_err)) {
    Logger::Instance().Error("cannot open search index at " + db_path +
                             ": " + open_err);
    return 1;
  }

  auto sync = std::make_shared<chirp::search::MessageIndexSync>(*index, *pool);
  int64_t backfilled = 0;
  std::string backfill_err;
  if (!sync->Backfill(&backfilled, &backfill_err)) {
    Logger::Instance().Error("search index backfill failed: " + backfill_err);
    return 1;
  }
  Logger::Instance().Info("search index ready: " + std::to_string(backfilled) +
                          " messages backfilled from mysql, " +
                          std::to_string(index->DocumentCount()) +
                          " documents in index");

  chirp::search::SearchServer server(*index, sync.get(), std::move(options));

  chirp::network::TcpServer tcp(
      io, port,
      [&server](const std::shared_ptr<chirp::network::Session>& session,
                std::string&& payload) { server.HandleFrame(session, std::move(payload)); },
      [&server](const std::shared_ptr<chirp::network::Session>& session) {
        server.HandleClose(session);
      });
  tcp.Start();

  // tail 泵：100ms 一次，把 id 游标之上的新消息推进索引。失败只记日志，
  // 下一拍重试（MySQL 短暂抖动不该杀掉服务）。
  asio::steady_timer tail_timer(io);
  std::function<void(const std::error_code&)> pump_tail;
  pump_tail = [&](const std::error_code& ec) {
    if (ec) {
      return;  // 取消/停机
    }
    std::string pump_err;
    if (!sync->PumpTail(&pump_err)) {
      Logger::Instance().Warn("search tail pump failed: " + pump_err);
    }
    tail_timer.expires_after(std::chrono::milliseconds(100));
    tail_timer.async_wait(pump_tail);
  };
  tail_timer.expires_after(std::chrono::milliseconds(100));
  tail_timer.async_wait(pump_tail);

  std::optional<chirp::common::MetricsHttpServer> metrics_server;
  if (metrics_port > 0) {
    metrics_server.emplace(io, metrics_port);
    if (metrics_server->Start()) {
      Logger::Instance().Info("Metrics endpoint listening on TCP:" +
                              std::to_string(metrics_port) + " (/metrics)");
    } else {
      Logger::Instance().Warn("Metrics endpoint failed to bind TCP:" +
                              std::to_string(metrics_port) + "; continuing without metrics");
      metrics_server.reset();
    }
  }

  asio::signal_set signals(io, SIGINT, SIGTERM);
  signals.async_wait([&](const std::error_code& /*ec*/, int /*sig*/) {
    Logger::Instance().Info("search service shutting down");
    tail_timer.cancel();
    tcp.Stop();
    if (metrics_server) {
      metrics_server->Stop();
    }
    io.stop();
  });

  Logger::Instance().Info("chirp_search listening on " + host + ":" +
                          std::to_string(port) + " (index: " + db_path + ")");
  io.run();
  Logger::Instance().Info("search service stopped.");
  return 0;
}
