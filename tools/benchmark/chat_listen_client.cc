#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

#include <sys/socket.h>
#include <sys/time.h>

#include <asio.hpp>

#include "network/byte_order.h"
#include "network/protobuf_framing.h"
#include "proto/auth.pb.h"
#include "proto/chat.pb.h"
#include "proto/gateway.pb.h"

namespace {

std::string GetArg(int argc, char** argv, const std::string& key, const std::string& def) {
  for (int i = 1; i < argc; i++) {
    if (argv[i] == key && i + 1 < argc) {
      return argv[i + 1];
    }
  }
  return def;
}

bool ReadFrame(asio::ip::tcp::socket& sock, std::string* payload) {
  uint8_t len_be[4];
  asio::error_code ec;
  asio::read(sock, asio::buffer(len_be, 4), ec);
  if (ec) {
    return false;
  }
  const uint32_t len = chirp::network::ReadU32BE(len_be);
  payload->resize(len);
  asio::read(sock, asio::buffer(payload->data(), payload->size()), ec);
  return !ec;
}

void SendPacket(asio::ip::tcp::socket& sock, chirp::gateway::MsgID msg_id, int64_t seq, const std::string& body) {
  chirp::gateway::Packet pkt;
  pkt.set_msg_id(msg_id);
  pkt.set_sequence(seq);
  pkt.set_body(body);
  auto out = chirp::network::ProtobufFraming::Encode(pkt);
  asio::write(sock, asio::buffer(out));
}

} // namespace

int main(int argc, char** argv) {
  const std::string host = GetArg(argc, argv, "--host", "127.0.0.1");
  const uint16_t port = static_cast<uint16_t>(std::atoi(GetArg(argc, argv, "--port", "7000").c_str()));
  const std::string user = GetArg(argc, argv, "--user", "user_2");
  const int max_msgs = std::atoi(GetArg(argc, argv, "--max", "1").c_str());
  const int timeout_ms = std::atoi(GetArg(argc, argv, "--timeout-ms", "0").c_str());

  asio::io_context io;
  asio::ip::tcp::resolver resolver(io);
  asio::ip::tcp::socket sock(io);
  asio::connect(sock, resolver.resolve(host, std::to_string(port)));

  // Bound every blocking read: without SO_RCVTIMEO a server that accepts
  // the connection but never replies keeps this client hanging forever.
  if (timeout_ms > 0) {
    struct timeval tv {};
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    ::setsockopt(sock.native_handle(), SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  }

  int got = 0;
  auto print_notify = [&got](const chirp::gateway::Packet& pkt) -> bool {
    if (pkt.msg_id() == chirp::gateway::CHAT_MESSAGE_NOTIFY) {
      chirp::chat::ChatMessage msg;
      if (msg.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
        std::cout << "notify ts=" << msg.timestamp() << " " << msg.sender_id() << " -> " << msg.receiver_id()
                  << " id=" << msg.message_id() << " bytes=" << msg.content().size() << "\n";
        got++;
        return true;
      }
    } else if (pkt.msg_id() == chirp::gateway::MESSAGE_READ_NOTIFY) {
      chirp::chat::MessageReadNotify msg;
      if (msg.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
        std::cout << "read_notify channel=" << msg.channel_id() << " reader=" << msg.reader_user_id()
                  << " message=" << msg.message_id() << " read_at=" << msg.read_at() << "\n";
        got++;
        return true;
      }
    } else if (pkt.msg_id() == chirp::gateway::TYPING_INDICATOR_NOTIFY) {
      chirp::chat::TypingIndicator msg;
      if (msg.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
        std::cout << "typing_notify channel=" << msg.channel_id() << " user=" << msg.user_id()
                  << " is_typing=" << (msg.is_typing() ? 1 : 0) << "\n";
        got++;
        return true;
      }
    } else if (pkt.msg_id() == chirp::gateway::REACTION_ADDED_NOTIFY) {
      chirp::chat::ReactionAddedNotify msg;
      if (msg.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
        std::cout << "reaction_added channel=" << msg.channel_id() << " user=" << msg.user_id()
                  << " emoji=" << msg.emoji() << " message=" << msg.message_id() << "\n";
        got++;
        return true;
      }
    } else if (pkt.msg_id() == chirp::gateway::REACTION_REMOVED_NOTIFY) {
      chirp::chat::ReactionRemovedNotify msg;
      if (msg.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
        std::cout << "reaction_removed channel=" << msg.channel_id() << " user=" << msg.user_id()
                  << " emoji=" << msg.emoji() << " message=" << msg.message_id() << "\n";
        got++;
        return true;
      }
    } else if (pkt.msg_id() == chirp::gateway::MESSAGE_EDITED_NOTIFY) {
      chirp::chat::MessageEditedNotify msg;
      if (msg.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
        std::cout << "edited_notify channel=" << msg.channel_id() << " by=" << msg.edited_by()
                  << " message=" << msg.message_id() << " content=" << msg.new_content() << "\n";
        got++;
        return true;
      }
    } else if (pkt.msg_id() == chirp::gateway::MESSAGE_DELETED_NOTIFY) {
      chirp::chat::MessageDeletedNotify msg;
      if (msg.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
        std::cout << "deleted_notify channel=" << msg.channel_id() << " by=" << msg.deleted_by()
                  << " message=" << msg.message_id() << " hard=" << (msg.is_hard_delete() ? 1 : 0)
                  << "\n";
        got++;
        return true;
      }
    }
    return false;
  };

  // Login to register this connection. Keep reading until the LOGIN_RESP:
  // a refill notify may arrive before it depending on the server build,
  // and swallowing it would strand the message loop waiting for a frame
  // that already came. Notify frames seen here still count toward --max.
  {
    chirp::auth::LoginRequest req;
    req.set_token(user);
    req.set_device_id("bench");
    req.set_platform("pc");
    SendPacket(sock, chirp::gateway::LOGIN_REQ, 1, req.SerializeAsString());

    while (true) {
      std::string payload;
      if (!ReadFrame(sock, &payload)) {
        std::cerr << "failed to read login response\n";
        return 1;
      }
      chirp::gateway::Packet pkt;
      if (!pkt.ParseFromArray(payload.data(), static_cast<int>(payload.size()))) {
        continue;
      }
      if (pkt.msg_id() == chirp::gateway::LOGIN_RESP) {
        chirp::auth::LoginResponse resp;
        if (resp.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
          std::cout << "login_resp code=" << resp.code() << " user=" << resp.user_id() << "\n";
        }
        break;
      }
      print_notify(pkt);
    }
  }

  while (max_msgs <= 0 || got < max_msgs) {
    std::string payload;
    if (!ReadFrame(sock, &payload)) {
      std::cerr << "socket closed\n";
      return 1;
    }

    chirp::gateway::Packet pkt;
    if (!pkt.ParseFromArray(payload.data(), static_cast<int>(payload.size()))) {
      continue;
    }

    print_notify(pkt);
  }

  return 0;
}

