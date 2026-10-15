#pragma once

#include "proto/chat.pb.h"

namespace chirp::chat {

/// History-readback compression (game_chat_features P2 消息压缩,
/// 2026-10-15): switches a filled GetHistoryResponse to the gzip form for
/// sessions that declared LoginRequest.supports_gzip at login. The
/// messages list is swapped into a GetHistoryResponse-shaped payload that
/// carries only the list, gzip-compressed (RFC 1952 container,
/// chirp::common::GzipCompress) into the messages_gzip field. Clients
/// inflate it and parse the bytes back as this same message type, then
/// read `.messages` — no side format on the wire.
///
/// A no-op (the plain form stays in place) when the session did not declare
/// the capability, when the message list is empty, or when compression
/// fails (empty serialization — every message default-constructed — or a
/// zlib error): the caller just serializes whatever the response now holds,
/// so a legacy or failed path silently degrades to the historical shape.
void CompressHistoryResponse(chirp::chat::GetHistoryResponse* resp,
                             bool supports_gzip);

} // namespace chirp::chat
