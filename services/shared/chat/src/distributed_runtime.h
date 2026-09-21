#pragma once

#include <functional>
#include <memory>

#include <asio.hpp>

#include "network/session.h"
#include "network/tcp_server.h"
#include "network/websocket_server.h"
#include "proto/gateway.pb.h"

namespace chirp::chat::runtime {

using PacketHandler = std::function<void(const std::shared_ptr<network::Session>& session,
                                         const gateway::Packet& pkt)>;
using DisconnectHandler = std::function<void(const std::shared_ptr<network::Session>& session)>;
using ShutdownHandler = std::function<void()>;

std::unique_ptr<network::TcpServer> MakeDistributedTcpServer(asio::io_context& io,
                                                             uint16_t port,
                                                             PacketHandler on_packet,
                                                             DisconnectHandler on_disconnect);

std::unique_ptr<network::WebSocketServer> MakeDistributedWsServer(asio::io_context& io,
                                                                  uint16_t port,
                                                                  PacketHandler on_packet,
                                                                  DisconnectHandler on_disconnect);

void InstallSignalStop(asio::io_context& io, ShutdownHandler on_shutdown);

}  // namespace chirp::chat::runtime
