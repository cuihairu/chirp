#include "network/protobuf_framing.h"

#include <limits>

#include "network/byte_order.h"
#include <google/protobuf/arena.h>

namespace chirp::network {

std::vector<uint8_t> ProtobufFraming::Encode(const google::protobuf::Message& msg) {
  const size_t payload_size = msg.ByteSizeLong();
  if (payload_size > static_cast<size_t>(std::numeric_limits<uint32_t>::max()) ||
      payload_size > static_cast<size_t>(std::numeric_limits<int>::max())) {
    return {};
  }

  std::vector<uint8_t> out(4 + payload_size);
  WriteU32BE(out.data(), static_cast<uint32_t>(payload_size));
  if (!msg.SerializeToArray(out.data() + 4, static_cast<int>(payload_size))) {
    return {};
  }
  return out;
}

bool ProtobufFraming::Decode(const std::string& payload, google::protobuf::Message* out) {
  if (!out) {
    return false;
  }
  return out->ParseFromArray(payload.data(), static_cast<int>(payload.size()));
}

} // namespace chirp::network
