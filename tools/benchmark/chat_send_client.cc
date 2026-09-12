#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

#include <asio.hpp>

#include "network/byte_order.h"
#include "network/protobuf_framing.h"
#include "proto/auth.pb.h"
#include "proto/chat.pb.h"
#include "proto/gateway.pb.h"

namespace {

int64_t NowMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::string GetArg(int argc, char** argv, const std::string& key, const std::string& def) {
  for (int i = 1; i < argc; i++) {
    if (argv[i] == key && i + 1 < argc) {
      return argv[i + 1];
    }
  }
  return def;
}

bool HasArg(int argc, char** argv, const std::string& key) {
  for (int i = 1; i < argc; i++) {
    if (argv[i] == key) {
      return true;
    }
  }
  return false;
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

bool SendAndRead(asio::ip::tcp::socket& sock,
                 chirp::gateway::MsgID msg_id,
                 int64_t seq,
                 const std::string& body,
                 chirp::gateway::Packet* out_pkt) {
  chirp::gateway::Packet pkt;
  pkt.set_msg_id(msg_id);
  pkt.set_sequence(seq);
  pkt.set_body(body);
  auto out = chirp::network::ProtobufFraming::Encode(pkt);
  asio::write(sock, asio::buffer(out));

  // The server may push notifications (chat messages, group events) before
  // the response for this request; skip frames until the matching response
  // with the same sequence arrives.
  for (;;) {
    std::string payload;
    if (!ReadFrame(sock, &payload)) {
      return false;
    }
    if (!out_pkt->ParseFromArray(payload.data(), static_cast<int>(payload.size()))) {
      return false;
    }
    if (out_pkt->sequence() == seq) {
      return true;
    }
  }
}

std::string PrivateChannelId(std::string a, std::string b) {
  if (a <= b) {
    return a + "|" + b;
  }
  return b + "|" + a;
}

} // namespace

int main(int argc, char** argv) {
  const std::string host = GetArg(argc, argv, "--host", "127.0.0.1");
  const uint16_t port = static_cast<uint16_t>(std::atoi(GetArg(argc, argv, "--port", "7000").c_str()));

  const std::string sender = GetArg(argc, argv, "--sender", "user_1");
  const std::string receiver = GetArg(argc, argv, "--receiver", "user_2");
  const std::string text = GetArg(argc, argv, "--text", "hello from chirp_chat_send_client");
  // --create_group <name>: create a group (comma-separated --initial members)
  // and print the assigned group_id. --group <id>: send a group message.
  const std::string create_group = GetArg(argc, argv, "--create_group", "");
  const std::string group = GetArg(argc, argv, "--group", "");
  // Feature actions over the established private channel (sender|receiver):
  // --act mark_read|get_receipts|typing_start|typing_stop|react|unreact|get_reactions
  // with --message-id <id> and --emoji <emoji> where applicable.
  const std::string act = GetArg(argc, argv, "--act", "");
  const std::string message_id = GetArg(argc, argv, "--message-id", "");
  const std::string emoji = GetArg(argc, argv, "--emoji", "👍");

  asio::io_context io;
  asio::ip::tcp::resolver resolver(io);
  asio::ip::tcp::socket sock(io);
  asio::connect(sock, resolver.resolve(host, std::to_string(port)));

  // Login to register this connection as sender.
  {
    chirp::auth::LoginRequest req;
    req.set_token(sender);
    req.set_device_id("bench");
    req.set_platform("pc");

    chirp::gateway::Packet resp_pkt;
    if (!SendAndRead(sock, chirp::gateway::LOGIN_REQ, 1, req.SerializeAsString(), &resp_pkt) ||
        resp_pkt.msg_id() != chirp::gateway::LOGIN_RESP) {
      std::cerr << "login failed\n";
      return 1;
    }
  }

  if (!create_group.empty()) {
    chirp::chat::CreateGroupRequest greq;
    greq.set_creator_id(sender);
    greq.set_group_name(create_group);
    const std::string initial = GetArg(argc, argv, "--initial", "");
    size_t start = 0;
    while (start <= initial.size()) {
      size_t comma = initial.find(',', start);
      if (comma == std::string::npos) {
        comma = initial.size();
      }
      if (comma > start) {
        greq.add_initial_members(initial.substr(start, comma - start));
      }
      start = comma + 1;
    }

    chirp::gateway::Packet resp_pkt;
    if (!SendAndRead(sock, chirp::gateway::CREATE_GROUP_REQ, 2, greq.SerializeAsString(), &resp_pkt) ||
        resp_pkt.msg_id() != chirp::gateway::CREATE_GROUP_RESP) {
      std::cerr << "create group failed\n";
      return 1;
    }
    chirp::chat::CreateGroupResponse gresp;
    if (!gresp.ParseFromArray(resp_pkt.body().data(), static_cast<int>(resp_pkt.body().size())) ||
        gresp.code() != chirp::common::OK) {
      std::cerr << "create group rejected code=" << gresp.code() << "\n";
      return 1;
    }
    std::cout << "group_id=" << gresp.group_id() << "\n";
    return 0;
  }

  int64_t seq = 3;

  // Feature actions run instead of a plain message send.
  if (!act.empty()) {
    chirp::gateway::Packet resp_pkt;
    chirp::gateway::MsgID req_id = chirp::gateway::MARK_READ_REQ;
    chirp::gateway::MsgID resp_id = chirp::gateway::MARK_READ_RESP;
    std::string body;

    const std::string channel = PrivateChannelId(sender, receiver);
    if (act == "mark_read" || act == "get_receipts") {
      if (act == "mark_read") {
        chirp::chat::MarkReadRequest req;
        req.set_user_id(sender);
        req.set_channel_id(channel);
        req.set_channel_type(chirp::chat::PRIVATE);
        req.set_message_id(message_id);
        body = req.SerializeAsString();
      } else {
        chirp::chat::GetReadReceiptsRequest req;
        req.set_message_id(message_id);
        body = req.SerializeAsString();
        req_id = chirp::gateway::GET_READ_RECEIPTS_REQ;
        resp_id = chirp::gateway::GET_READ_RECEIPTS_RESP;
      }
    } else if (act == "typing_start" || act == "typing_stop") {
      chirp::chat::TypingIndicator req;
      req.set_channel_id(channel);
      req.set_channel_type(chirp::chat::PRIVATE);
      req.set_user_id(sender);
      req.set_username(sender);
      req.set_is_typing(act == "typing_start");
      req.set_timestamp(NowMs());
      // Inbound-only: the server broadcasts to the other party and never
      // replies on this connection, so fire and forget.
      chirp::gateway::Packet pkt;
      pkt.set_msg_id(chirp::gateway::TYPING_INDICATOR_NOTIFY);
      pkt.set_sequence(seq);
      pkt.set_body(req.SerializeAsString());
      auto out = chirp::network::ProtobufFraming::Encode(pkt);
      asio::write(sock, asio::buffer(out));
      std::cout << act << " sent\n";
      return 0;
    } else if (act == "react" || act == "unreact") {
      if (act == "react") {
        chirp::chat::AddReactionRequest req;
        req.set_message_id(message_id);
        req.set_user_id(sender);
        req.set_emoji(emoji);
        body = req.SerializeAsString();
        req_id = chirp::gateway::ADD_REACTION_REQ;
        resp_id = chirp::gateway::ADD_REACTION_RESP;
      } else {
        chirp::chat::RemoveReactionRequest req;
        req.set_message_id(message_id);
        req.set_user_id(sender);
        req.set_emoji(emoji);
        body = req.SerializeAsString();
        req_id = chirp::gateway::REMOVE_REACTION_REQ;
        resp_id = chirp::gateway::REMOVE_REACTION_RESP;
      }
    } else if (act == "get_reactions") {
      chirp::chat::GetReactionsRequest req;
      req.set_message_id(message_id);
      body = req.SerializeAsString();
      req_id = chirp::gateway::GET_REACTIONS_REQ;
      resp_id = chirp::gateway::GET_REACTIONS_RESP;
    } else if (act == "edit") {
      chirp::chat::EditMessageRequest req;
      req.set_message_id(message_id);
      req.set_user_id(sender);
      req.set_new_content(text);
      body = req.SerializeAsString();
      req_id = chirp::gateway::EDIT_MESSAGE_REQ;
      resp_id = chirp::gateway::EDIT_MESSAGE_RESP;
    } else if (act == "delete") {
      chirp::chat::DeleteMessageRequest req;
      req.set_message_id(message_id);
      req.set_user_id(sender);
      body = req.SerializeAsString();
      req_id = chirp::gateway::DELETE_MESSAGE_REQ;
      resp_id = chirp::gateway::DELETE_MESSAGE_RESP;
    } else if (act == "suggest") {
      chirp::chat::GetMentionSuggestionsRequest req;
      req.set_user_id(sender);
      req.set_channel_id(PrivateChannelId(sender, receiver));
      req.set_query(GetArg(argc, argv, "--query", ""));
      body = req.SerializeAsString();
      req_id = chirp::gateway::GET_MENTION_SUGGESTIONS_REQ;
      resp_id = chirp::gateway::GET_MENTION_SUGGESTIONS_RESP;
    } else if (act == "get_typing") {
      chirp::chat::GetTypingUsersRequest req;
      req.set_channel_id(channel);
      req.set_channel_type(chirp::chat::PRIVATE);
      body = req.SerializeAsString();
      req_id = chirp::gateway::GET_TYPING_USERS_REQ;
      resp_id = chirp::gateway::GET_TYPING_USERS_RESP;
    } else {
      std::cerr << "unknown act: " << act << "\n";
      return 1;
    }

    if (!SendAndRead(sock, req_id, seq, body, &resp_pkt) || resp_pkt.msg_id() != resp_id) {
      std::cerr << act << " failed (msg_id=" << resp_pkt.msg_id() << ")\n";
      return 1;
    }

    if (resp_id == chirp::gateway::GET_READ_RECEIPTS_RESP) {
      chirp::chat::GetReadReceiptsResponse r;
      r.ParseFromArray(resp_pkt.body().data(), static_cast<int>(resp_pkt.body().size()));
      std::cout << "code=" << r.code() << " receipts=" << r.receipts_size();
      for (const auto& rc : r.receipts()) {
        std::cout << " [" << rc.user_id() << "@" << rc.read_at() << "]";
      }
      std::cout << "\n";
    } else if (resp_id == chirp::gateway::GET_REACTIONS_RESP) {
      chirp::chat::GetReactionsResponse r;
      r.ParseFromArray(resp_pkt.body().data(), static_cast<int>(resp_pkt.body().size()));
      std::cout << "code=" << r.code() << " reactions=" << r.reactions_size();
      for (const auto& rc : r.reactions()) {
        std::cout << " [" << rc.emoji() << "x" << rc.count() << "]";
      }
      std::cout << "\n";
    } else if (resp_id == chirp::gateway::GET_TYPING_USERS_RESP) {
      chirp::chat::GetTypingUsersResponse r;
      r.ParseFromArray(resp_pkt.body().data(), static_cast<int>(resp_pkt.body().size()));
      std::cout << "code=" << r.code() << " typing_users=" << r.typing_user_ids_size();
      for (const auto& uid : r.typing_user_ids()) {
        std::cout << " [" << uid << "]";
      }
      std::cout << "\n";
    } else if (resp_id == chirp::gateway::EDIT_MESSAGE_RESP) {
      chirp::chat::EditMessageResponse r;
      r.ParseFromArray(resp_pkt.body().data(), static_cast<int>(resp_pkt.body().size()));
      std::cout << "code=" << r.code() << " edited=" << (r.has_message() ? "yes" : "no")
                << " content=" << r.message().content() << "\n";
    } else if (resp_id == chirp::gateway::DELETE_MESSAGE_RESP) {
      chirp::chat::DeleteMessageResponse r;
      r.ParseFromArray(resp_pkt.body().data(), static_cast<int>(resp_pkt.body().size()));
      std::cout << "code=" << r.code() << " permanent=" << (r.was_permanently_deleted() ? 1 : 0)
                << "\n";
    } else if (resp_id == chirp::gateway::GET_MENTION_SUGGESTIONS_RESP) {
      chirp::chat::GetMentionSuggestionsResponse r;
      r.ParseFromArray(resp_pkt.body().data(), static_cast<int>(resp_pkt.body().size()));
      std::cout << "code=" << r.code() << " suggestions=" << r.suggestions_size();
      for (const auto& s : r.suggestions()) {
        std::cout << " [" << s.display_text() << "]";
      }
      std::cout << "\n";
    } else {
      std::cout << act << " ok\n";
    }
    return 0;
  }

  chirp::chat::SendMessageRequest req;
  req.set_sender_id(sender);
  req.set_msg_type(chirp::chat::TEXT);
  req.set_content(text);
  req.set_client_timestamp(NowMs());
  if (!group.empty()) {
    req.set_channel_type(chirp::chat::GUILD);
    req.set_channel_id(group);
  } else {
    req.set_receiver_id(receiver);
    req.set_channel_type(chirp::chat::PRIVATE);
    req.set_channel_id(PrivateChannelId(sender, receiver));
  }

  chirp::gateway::Packet resp_pkt;
  if (!SendAndRead(sock, chirp::gateway::SEND_MESSAGE_REQ, seq, req.SerializeAsString(), &resp_pkt) ||
      resp_pkt.msg_id() != chirp::gateway::SEND_MESSAGE_RESP) {
    std::cerr << "send message failed\n";
    return 1;
  }

  chirp::chat::SendMessageResponse resp;
  if (!resp.ParseFromArray(resp_pkt.body().data(), static_cast<int>(resp_pkt.body().size()))) {
    std::cerr << "failed to parse SendMessageResponse\n";
    return 1;
  }

  std::cout << "code=" << resp.code() << " message_id=" << resp.message_id()
            << " server_ts=" << resp.server_timestamp() << "\n";

  if (HasArg(argc, argv, "--print-unread")) {
    chirp::chat::GetUnreadCountRequest ureq;
    ureq.set_user_id(sender);
    chirp::gateway::Packet ureq_pkt;
    if (SendAndRead(sock, chirp::gateway::GET_UNREAD_COUNT_REQ, 4, ureq.SerializeAsString(), &ureq_pkt) &&
        ureq_pkt.msg_id() == chirp::gateway::GET_UNREAD_COUNT_RESP) {
      chirp::chat::GetUnreadCountResponse uresp;
      uresp.ParseFromArray(ureq_pkt.body().data(), static_cast<int>(ureq_pkt.body().size()));
      std::cout << "unread total=" << uresp.total_unread() << " channels=" << uresp.channels_size() << "\n";
    }
  }
  return 0;
}
