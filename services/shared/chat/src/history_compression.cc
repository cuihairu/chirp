#include "history_compression.h"

#include "common/gzip.h"

namespace chirp::chat {

void CompressHistoryResponse(chirp::chat::GetHistoryResponse* resp,
                             bool supports_gzip) {
  if (!supports_gzip || resp->messages().empty()) {
    return;
  }

  // The compressed bytes stay self-describing: a GetHistoryResponse with
  // only the messages list populated, so clients reuse this message's
  // parser after inflating.
  chirp::chat::GetHistoryResponse plain;
  plain.mutable_messages()->Swap(resp->mutable_messages());
  const std::string raw = plain.SerializeAsString();

  std::string gz;
  if (!chirp::common::GzipCompress(raw, &gz)) {
    // Nothing compressible (e.g. every message serialized empty): put the
    // list back and keep the plain form rather than sending an unusable
    // gzip field.
    resp->mutable_messages()->Swap(plain.mutable_messages());
    return;
  }
  resp->set_messages_gzip(std::move(gz));
}

} // namespace chirp::chat
