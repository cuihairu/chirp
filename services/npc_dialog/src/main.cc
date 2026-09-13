// NPC dialog service: a server-plane client (no player-facing listener).
//
// Dials the hub as a service, receives npc.player_message events, answers
// through the injection path, and acknowledges. Flags mirror the chat
// service's server-gateway flags.
#include <asio.hpp>

#include <csignal>
#include <functional>
#include <memory>
#include <string>

#include "logger.h"
#include "npc_engine.h"
#include "npc_responder.h"
#include "proto/server_gateway.pb.h"
#include "runtime_utils.h"
#include "server_gateway_peer.h"

namespace {

using chirp::common::Logger;

}  // namespace

int main(int argc, char** argv) {
  Logger::Instance().SetLevel(Logger::Level::kInfo);

  const std::string hub_host =
      chirp::chat::runtime::GetArg(argc, argv, "--server_gateway_host", "");
  if (hub_host.empty()) {
    Logger::Instance().Warn(
        "no --server_gateway_host configured; the NPC dialog service has "
        "nothing to do without the server plane - exiting");
    return 0;
  }
  const uint16_t hub_port = chirp::chat::runtime::ParseU16Arg(
      argc, argv, "--server_gateway_port", 8100);

  // Rules: --rules_file when given (missing file keeps the demo rules and
  // says so), built-in demo rules otherwise.
  auto engine = std::make_shared<chirp::npc::RuleBasedNpcEngine>(chirp::npc::DemoRules());
  const std::string rules_file =
      chirp::chat::runtime::GetArg(argc, argv, "--rules_file", "");
  if (rules_file.empty()) {
    Logger::Instance().Warn("no --rules_file; using built-in demo rules");
  } else if (!engine->LoadFile(rules_file)) {
    Logger::Instance().Warn("cannot open rules file " + rules_file +
                            "; using built-in demo rules");
  } else {
    Logger::Instance().Info("loaded " + std::to_string(engine->rule_count()) +
                            " npc rules from " + rules_file);
  }
  engine->set_fallback_reply(
      chirp::chat::runtime::GetArg(argc, argv, "--fallback_reply", "..."));

  asio::io_context io;

  chirp::chat::ServerGatewayPeer::Options options;
  options.host = hub_host;
  options.port = hub_port;
  options.service_id =
      chirp::chat::runtime::GetArg(argc, argv, "--server_gateway_service", "npc_dialog");
  options.secret =
      chirp::chat::runtime::GetArg(argc, argv, "--server_gateway_secret", "");
  options.reconnect_delay_seconds = chirp::chat::runtime::ParseIntArg(
      argc, argv, "--server_gateway_reconnect", 3);

  // The responder needs the peer (to inject replies and ack) and the peer's
  // event handler needs the responder; break the cycle through the peer
  // variable, which outlives the io loop below.
  std::shared_ptr<chirp::chat::ServerGatewayPeer> peer;
  auto responder = std::make_shared<chirp::npc::NpcResponder>(
      *engine,
      [&peer](const chirp::server_gateway::MessageInjectRequest& req,
              std::function<void(chirp::common::ErrorCode)> cb) {
        peer->SendInject(req, std::move(cb));
      },
      [&peer](const chirp::server_gateway::EventAckRequest& req,
              std::function<void(chirp::common::ErrorCode)> cb) {
        peer->SendEventAck(req, std::move(cb));
      });
  peer = chirp::chat::ServerGatewayPeer::Create(
      io, std::move(options),
      [](const chirp::server_gateway::InjectMessageNotify&) {},  // consumes events only
      [responder](const chirp::server_gateway::EventDeliverNotify& event) {
        responder->OnEvent(event);
      });
  peer->Start();

  asio::signal_set signals(io, SIGINT, SIGTERM);
  signals.async_wait([&](const std::error_code& /*ec*/, int /*sig*/) {
    Logger::Instance().Info("shutdown requested");
    peer->Stop();
    io.stop();
  });

  Logger::Instance().Info("npc_dialog service connecting to " + hub_host + ":" +
                          std::to_string(hub_port) + " as " +
                          (rules_file.empty() ? std::string("demo rules")
                                              : rules_file));
  io.run();
  return 0;
}
