#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "network/redis_client.h"
#include "proto/chat.pb.h"

namespace {

std::string GetArg(int argc, char** argv, const std::string& key, const std::string& def) {
  for (int i = 1; i < argc; ++i) {
    if (key == argv[i] && i + 1 < argc) {
      return argv[i + 1];
    }
  }
  return def;
}

uint16_t GetU16Arg(int argc, char** argv, const std::string& key, uint16_t def) {
  const std::string value = GetArg(argc, argv, key, "");
  if (value.empty()) {
    return def;
  }
  return static_cast<uint16_t>(std::atoi(value.c_str()));
}

bool GetBoolArg(int argc, char** argv, const std::string& key, bool def) {
  const std::string value = GetArg(argc, argv, key, "");
  if (value.empty()) {
    return def;
  }
  return value != "0" && value != "false" && value != "False";
}

std::string EscapeSql(std::string_view value) {
  std::string out;
  out.reserve(value.size() + 8);
  for (unsigned char c : value) {
    switch (c) {
    case 0:
      out += "\\0";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\r':
      out += "\\r";
      break;
    case '\\':
      out += "\\\\";
      break;
    case '\'':
      out += "\\'";
      break;
    case '"':
      out += "\\\"";
      break;
    case '\x1a':
      out += "\\Z";
      break;
    default:
      out.push_back(static_cast<char>(c));
      break;
    }
  }
  return out;
}

std::string EscapeShellSingleQuoted(std::string_view value) {
  std::string out;
  out.reserve(value.size() + 8);
  for (char c : value) {
    if (c == '\'') {
      out += "'\\''";
    } else {
      out.push_back(c);
    }
  }
  return out;
}

std::string BytesToHex(std::string_view value) {
  std::ostringstream oss;
  oss << std::hex << std::setfill('0');
  for (unsigned char c : value) {
    oss << std::setw(2) << static_cast<int>(c);
  }
  return oss.str();
}

void WriteSchema(std::ostream& out, const std::string& table) {
  out << "CREATE TABLE IF NOT EXISTS `" << table << "` (\n"
      << "  `message_id` varchar(128) NOT NULL,\n"
      << "  `sender_id` varchar(128) NOT NULL,\n"
      << "  `receiver_id` varchar(128) NOT NULL,\n"
      << "  `channel_type` int NOT NULL,\n"
      << "  `channel_id` varchar(255) NOT NULL,\n"
      << "  `msg_type` int NOT NULL,\n"
      << "  `content` blob NOT NULL,\n"
      << "  `timestamp_ms` bigint NOT NULL,\n"
      << "  `source_redis_key` varchar(255) NOT NULL,\n"
      << "  PRIMARY KEY (`message_id`),\n"
      << "  KEY `idx_channel_time` (`channel_id`, `timestamp_ms`)\n"
      << ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;\n\n";
}

void WriteInsert(std::ostream& out, const std::string& table, const std::string& source_key,
                 const chirp::chat::ChatMessage& msg) {
  out << "INSERT INTO `" << table << "` "
      << "(`message_id`,`sender_id`,`receiver_id`,`channel_type`,`channel_id`,`msg_type`,`content`,`timestamp_ms`,`source_redis_key`) VALUES ("
      << "'" << EscapeSql(msg.message_id()) << "',"
      << "'" << EscapeSql(msg.sender_id()) << "',"
      << "'" << EscapeSql(msg.receiver_id()) << "',"
      << static_cast<int>(msg.channel_type()) << ","
      << "'" << EscapeSql(msg.channel_id()) << "',"
      << static_cast<int>(msg.msg_type()) << ","
      << "x'" << BytesToHex(msg.content()) << "',"
      << msg.timestamp() << ","
      << "'" << EscapeSql(source_key) << "') "
      << "ON DUPLICATE KEY UPDATE "
      << "`sender_id`=VALUES(`sender_id`),"
      << "`receiver_id`=VALUES(`receiver_id`),"
      << "`channel_type`=VALUES(`channel_type`),"
      << "`channel_id`=VALUES(`channel_id`),"
      << "`msg_type`=VALUES(`msg_type`),"
      << "`content`=VALUES(`content`),"
      << "`timestamp_ms`=VALUES(`timestamp_ms`),"
      << "`source_redis_key`=VALUES(`source_redis_key`);\n";
}

void WriteAckDelete(std::ostream& out, const std::string& redis_cli_bin, const std::string& redis_host,
                    uint16_t redis_port, const std::string& key) {
  out << "'" << EscapeShellSingleQuoted(redis_cli_bin) << "'"
      << " -h '" << EscapeShellSingleQuoted(redis_host) << "'"
      << " -p " << redis_port
      << " DEL '" << EscapeShellSingleQuoted(key) << "'\n";
}

struct ExportStats {
  size_t key_count{0};
  size_t msg_count{0};
  size_t skipped{0};
};

ExportStats ExportPattern(chirp::network::RedisClient& redis,
                          const std::string& pattern,
                          const std::string& table,
                          std::ostream& sql_out,
                          std::ostream* ack_out,
                          const std::string& redis_cli_bin,
                          const std::string& redis_host,
                          uint16_t redis_port) {
  ExportStats stats;
  auto keys = redis.Keys(pattern);
  std::sort(keys.begin(), keys.end());
  WriteSchema(sql_out, table);
  sql_out << "START TRANSACTION;\n";

  for (const auto& key : keys) {
    auto raw_msgs = redis.LRange(key, 0, -1);
    ++stats.key_count;
    for (const auto& raw : raw_msgs) {
      chirp::chat::ChatMessage msg;
      if (!msg.ParseFromString(raw)) {
        ++stats.skipped;
        continue;
      }
      WriteInsert(sql_out, table, key, msg);
      ++stats.msg_count;
    }

    if (ack_out) {
      WriteAckDelete(*ack_out, redis_cli_bin, redis_host, redis_port, key);
    }
  }

  sql_out << "COMMIT;\n\n";
  return stats;
}

} // namespace

int main(int argc, char** argv) {
  const std::string redis_host = GetArg(argc, argv, "--redis_host", "127.0.0.1");
  const uint16_t redis_port = GetU16Arg(argc, argv, "--redis_port", 6379);
  const std::string output = GetArg(argc, argv, "--out", "");
  const std::string ack_output = GetArg(argc, argv, "--ack_out", "");
  const std::string redis_cli_bin = GetArg(argc, argv, "--redis_cli_bin", "redis-cli");
  const std::string history_table = GetArg(argc, argv, "--history_table", "chat_messages");
  const std::string offline_table = GetArg(argc, argv, "--offline_table", "chat_offline_messages");
  const bool include_offline = GetBoolArg(argc, argv, "--include_offline", true);

  chirp::network::RedisClient redis(redis_host, redis_port);

  std::ofstream file;
  std::ostream* out = &std::cout;
  if (!output.empty()) {
    file.open(output, std::ios::out | std::ios::trunc);
    if (!file.is_open()) {
      std::cerr << "failed to open output file: " << output << "\n";
      return 1;
    }
    out = &file;
  }

  std::ofstream ack_file;
  std::ostream* ack_out = nullptr;
  if (!ack_output.empty()) {
    ack_file.open(ack_output, std::ios::out | std::ios::trunc);
    if (!ack_file.is_open()) {
      std::cerr << "failed to open ack output file: " << ack_output << "\n";
      return 1;
    }
    ack_out = &ack_file;
    *ack_out << "#!/bin/sh\nset -eu\n";
  }

  try {
    const ExportStats history_stats =
        ExportPattern(redis, "chat:history:*", history_table, *out, ack_out, redis_cli_bin, redis_host, redis_port);

    ExportStats offline_stats;
    if (include_offline) {
      offline_stats =
          ExportPattern(redis, "chat:offline:*", offline_table, *out, ack_out, redis_cli_bin, redis_host, redis_port);
    }

    std::cerr << "history_keys=" << history_stats.key_count
              << " history_messages=" << history_stats.msg_count
              << " offline_keys=" << offline_stats.key_count
              << " offline_messages=" << offline_stats.msg_count
              << " skipped=" << (history_stats.skipped + offline_stats.skipped) << "\n";
  } catch (const std::exception& e) {
    std::cerr << e.what() << "\n";
    return 1;
  }
  return 0;
}
