#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <google/protobuf/message.h>

namespace chirp::network {

// Encodes/decodes protobuf messages with a u32_be length prefix.
class ProtobufFraming {
public:
  static std::vector<uint8_t> Encode(const google::protobuf::Message& msg);
  static bool Decode(const std::string& payload, google::protobuf::Message* out);
};

} // namespace chirp::network

